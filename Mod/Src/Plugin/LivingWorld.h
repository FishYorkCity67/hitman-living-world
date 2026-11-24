#pragma once

#include "IPluginInterface.h"

#include <Glacier/ZEntity.h>

#include "Dialogue.h"
#include "FireGate.h"
#include "Http.h"
#include "LlmClient.h"
#include "Reactions.h"
#include "Settings.h"
#include "Speech.h"
#include "Voice.h"
#include "World.h"

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ZHM5ItemWeapon;
class IBaseCharacter;
struct SHitInfo;

// Living World – KI-NPCs & Realismus.
// Mit T (Chat) oder Push-to-Talk sprichst du mit NPCs in Hoerweite; eine lokale KI antwortet in ihrer Rolle.
// Bewaffnete NPCs warnen und verhandeln, bevor sie schiessen.
class LivingWorld : public IPluginInterface {
public:
    LivingWorld();
    ~LivingWorld() override;

    void Init() override;
    void OnEngineInitialized() override;
    void OnDrawUI(bool p_HasFocus) override;
    void OnDrawMenu() override;

private:
    // --- Einstellungen & Dienste ---------------------------------------------------------------------------------
    lw::SettingsStore MakeStore();
    lw::Settings CurrentSettings() const;
    void CommitSettings(const lw::Settings& p_Settings);
    void ApplySettingsToServices(const lw::Settings& p_Settings);
    std::wstring ModsDirectory() const;
    std::wstring WhisperModelPath(const lw::Settings& p_Settings) const;

    // --- Spiel-Thread ----------------------------------------------------------------------------------------------
    void OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent);
    static double GameNow();
    bool ReadWorld(lw::WorldSnapshot& p_World, const lw::Settings& p_Settings);
    void HandleInput(const lw::Settings& p_Settings, double p_Now);
    void HandleUtterance(const std::string& p_Text, lw::VoiceVolume p_Volume, bool p_FromVoice, double p_Now);
    void SubmitDialogue(
        const lw::NpcSnapshot& p_Npc, lw::DialogueTrigger p_Trigger, const std::string& p_Text,
        lw::VoiceVolume p_Volume, const std::string& p_AddressedTo, int p_Priority, double p_Now
    );
    void ProcessLlmResults(const lw::Settings& p_Settings, double p_Now);
    void ProcessTranscripts(const lw::Settings& p_Settings, double p_Now);
    void UpdateStandoffs(const lw::Settings& p_Settings, double p_Now);
    void UpdateAmbient(const lw::Settings& p_Settings, double p_Now);
    void UpdateAudio(const lw::Settings& p_Settings);
    void UpdateTimeScale(const lw::Settings& p_Settings);
    void UpdatePlayerDamage(const lw::Settings& p_Settings);
    void DetectDeaths(double p_Now);
    void UpdateVanillaVoices(const lw::Settings& p_Settings);
    void MuteActor(void* p_Actor);
    void ProcessPendingKills();
    void ProcessPendingDisarms(const lw::Settings& p_Settings, double p_Now);
    void RegisterKill(const lw::NpcSnapshot& p_Victim, const lw::Settings& p_Settings);
    void HandleThreat(const std::vector<uint64_t>& p_Hearers, uint64_t p_Addressee, const lw::Settings& p_Settings, double p_Now);
    void Surrender(uint64_t p_Key, const lw::Settings& p_Settings, double p_Now);
    void UpdateSurrenders(const lw::Settings& p_Settings, double p_Now, double p_Delta);
    void DeliverNpcReply(const lw::NpcSnapshot& p_Npc, lw::NpcReply p_Reply, int p_Kind, const lw::Settings& p_Settings, double p_Now);
    void ApplyWound(const lw::NpcSnapshot& p_Npc, lw::BodyZone p_Zone, lw::ViolenceKind p_Kind,
                    const lw::Settings& p_Settings, double p_Now);
    void UpdateInjuries(const lw::Settings& p_Settings, double p_Now, double p_Delta);
    void ProcessViolence(const lw::Settings& p_Settings, double p_Now);
    void QueueViolence(uint64_t p_Victim, lw::ViolenceKind p_Kind, const lw::Vec3& p_Pos, double p_Time, bool p_Killed,
                       lw::BodyZone p_Zone = lw::BodyZone::Unknown);
    void SpeakNpcLine(
        const lw::NpcSnapshot& p_Npc, const std::string& p_Text, const std::string& p_Emotion,
        const lw::Settings& p_Settings, double p_Now
    );
    void Speak47(const std::string& p_Text, const lw::Settings& p_Settings);
    std::vector<uint64_t> HearersAround(const lw::Vec3& p_Pos, float p_Radius, uint64_t p_Exclude = 0) const;
    float RadiusFor(lw::VoiceVolume p_Volume, const lw::Settings& p_Settings) const;
    void ResetSceneState(const char* p_Reason);
    void WarmUpModels();

    // --- Hooks -------------------------------------------------------------------------------------------------------
    DECLARE_PLUGIN_DETOUR(LivingWorld, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene);
    DECLARE_PLUGIN_DETOUR(LivingWorld, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound);
    DECLARE_PLUGIN_DETOUR(LivingWorld, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo);
    DECLARE_PLUGIN_DETOUR(LivingWorld, void, ZEntityManager_ActivateEntity, ZEntityManager* th, ZEntityRef* p_Entity, void* p_Unk);

    // --- Oberflaeche (Render-Thread) -------------------------------------------------------------------------------
    void DrawChatBox();
    void DrawSubtitles();
    void DrawBubbles();
    void DrawStatusHud();
    void DrawDebugOverlay();
    void DrawSettingsWindow();
    void DrawTabGeneral(lw::Settings& p_Edit, bool& p_Changed);
    void DrawTabAi(lw::Settings& p_Edit, bool& p_Changed);
    void DrawTabVoices(lw::Settings& p_Edit, bool& p_Changed);
    void DrawTabMicrophone(lw::Settings& p_Edit, bool& p_Changed);
    void DrawTabBehavior(lw::Settings& p_Edit, bool& p_Changed);
    void DrawTabNpcs();
    void DrawTabConversations();
    void DrawTabDiagnostics(lw::Settings& p_Edit, bool& p_Changed);
    bool KeyButton(const char* p_Label, int& p_Key);
    void OpenChat();
    void CloseChat();
    void SetStatus(const std::string& p_Text, float p_Seconds = 4.f);

    // --- Zustand -----------------------------------------------------------------------------------------------------
    mutable std::mutex m_SettingsMutex;
    lw::Settings m_Settings;
    std::map<std::string, std::string> m_SavedValues; // fuer "nur Geaendertes speichern"

    lw::LlmClient m_Llm;
    lw::SpeechService m_Speech;
    lw::TranscriptionService m_Stt;
    lw::MicRecorder m_Mic;
    lw::FireGate m_Gate;
    lw::DialogueBook m_Book; // nur Spiel-Thread (ausser GlobalLog)

    // Aktuelles Abbild der Spielwelt (Spiel-Thread schreibt, Oberflaeche liest eine Kopie).
    mutable std::mutex m_WorldMutex;
    lw::WorldSnapshot m_World;
    std::atomic<int> m_Generation = 0;
    bool m_WasInLevel = false;

    // Untertitel & Sprechblasen (Spiel-Thread schreibt, Render-Thread zeichnet).
    struct Subtitle {
        std::string m_Speaker;
        std::string m_Text;
        std::string m_Emotion;
        uint64_t m_NpcKey = 0;
        bool m_FromPlayer = false;
        bool m_System = false;
        double m_Created = 0.0; // Echtzeit
        float m_Duration = 6.f;
    };

    mutable std::mutex m_UiMutex;
    std::deque<Subtitle> m_Subtitles;
    std::unordered_map<uint64_t, double> m_Thinking; // NPC -> seit wann die KI rechnet
    std::string m_StatusText;
    double m_StatusUntil = 0.0;

    // Chat (Render-Thread), Anfragen ueber atomics vom Spiel-Thread.
    std::atomic<bool> m_RequestOpenChat = false;
    std::atomic<bool> m_ChatOpen = false;
    std::atomic<bool> m_UiHasFocus = false;
    std::atomic<bool> m_RequestToggleSettings = false;
    char m_ChatBuffer[512] = {};
    lw::VoiceVolume m_ChatVolume = lw::VoiceVolume::Normal;
    double m_ChatOpenedAt = 0.0;
    bool m_ShowSettings = false;
    bool m_FocusRequestedBySettings = false;
    int* m_CapturingKey = nullptr;

    struct PendingUtterance {
        std::string m_Text;
        lw::VoiceVolume m_Volume;
        bool m_FromVoice;
    };

    std::mutex m_PendingMutex;
    std::deque<PendingUtterance> m_PendingUtterances;

    // Push-to-Talk (Spiel-Thread).
    bool m_PttDown = false;
    bool m_ChatKeyDown = false;
    bool m_MenuKeyDown = false;
    std::atomic<bool> m_Recording = false;
    std::atomic<bool> m_CancelRecording = false;
    double m_PttReleasedAt = -1.0;

    // Originalstimmen: NPC -> urspruengliche Stimmvariante, Outfit -> urspruengliche Standardstimmen (zum Wiederherstellen).
    std::mutex m_VoiceMutex;
    std::unordered_map<void*, int> m_SavedVoiceVariations;
    std::atomic<bool> m_MuteVoices = false;
    double m_LastVoiceMuteCheck = -1e9;

    // Verwundungen (Spiel-Thread).
    struct Injury {
        int m_LegHits = 0;
        int m_ArmHits = 0;
        int m_TorsoHits = 0;
        bool m_Down = false;
        lw::WoundState m_State = lw::WoundState::ArmHurt;
        lw::ViolenceKind m_Kind = lw::ViolenceKind::Gunshot;
        double m_BleedDeath = -1.0;
        double m_NextCrawl = 0.0;
        double m_NextMoan = 0.0;
        double m_NextStumble = 0.0;
        double m_NextRagdollCheck = 0.0;
        lw::Vec3 m_DownPos;
        lw::Vec3 m_LastPos;
        bool m_HasLastPos = false;
    };

    std::unordered_map<uint64_t, Injury> m_Injuries;

    // NPCs, die nach abgeschwaechtem Schaden trotzdem bei 0 Lebenspunkten sind (aus dem Treffer-Hook),
    // und NPCs, denen die Waffe aus der Hand geschossen/geschlagen wurde.
    std::mutex m_KillMutex;
    std::vector<void*> m_PendingKills;
    std::vector<void*> m_PendingDisarms;

    // Entwaffnete, Bedrohte und der Ruf von 47 in dieser Mission (Spiel-Thread).
    std::unordered_set<uint64_t> m_Disarmed;
    std::unordered_map<uint64_t, double> m_ThreatenedUntil;
    std::unordered_set<uint64_t> m_CountedKills;
    std::atomic<int> m_ArmedKills = 0;
    std::atomic<int> m_CivilianKills = 0;

    // Eingeschuechterte/Entwaffnete: bleiben (fast) stehen, statt wie im Original-Spiel wegzurennen (Spiel-Thread).
    struct SurrenderState {
        double m_Until = 0.0;
        lw::Vec3 m_LastPos;
        bool m_HasLastPos = false;
    };

    std::unordered_map<uint64_t, SurrenderState> m_Surrendered;

    // Gestreamte KI-Antworten, die schon vorab gesprochen wurden (Auftrags-ID).
    std::unordered_set<uint64_t> m_SpokenEarly;
    double m_LastFrameTime = -1.0;
    double m_LastSituationLog = -1e9;
    std::atomic<bool> m_Transcribing = false;

    // Gespraechsfokus: mit wem zuletzt geredet wurde.
    uint64_t m_LastAddressee = 0;
    double m_LastAddressTime = -1e9;

    // NPCs, die 47 von sich aus ansprechen.
    std::unordered_map<uint64_t, double> m_NearSince;
    double m_LastAmbient = -1e9;

    // Abgefangene erste Schuesse (aus dem Waffen-Hook) -> Warnung aussprechen.
    std::mutex m_StandoffMutex;
    std::vector<uint64_t> m_NewStandoffs;
    std::atomic<double> m_LastPlayerShot = -1e9;
    std::atomic<int> m_BlockedShots = 0;
    std::atomic<int> m_OwnerMisses = 0;

    // Gewalt: Treffer, Schuesse und Tote -> Reaktionen von Opfern und Zeugen.
    struct ViolenceEvent {
        uint64_t m_Victim = 0;
        lw::ViolenceKind m_Kind = lw::ViolenceKind::Gunshot;
        lw::BodyZone m_Zone = lw::BodyZone::Unknown;
        lw::Vec3 m_Pos;
        double m_Time = 0.0;
        bool m_Killed = false;
    };

    std::mutex m_ViolenceMutex;
    std::vector<ViolenceEvent> m_ViolenceEvents;
    std::unordered_map<uint64_t, std::pair<double, lw::ViolenceKind>> m_LastHits; // Opfer -> Zeitpunkt, Art
    std::unordered_map<uint64_t, std::pair<bool, bool>> m_PreviousStates;         // NPC -> lebt, bewusstlos
    double m_LastShotEvent = -1e9;
    double m_LastEarwitnessVoice = -1e9;
    std::atomic<int> m_ViolenceCount = 0;

    // Positionsabhaengiges Audio: Wiedergabe-Handle -> NPC.
    std::unordered_map<uint64_t, uint64_t> m_PlayingVoices;

    // Zeitlupe beim Tippen.
    bool m_TimeScaleApplied = false;
    float m_PreviousTimeScale = 1.f;

    // Schadensmultiplikator fuer 47.
    float m_LastPlayerHp = -1.f;

    // KI-Fehler nicht im Sekundentakt melden.
    double m_LastAiErrorShown = -1e9;

    // Hintergrundaufgaben der Oberflaeche (Verbindung testen, Downloads, Mikrofontest).
    struct BackgroundTask {
        std::thread m_Thread;
        std::atomic<bool> m_Running = false;
        std::atomic<uint64_t> m_Done = 0;
        std::atomic<uint64_t> m_Total = 0;
        std::mutex m_Mutex;
        std::string m_Result;
        lw::http::CancelToken m_Cancel;

        void SetResult(const std::string& p_Text) {
            std::scoped_lock s_Lock(m_Mutex);
            m_Result = p_Text;
        }

        std::string Result() {
            std::scoped_lock s_Lock(m_Mutex);
            return m_Result;
        }

        void Join() {
            m_Cancel.Cancel();

            if (m_Thread.joinable()) {
                m_Thread.join();
            }
        }
    };

    // Fuehrt p_Fn auf einem eigenen Thread aus (eine Aufgabe gleichzeitig pro BackgroundTask).
    template <typename Fn>
    void RunTask(BackgroundTask& p_Task, Fn&& p_Fn) {
        if (p_Task.m_Running) {
            return;
        }

        if (p_Task.m_Thread.joinable()) {
            p_Task.m_Thread.join();
        }

        p_Task.m_Cancel.Reset();
        p_Task.m_Done = 0;
        p_Task.m_Total = 0;
        p_Task.m_Running = true;
        p_Task.m_Thread = std::thread([&p_Task, s_Fn = std::forward<Fn>(p_Fn)]() mutable {
            s_Fn(p_Task);
            p_Task.m_Running = false;
        });
    }

    // Echtzeit in Sekunden (fuer Untertitel, unabhaengig von Zeitlupe/Pause).
    static double RealTime();

    BackgroundTask m_ConnectionTask;
    BackgroundTask m_ModelPullTask;
    BackgroundTask m_WhisperDownloadTask;
    BackgroundTask m_TestTask;
    std::vector<std::string> m_KnownModels;
    std::mutex m_KnownModelsMutex;
    char m_PullModelName[128] = "qwen2.5:3b";
    char m_TestSentence[256] = "Guten Abend. Ich bin neu hier – wo finde ich die Küche?";
    std::atomic<bool> m_MicTestRunning = false;
    std::atomic<float> m_MicTestLevel = 0.f;
};

DECLARE_ZHM_PLUGIN(LivingWorld)
