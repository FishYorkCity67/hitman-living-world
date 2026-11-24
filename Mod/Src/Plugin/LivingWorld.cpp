#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <Logging.h>

#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZGameTime.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZSpatialEntity.h>

#include <Windows.h>

#include <filesystem>

namespace {
    constexpr const char* c_Version = "1.0.0";

    // Liefert das Verzeichnis dieser DLL (…\Retail\mods).
    std::wstring OwnDllDirectory() {
        HMODULE s_Module = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OwnDllDirectory), &s_Module
        );

        wchar_t s_Path[MAX_PATH] = {};
        GetModuleFileNameW(s_Module, s_Path, MAX_PATH);
        return std::filesystem::path(s_Path).parent_path().wstring();
    }
}

LivingWorld::LivingWorld() = default;

LivingWorld::~LivingWorld() {
    const ZMemberDelegate<LivingWorld, void(const SGameUpdateEvent&)> s_Delegate(this, &LivingWorld::OnFrameUpdate);
    Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);

    if (m_TimeScaleApplied && Globals::GameTimeManager) {
        Globals::GameTimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
    }

    if (m_ChatOpen || (m_ShowSettings && m_FocusRequestedBySettings)) {
        SDK()->ReleaseUIFocus();
    }

    m_Mic.Stop();

    for (BackgroundTask* s_Task : { &m_ConnectionTask, &m_ModelPullTask, &m_WhisperDownloadTask, &m_TestTask }) {
        s_Task->Join();
    }

    m_Llm.Stop();
    m_Speech.Stop();
    m_Stt.Stop();

    lw::log::Info("Living World beendet.");
    lw::log::Shutdown();
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------------------------------------------------

lw::SettingsStore LivingWorld::MakeStore() {
    lw::SettingsStore s_Store;

    s_Store.m_Get = [this](const std::string& p_Section, const std::string& p_Key, const std::string& p_Default) {
        const ZString s_Value = GetSetting(ZString(p_Section), ZString(p_Key), ZString(p_Default));
        std::string s_Result(s_Value.c_str(), s_Value.size());
        m_SavedValues[p_Section + "." + p_Key] = s_Result;
        return s_Result;
    };

    s_Store.m_Set = [this](const std::string& p_Section, const std::string& p_Key, const std::string& p_Value) {
        // Das SDK schreibt bei jedem SetSetting die ganze Datei – also nur Geaendertes schreiben.
        auto& s_Saved = m_SavedValues[p_Section + "." + p_Key];

        if (s_Saved != p_Value) {
            s_Saved = p_Value;
            SetSetting(ZString(p_Section), ZString(p_Key), ZString(p_Value));
        }
    };

    return s_Store;
}

lw::Settings LivingWorld::CurrentSettings() const {
    std::scoped_lock s_Lock(m_SettingsMutex);
    return m_Settings;
}

void LivingWorld::CommitSettings(const lw::Settings& p_Settings) {
    {
        std::scoped_lock s_Lock(m_SettingsMutex);
        m_Settings = p_Settings;
        lw::SaveSettings(m_Settings, MakeStore());
    }

    ApplySettingsToServices(p_Settings);
}

void LivingWorld::ApplySettingsToServices(const lw::Settings& p_Settings) {
    m_Llm.SetConfig(lw::LlmConfig::FromSettings(p_Settings));

    lw::FireGateConfig s_Gate;
    s_Gate.m_Enabled = p_Settings.m_Enabled && p_Settings.m_Deescalation;

    // Ruf: Wer schon viele Bewaffnete getoetet hat, vor dem zoegern die Wachen laenger.
    const int s_Kills = p_Settings.m_Reputation ? m_ArmedKills.load() : 0;
    s_Gate.m_WarnSeconds = p_Settings.m_WarnSeconds + static_cast<float>(std::min(15, s_Kills * 2));
    s_Gate.m_NegotiationMaxSeconds = p_Settings.m_NegotiationMaxSeconds;
    s_Gate.m_HoldFireExtend = p_Settings.m_HoldFireExtend;
    s_Gate.m_StandDownSeconds = p_Settings.m_StandDownSeconds;
    s_Gate.m_AggressionMemorySeconds = p_Settings.m_AggressionMemorySeconds;
    s_Gate.m_Group = p_Settings.m_GroupDeescalation;
    m_Gate.Configure(s_Gate);

    m_Stt.SetModelPath(WhisperModelPath(p_Settings));
    lw::log::SetDebugEnabled(p_Settings.m_DebugLog);

    // Muss schon beim Laden einer Mission gelten (NPCs werden dabei aktiviert).
    m_MuteVoices = p_Settings.m_Enabled && p_Settings.m_MuteVanillaVoices;
}

std::wstring LivingWorld::ModsDirectory() const {
    return OwnDllDirectory();
}

std::wstring LivingWorld::WhisperModelPath(const lw::Settings& p_Settings) const {
    return ModsDirectory() + L"\\LivingWorld\\models\\" + lw::Utf8ToWide(p_Settings.m_WhisperModel);
}

// ---------------------------------------------------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::Init() {
    const std::wstring s_DataDir = ModsDirectory() + L"\\LivingWorld";
    std::error_code s_Error;
    std::filesystem::create_directories(s_DataDir + L"\\models", s_Error);

    lw::log::Init(s_DataDir + L"\\LivingWorld.log");
    lw::log::SetForwarder([](lw::log::Level p_Level, const std::string& p_Text) {
        switch (p_Level) {
            case lw::log::Level::Error: Logger::Error("[LivingWorld] {}", p_Text); break;
            case lw::log::Level::Warn: Logger::Warn("[LivingWorld] {}", p_Text); break;
            case lw::log::Level::Info: Logger::Info("[LivingWorld] {}", p_Text); break;
            default: Logger::Debug("[LivingWorld] {}", p_Text); break;
        }
    });

    lw::Settings s_Settings;
    lw::LoadSettings(s_Settings, MakeStore());

    {
        std::scoped_lock s_Lock(m_SettingsMutex);
        m_Settings = s_Settings;
    }

    // Beim allerersten Start die Standardwerte in die INI schreiben, damit man sie auch von Hand sieht.
    if (!HasSetting("general", "enabled")) {
        m_SavedValues.clear();
        lw::SaveSettings(s_Settings, MakeStore());
    }

    // Einmalige Umstellung aelterer Einstellungen (Version 1 hatte das langsame 3b-Modell mit langen Antworten).
    if (GetSettingInt("general", "settings_version", 1) < 2) {
        if (s_Settings.m_LlmModel == "qwen2.5:3b" || s_Settings.m_LlmModel == "qwen2.5:1.5b") {
            s_Settings.m_LlmModel = "qwen3:1.7b";
        }

        s_Settings.m_MaxTokens = std::min(s_Settings.m_MaxTokens, 120);
        s_Settings.m_ContextSize = std::min(s_Settings.m_ContextSize, 3072);
        s_Settings.m_HistoryTurns = std::min(s_Settings.m_HistoryTurns, 6);

        {
            std::scoped_lock s_Lock(m_SettingsMutex);
            m_Settings = s_Settings;
        }

        lw::SaveSettings(s_Settings, MakeStore());
        SetSettingInt("general", "settings_version", 2);
        lw::log::Info("Einstellungen auf Version 2 umgestellt (Modell {}).", s_Settings.m_LlmModel);
    }

    // Version 3: kuerzere Prompts und weniger Nebenanfragen – Antworten kamen im Spiel zu langsam.
    if (GetSettingInt("general", "settings_version", 1) < 3) {
        s_Settings.m_HistoryTurns = std::min(s_Settings.m_HistoryTurns, 4);
        s_Settings.m_MaxBystanderReplies = std::min(s_Settings.m_MaxBystanderReplies, 1);
        s_Settings.m_MaxTokens = std::min(s_Settings.m_MaxTokens, 100);

        {
            std::scoped_lock s_Lock(m_SettingsMutex);
            m_Settings = s_Settings;
        }

        lw::SaveSettings(s_Settings, MakeStore());
        SetSettingInt("general", "settings_version", 3);
        lw::log::Info("Einstellungen auf Version 3 umgestellt (schnellere Antworten).");
    }

    lw::log::Info("Living World {} geladen. Daten: {}", c_Version, lw::WideToUtf8(s_DataDir));

    Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &LivingWorld::OnClearScene);
    Hooks::ZHM5ItemWeapon_FireProjectiles->AddDetour(this, &LivingWorld::ZHM5ItemWeapon_FireProjectiles);
    Hooks::ZActor_YouGotHit->AddDetour(this, &LivingWorld::ZActor_YouGotHit);
    Hooks::ZEntityManager_ActivateEntity->AddDetour(this, &LivingWorld::ZEntityManager_ActivateEntity);

    ApplySettingsToServices(s_Settings);
    m_Llm.Start();
    m_Speech.Start();
    m_Stt.Start();
}

void LivingWorld::OnEngineInitialized() {
    const ZMemberDelegate<LivingWorld, void(const SGameUpdateEvent&)> s_Delegate(this, &LivingWorld::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);
    lw::log::Info("Spiel-Update registriert.");
}

double LivingWorld::GameNow() {
    return Globals::GameTimeManager ? Globals::GameTimeManager->m_tGameTime.ToSeconds() : 0.0;
}

void LivingWorld::ResetSceneState(const char* p_Reason) {
    ++m_Generation;

    m_Llm.DropPending();
    m_Speech.CancelAll();
    m_Gate.Reset();
    m_Book.Clear();

    {
        std::scoped_lock s_Lock(m_UiMutex);
        m_Subtitles.clear();
        m_Thinking.clear();
    }

    {
        std::scoped_lock s_Lock(m_PendingMutex);
        m_PendingUtterances.clear();
    }

    {
        std::scoped_lock s_Lock(m_StandoffMutex);
        m_NewStandoffs.clear();
    }

    {
        std::scoped_lock s_Lock(m_ViolenceMutex);
        m_ViolenceEvents.clear();
    }

    m_LastHits.clear();
    m_PreviousStates.clear();
    m_Injuries.clear();
    m_LastFrameTime = -1.0;

    {
        std::scoped_lock s_Lock(m_VoiceMutex);
        m_SavedVoiceVariations.clear(); // die NPCs dieser Szene gibt es gleich nicht mehr
    }

    {
        std::scoped_lock s_Lock(m_KillMutex);
        m_PendingKills.clear();
        m_PendingDisarms.clear();
    }

    m_Disarmed.clear();
    m_ThreatenedUntil.clear();
    m_Surrendered.clear();
    m_SpokenEarly.clear();
    m_CountedKills.clear();
    m_ArmedKills = 0;
    m_CivilianKills = 0;

    // Das Mikrofon gehoert dem Render-Thread – dort beenden lassen.
    m_CancelRecording = true;

    m_NearSince.clear();
    m_PlayingVoices.clear();
    m_LastAddressee = 0;
    m_LastPlayerHp = -1.f;
    m_RequestOpenChat = false;

    if (m_TimeScaleApplied && Globals::GameTimeManager) {
        Globals::GameTimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
        m_TimeScaleApplied = false;
    }

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        m_World = {};
    }

    m_WasInLevel = false;
    lw::log::Info("Zustand zurueckgesetzt ({}).", p_Reason);
}

void LivingWorld::WarmUpModels() {
    const lw::Settings s_Settings = CurrentSettings();

    if (!s_Settings.m_Enabled) {
        return;
    }

    // Sprachmodell vorladen: der erste Aufruf nach dem Start dauert sonst sehr lange.
    if (!m_ConnectionTask.m_Running) {
        RunTask(m_ConnectionTask, [this, s_Settings](BackgroundTask& p_Task) {
            std::string s_Error;

            if (!m_Llm.EnsureServer(s_Error)) {
                p_Task.SetResult("KI nicht erreichbar: " + s_Error);
                lw::log::Warn("Vorwaermen: {}", s_Error);
                return;
            }

            // Mit dem echten Regeltext: den merkt sich der Server (Prompt-Cache), jede spaetere Antwort spart sich das Lesen.
            lw::LlmJob s_Job;
            s_Job.m_Messages.push_back({ "system", lw::SystemPrompt(s_Settings.m_Language) });
            s_Job.m_Messages.push_back({ "user", "Antworte nur mit OK." });
            const auto s_Result = m_Llm.RunNow(s_Job, &p_Task.m_Cancel);
            p_Task.SetResult(
                s_Result.m_Ok ? "KI bereit (" + s_Settings.m_LlmModel + ", " + lw::FormatSeconds(s_Result.m_Seconds) + ")"
                              : "KI-Fehler: " + s_Result.m_Error
            );
            lw::log::Info("KI vorgewaermt: {} ({:.1f} s)", s_Result.m_Ok ? "ok" : s_Result.m_Error, s_Result.m_Seconds);
        });
    }

    if (s_Settings.m_SttEnabled && GetFileAttributesW(WhisperModelPath(s_Settings).c_str()) != INVALID_FILE_ATTRIBUTES) {
        m_Stt.Preload();
    }
}

double LivingWorld::RealTime() {
    static LARGE_INTEGER s_Frequency = []() {
        LARGE_INTEGER s_Value;
        QueryPerformanceFrequency(&s_Value);
        return s_Value;
    }();

    LARGE_INTEGER s_Counter;
    QueryPerformanceCounter(&s_Counter);
    return static_cast<double>(s_Counter.QuadPart) / static_cast<double>(s_Frequency.QuadPart);
}

void LivingWorld::SetStatus(const std::string& p_Text, float p_Seconds) {
    std::scoped_lock s_Lock(m_UiMutex);
    m_StatusText = p_Text;
    m_StatusUntil = RealTime() + p_Seconds;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------------------------------------------------

DEFINE_PLUGIN_DETOUR(LivingWorld, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene) {
    ResetSceneState("Szene entladen");
    return { HookAction::Continue() };
}

DEFINE_PLUGIN_DETOUR(LivingWorld, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound) {
    if (!th) {
        return { HookAction::Continue() };
    }

    const double s_Now = GameNow();
    const auto s_Player = SDK()->GetLocalPlayer();

    // 47 schiesst: zaehlt als Angriff – die Wachen duerfen sofort zurueckschiessen.
    if (s_Player && th->m_pOwner == s_Player.m_entityRef) {
        m_LastPlayerShot = s_Now;
        m_Gate.OnPlayerAggression(s_Now);

        // Schuesse sind weithin zu hoeren (Salven nur einmal melden).
        lw::Vec3 s_PlayerPos;

        {
            std::scoped_lock s_Lock(m_WorldMutex);
            s_PlayerPos = m_World.m_Player.m_Pos;
        }

        QueueViolence(0, lw::ViolenceKind::ShotHeard, s_PlayerPos, s_Now, false);
        return { HookAction::Continue() };
    }

    // Welcher NPC haelt die Waffe? (Besitzer selbst oder sein logischer Elternteil.)
    uint64_t s_Key = 0;

    {
        std::scoped_lock s_Lock(m_WorldMutex);

        if (!m_World.m_InLevel) {
            return { HookAction::Continue() };
        }

        const ZEntityRef s_Candidates[] = {
            th->m_pOwner, th->m_pOwner.GetLogicalParent(), th->m_pOwner.GetOwningEntity()
        };

        for (const ZEntityRef& s_Candidate : s_Candidates) {
            const auto s_Raw = reinterpret_cast<uint64_t>(s_Candidate.m_pObj);

            if (s_Raw != 0 && m_World.FindNpc(s_Raw)) {
                s_Key = s_Raw;
                break;
            }
        }
    }

    if (s_Key == 0) {
        if (++m_OwnerMisses <= 3) {
            lw::log::Debug("Schuss einer Waffe ohne bekannten NPC-Besitzer – unveraendert durchgelassen.");
        }

        return { HookAction::Continue() };
    }

    const lw::GateDecision s_Decision = m_Gate.OnShotAttempt(s_Key, s_Now);

    if (s_Decision.m_FirstAttempt) {
        std::scoped_lock s_Lock(m_StandoffMutex);
        m_NewStandoffs.push_back(s_Key);
    }

    if (s_Decision.m_Block) {
        ++m_BlockedShots;
        return { HookAction::Return(), false };
    }

    return { HookAction::Continue() };
}

DEFINE_PLUGIN_DETOUR(LivingWorld, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo) {
    if (!th) {
        return { HookAction::Continue() };
    }

    const double s_Now = GameNow();
    auto* s_Actor = static_cast<ZActor*>(th);
    ZEntityRef s_Ref;
    s_Actor->GetID(s_Ref);
    const auto s_Key = reinterpret_cast<uint64_t>(s_Ref.m_pObj);

    // Das Spiel verraet die Waffe nicht: kurz vorher geschossen = Schuss, sonst direkt daneben = Nahkampf.
    // Haelt 47 dabei sichtbar eine Waffe (Messer, Klinge ...), gilt es als Stich.
    const bool s_Shot = s_Now - m_LastPlayerShot.load() < 1.5;
    bool s_Melee = false;
    bool s_Armed = false;
    bool s_AlreadyDead = false;
    lw::Vec3 s_Pos;
    lw::BodyZone s_Zone = lw::BodyZone::Unknown;

    {
        std::scoped_lock s_Lock(m_WorldMutex);

        if (const auto* s_Npc = m_World.FindNpc(s_Key)) {
            s_Melee = !s_Shot && s_Npc->m_Distance < 2.5f;
            s_Pos = s_Npc->m_Pos;
            s_AlreadyDead = !s_Npc->m_Alive;

            // Trefferzone aus der Zielrichtung: dort, wo der Kamerastrahl die Koerperachse kreuzt.
            if (s_Shot) {
                s_Zone = lw::EstimateHitZone(m_World.m_CameraPos, m_World.m_CameraForward, s_Npc->m_Pos);
            }
        }

        s_Armed = m_World.m_Ai.m_IllegalWeapon;
    }

    // Schuesse auf Leichen loesen keine neuen Reaktionen aus.
    if (s_AlreadyDead) {
        return { HookAction::Continue() };
    }

    if (s_Shot || s_Melee) {
        m_Gate.OnPlayerAggression(s_Now);
        const lw::ViolenceKind s_Kind = s_Shot ? lw::ViolenceKind::Gunshot
            : (s_Armed ? lw::ViolenceKind::Stab : lw::ViolenceKind::Melee);
        QueueViolence(s_Key, s_Kind, s_Pos, s_Now, false, s_Zone);
    }

    // Realistische Trefferzonen: Bein/Arm/Rumpf sind nicht sofort toedlich. Das Spiel rechnet den Schaden selbst aus
    // (mit grossem Puffer, damit niemand dabei stirbt), danach ziehen wir nur einen Teil davon ab.
    // m_fCurrentHitPoints ist ein Feld, das auch DebugMod und CheatMenu (One-Hit-Kill) benutzen.
    const lw::Settings s_Settings = CurrentSettings();
    float s_Factor = 1.f;

    if (s_Shot && s_Settings.m_Enabled && s_Settings.m_Injuries && s_Settings.m_RealisticHitZones) {
        switch (s_Zone) {
            case lw::BodyZone::Leg: s_Factor = 0.3f; break;
            case lw::BodyZone::Arm: s_Factor = 0.35f; break;
            case lw::BodyZone::Torso: s_Factor = 0.6f; break;
            default: break;
        }
    }

    // Messerstich: nicht sofort toedlich – der NPC soll zu Boden gehen, kriechen und verbluten (siehe Injuries.cpp).
    if (s_Melee && s_Armed && s_Settings.m_Enabled && s_Settings.m_Injuries && s_Settings.m_StabWounds) {
        s_Factor = 0.3f;
    }

    // Entwaffnen: Treffer in den Arm oder ein Schlag aus naechster Naehe (ohne Klinge) schlaegt die Waffe aus der Hand.
    // Das eigentliche Fallenlassen passiert im naechsten Frame auf dem Spiel-Thread.
    if (s_Settings.m_Enabled && s_Settings.m_Disarm && (s_Zone == lw::BodyZone::Arm || (s_Melee && !s_Armed)) &&
        s_Actor->m_pInventoryHandler && s_Actor->m_pInventoryHandler->m_rMainWeapon) {
        static thread_local uint32_t s_Seed = 0x9E3779B9u;
        s_Seed = s_Seed * 1664525u + 1013904223u;

        if (static_cast<int>((s_Seed >> 8) % 100) < s_Settings.m_DisarmChance) {
            std::scoped_lock s_Lock(m_KillMutex);
            m_PendingDisarms.push_back(s_Actor);
        }
    }

    if (s_Factor < 1.f && s_Actor->m_fCurrentHitPoints > 0.f) {
        constexpr float c_Buffer = 100000.f;
        const float s_Before = s_Actor->m_fCurrentHitPoints;

        s_Actor->m_fCurrentHitPoints = s_Before + c_Buffer;
        const bool s_Result = p_Hook->CallOriginal(th, hitInfo);
        const float s_After = s_Actor->m_fCurrentHitPoints;

        // Hat das Spiel den Wert ganz anders gesetzt (unerwartet), lassen wir sein Ergebnis stehen.
        if (s_After > c_Buffer * 0.5f) {
            const float s_Damage = std::max(0.f, s_Before + c_Buffer - s_After);
            const float s_NewHp = s_Before - s_Damage * s_Factor;

            if (s_NewHp > 0.f) {
                s_Actor->m_fCurrentHitPoints = s_NewHp;
            }
            else {
                // Auch abgeschwaecht toedlich: sauber ueber das Spiel sterben lassen (im naechsten Frame).
                s_Actor->m_fCurrentHitPoints = 0.01f;
                std::scoped_lock s_Lock(m_KillMutex);
                m_PendingKills.push_back(s_Actor);
            }
        }

        return { HookAction::Return(), s_Result };
    }

    return { HookAction::Continue() };
}

DEFINE_ZHM_PLUGIN(LivingWorld);
