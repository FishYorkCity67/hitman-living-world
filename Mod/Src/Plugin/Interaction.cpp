// Spiel-Thread: Eingaben, Gespraeche, Deeskalation, Audio. Alles hier laeuft im Frame-Update des Spiels.

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZApplicationEngineWin32.h>
#include <Glacier/ZGameTime.h>
#include <Glacier/ZHitman5.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace {
    bool IsGerman(const lw::Settings& p_Settings) {
        return p_Settings.m_Language == lw::Language::German;
    }

    float SubtitleDuration(const std::string& p_Text, const lw::Settings& p_Settings) {
        const size_t s_Words = lw::SplitWords(p_Text).size();
        return std::clamp(2.5f + static_cast<float>(s_Words) * 0.38f, 3.f, std::max(4.f, p_Settings.m_SubtitleSeconds + 4.f));
    }
}

void LivingWorld::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {
    const lw::Settings s_Settings = CurrentSettings();
    const double s_Now = GameNow();

    lw::WorldSnapshot s_World;
    const bool s_InLevel = s_Settings.m_Enabled && ReadWorld(s_World, s_Settings);

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        m_World = std::move(s_World);
    }

    if (!s_InLevel) {
        if (m_TimeScaleApplied) {
            UpdateTimeScale(s_Settings);
        }

        return;
    }

    if (!m_WasInLevel) {
        m_WasInLevel = true;
        lw::log::Info(
            "Mission erkannt: {} ({} NPCs in der Naehe, Szene {})", m_World.m_LocationName, m_World.m_Npcs.size(),
            m_World.m_Scene
        );
        WarmUpModels();
    }

    // Kurzer Lagebericht im Protokoll (hilft bei Fehlersuche: sieht die Mod die NPCs?).
    if (RealTime() - m_LastSituationLog > 60.0) {
        m_LastSituationLog = RealTime();
        lw::log::Info(
            "Lage: {} Akteure aktiv, {} NPCs im Umkreis von 45 m, Alarmstufe {}", m_World.m_ActorCount, m_World.m_Npcs.size(),
            m_World.m_Ai.m_Tension
        );
    }

    std::deque<PendingUtterance> s_Pending;

    {
        std::scoped_lock s_Lock(m_PendingMutex);
        s_Pending.swap(m_PendingUtterances);
    }

    for (const auto& s_Utterance : s_Pending) {
        HandleUtterance(s_Utterance.m_Text, s_Utterance.m_Volume, s_Utterance.m_FromVoice, s_Now);
    }

    const double s_Delta = m_LastFrameTime < 0.0 ? 0.0 : std::clamp(s_Now - m_LastFrameTime, 0.0, 0.25);
    m_LastFrameTime = s_Now;

    UpdateVanillaVoices(s_Settings);
    ProcessPendingKills();
    ProcessPendingDisarms(s_Settings, s_Now);
    DetectDeaths(s_Now);
    ProcessViolence(s_Settings, s_Now);
    UpdateInjuries(s_Settings, s_Now, s_Delta);
    UpdateSurrenders(s_Settings, s_Now, s_Delta);
    ProcessTranscripts(s_Settings, s_Now);
    ProcessLlmResults(s_Settings, s_Now);
    UpdateStandoffs(s_Settings, s_Now);
    UpdateAmbient(s_Settings, s_Now);
    UpdateAudio(s_Settings);
    UpdateTimeScale(s_Settings);
    UpdatePlayerDamage(s_Settings);
}

// ---------------------------------------------------------------------------------------------------------------------
// Eingaben
// ---------------------------------------------------------------------------------------------------------------------

// Laeuft im Render-Thread (OnDrawUI), damit die Tasten auch im Menue und in der Pause reagieren –
// das Spiel-Update laeuft dort nicht.
void LivingWorld::HandleInput(const lw::Settings& p_Settings, double p_Now) {
    // Gehoert das Vordergrundfenster zu HITMAN? (Prozess-ID statt Fenster-Handle: robust bei Vollbild/Unterfenstern.)
    DWORD s_ForegroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &s_ForegroundProcess);
    const bool s_Foreground = s_ForegroundProcess == GetCurrentProcessId();

    const auto s_Down = [&](int p_Key) {
        return s_Foreground && p_Key > 0 && (GetAsyncKeyState(p_Key) & 0x8000) != 0;
    };

    if (m_CancelRecording.exchange(false) && m_Recording) {
        m_Mic.Stop();
        m_Recording = false;
    }

    bool s_InLevel = false;

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        s_InLevel = m_World.m_InLevel;
    }

    // Chat und Sprechen nur in einer Mission; das Einstellungsfenster (F8) immer.
    if (!p_Settings.m_Enabled || !s_InLevel) {
        const bool s_MenuOnly = s_Down(p_Settings.m_MenuKey);

        if (s_MenuOnly && !m_MenuKeyDown && m_CapturingKey == nullptr) {
            m_RequestToggleSettings = true;
        }

        m_MenuKeyDown = s_MenuOnly;
        m_ChatKeyDown = s_Down(p_Settings.m_ChatKey);

        if (m_Recording) {
            m_Mic.Stop();
            m_Recording = false;
        }

        m_PttDown = false;
        return;
    }

    // Waehrend der Chat oder ein Menue den Fokus hat, sind Tasten Texteingabe – keine Kuerzel.
    const bool s_UiFocus = m_UiHasFocus || m_ChatOpen;

    const bool s_Menu = s_Down(p_Settings.m_MenuKey);

    if (s_Menu && !m_MenuKeyDown && !m_ChatOpen && m_CapturingKey == nullptr) {
        m_RequestToggleSettings = true;
    }

    m_MenuKeyDown = s_Menu;

    const bool s_Chat = s_Down(p_Settings.m_ChatKey);

    if (s_Chat && !m_ChatKeyDown && !s_UiFocus && !m_Recording) {
        m_RequestOpenChat = true;
    }

    m_ChatKeyDown = s_Chat;

    if (!p_Settings.m_SttEnabled) {
        if (m_Recording) {
            m_Mic.Stop();
            m_Recording = false;
        }

        m_PttDown = false;
        return;
    }

    const bool s_Ptt = s_Down(p_Settings.m_PushToTalkKey) && !s_UiFocus;

    if (s_Ptt && !m_PttDown) {
        m_PttReleasedAt = -1.0;

        if (!m_Recording) {
            std::string s_Error;

            if (m_Mic.Start(p_Settings.m_MicDevice, s_Error)) {
                m_Recording = true;
                lw::log::Info("Push-to-Talk: Aufnahme gestartet.");
            }
            else {
                SetStatus(s_Error, 6.f);
                lw::log::Warn("Mikrofon: {}", s_Error);
            }
        }
    }
    else if (!s_Ptt && m_PttDown && m_Recording) {
        // Losgelassen: noch kurz weiter aufnehmen – viele lassen die Taste schon beim letzten Wort los.
        m_PttReleasedAt = RealTime();
    }

    if (!s_Ptt && m_Recording && m_PttReleasedAt > 0.0 && RealTime() - m_PttReleasedAt >= 0.35) {
        m_PttReleasedAt = -1.0;
        std::vector<float> s_Samples = m_Mic.Stop();
        m_Recording = false;

        if (s_Samples.size() < static_cast<size_t>(lw::MicRecorder::c_SampleRate * 35 / 100)) {
            SetStatus(IsGerman(p_Settings) ? "Zu kurz – Taste beim Sprechen gedrückt halten." : "Too short – hold the key while speaking.");
        }
        else {
            lw::TranscriptJob s_Job;
            s_Job.m_LoudnessDb = lw::MeasureLoudnessDb(s_Samples);
            s_Job.m_Samples = std::move(s_Samples);
            s_Job.m_Language = IsGerman(p_Settings) ? "de" : "en";
            s_Job.m_Threads = p_Settings.m_SttThreads;
            s_Job.m_Generation = m_Generation;

            // Namen der NPCs in der Naehe als Hinweis fuer die Erkennung (sonst werden sie oft falsch geschrieben).
            {
                std::scoped_lock s_Lock(m_WorldMutex);
                std::string s_Prompt;

                for (const auto& s_Npc : m_World.m_Npcs) {
                    if (s_Npc.m_Distance > 15.f || s_Prompt.size() > 200) {
                        break;
                    }

                    if (lw::LooksLikePersonName(s_Npc.m_Name)) {
                        s_Prompt += s_Npc.m_Name + ", ";
                    }
                }

                s_Job.m_Prompt = s_Prompt;
            }

            m_Stt.Submit(std::move(s_Job));
        }
    }

    m_PttDown = s_Ptt;
}

void LivingWorld::ProcessTranscripts(const lw::Settings& p_Settings, double p_Now) {
    lw::TranscriptResult s_Result;

    while (m_Stt.TryPopResult(s_Result)) {
        if (s_Result.m_Generation != m_Generation) {
            continue;
        }

        if (!s_Result.m_Ok) {
            SetStatus((IsGerman(p_Settings) ? "Spracheingabe: " : "Voice input: ") + s_Result.m_Error, 5.f);
            continue;
        }

        lw::VoiceVolume s_Volume = lw::VoiceVolume::Normal;

        if (p_Settings.m_MicSetsVolume) {
            if (s_Result.m_LoudnessDb < p_Settings.m_WhisperThresholdDb) {
                s_Volume = lw::VoiceVolume::Whisper;
            }
            else if (s_Result.m_LoudnessDb > p_Settings.m_ShoutThresholdDb) {
                s_Volume = lw::VoiceVolume::Shout;
            }
        }

        lw::log::Info(
            "Sprache erkannt in {:.1f} s ({:.0f} dB): {}", s_Result.m_Seconds, s_Result.m_LoudnessDb, s_Result.m_Text
        );
        HandleUtterance(s_Result.m_Text, s_Volume, true, p_Now);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Gespraeche
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::HandleUtterance(const std::string& p_Text, lw::VoiceVolume p_Volume, bool p_FromVoice, double p_Now) {
    const lw::Settings s_Settings = CurrentSettings();
    const std::string s_Text = lw::TruncateUtf8(lw::Trim(p_Text), 400);

    if (s_Text.empty() || !m_World.m_InLevel) {
        return;
    }

    const bool s_De = IsGerman(s_Settings);
    const float s_Radius = RadiusFor(p_Volume, s_Settings);
    const lw::AddresseeChoice s_Choice = lw::ChooseAddressee(
        m_World, s_Text, s_Radius, m_LastAddressee, m_LastAddressTime, p_Now
    );

    {
        std::scoped_lock s_Lock(m_UiMutex);
        Subtitle s_Subtitle;
        s_Subtitle.m_Speaker = "47";
        s_Subtitle.m_Text = s_Text;
        s_Subtitle.m_FromPlayer = true;
        s_Subtitle.m_Created = RealTime();
        s_Subtitle.m_Duration = SubtitleDuration(s_Text, s_Settings);
        s_Subtitle.m_Emotion = p_Volume == lw::VoiceVolume::Shout ? "shout" : (p_Volume == lw::VoiceVolume::Whisper ? "whisper" : "");
        m_Subtitles.push_back(std::move(s_Subtitle));

        while (m_Subtitles.size() > 8) {
            m_Subtitles.pop_front();
        }
    }

    m_Book.AddToGlobalLog({ p_Now, "47", s_Text, true, "" });

    if (s_Settings.m_Tts47 && !p_FromVoice) {
        Speak47(s_Text, s_Settings);
    }

    // Erst alle Mithoerer "kennenlernen", damit sie sich den Satz merken koennen.
    for (const uint64_t s_Key : s_Choice.m_Hearers) {
        if (const auto* s_Npc = m_World.FindNpc(s_Key)) {
            m_Book.Mind(*s_Npc, m_World, s_Settings.m_Language);
        }
    }

    const lw::NpcSnapshot* s_Addressee = s_Choice.m_Key ? m_World.FindNpc(s_Choice.m_Key) : nullptr;

    // Drohung ("ich bin bewaffnet", "ich bring dich um" ...): sofort Panik/Betteln, ohne auf die KI zu warten.
    if (s_Settings.m_ThreatReactions && lw::IsThreat(s_Text)) {
        HandleThreat(s_Choice.m_Hearers, s_Choice.m_Key, s_Settings, p_Now);
    }

    if (!s_Addressee) {
        m_Book.RecordPlayerLine(s_Text, p_Volume, 0, s_De ? "niemanden" : "no one", s_Choice.m_Hearers, p_Now);
        SetStatus(
            s_Choice.m_Hearers.empty()
                ? (s_De ? "Niemand ist nah genug, um dich zu hören." : "Nobody is close enough to hear you.")
                : (s_De ? "Sieh die Person an, mit der du reden willst." : "Look at the person you want to talk to."),
            4.f
        );
        return;
    }

    const std::string s_AddresseeName = lw::DisplayName(*s_Addressee);

    // Reden verschafft Zeit, falls gerade jemand auf 47 zielt.
    m_Gate.KeepNegotiating(s_Addressee->m_Key, p_Now, 10.f);

    // Prompts bauen, BEVOR der Satz im Gedaechtnis landet (sonst stuende er doppelt drin).
    SubmitDialogue(*s_Addressee, lw::DialogueTrigger::PlayerSpoke, s_Text, p_Volume, "", 10, p_Now);

    if (s_Settings.m_BystandersReact && p_Volume == lw::VoiceVolume::Shout) {
        int s_Extra = 0;

        for (const auto& s_Npc : m_World.m_Npcs) {
            if (s_Extra >= s_Settings.m_MaxBystanderReplies) {
                break;
            }

            if (s_Npc.m_Key == s_Addressee->m_Key || !s_Npc.CanTalk() || s_Npc.m_Distance > s_Radius) {
                continue;
            }

            SubmitDialogue(s_Npc, lw::DialogueTrigger::Overheard, s_Text, p_Volume, s_AddresseeName, 1, p_Now);
            ++s_Extra;
        }
    }

    m_Book.RecordPlayerLine(s_Text, p_Volume, s_Addressee->m_Key, s_AddresseeName, s_Choice.m_Hearers, p_Now);
    m_LastAddressee = s_Addressee->m_Key;
    m_LastAddressTime = p_Now;

    lw::log::Info("47 -> {} ({:.1f} m, {} Mithoerer): {}", s_AddresseeName, s_Addressee->m_Distance, s_Choice.m_Hearers.size() - 1, s_Text);
}

void LivingWorld::SubmitDialogue(
    const lw::NpcSnapshot& p_Npc, lw::DialogueTrigger p_Trigger, const std::string& p_Text, lw::VoiceVolume p_Volume,
    const std::string& p_AddressedTo, int p_Priority, double p_Now
) {
    const lw::Settings s_Settings = CurrentSettings();

    lw::PromptContext s_Context;
    s_Context.m_Settings = &s_Settings;
    s_Context.m_World = &m_World;
    s_Context.m_Npc = &p_Npc;
    s_Context.m_Trigger = p_Trigger;
    s_Context.m_PlayerText = p_Text;
    s_Context.m_Volume = p_Volume;
    s_Context.m_AddressedTo = p_AddressedTo;
    s_Context.m_Gate = m_Gate.State(p_Npc.m_Key, p_Now);
    s_Context.m_GateSecondsLeft = m_Gate.SecondsLeft(p_Npc.m_Key, p_Now);

    const auto s_Threat = m_ThreatenedUntil.find(p_Npc.m_Key);
    s_Context.m_Threatened = s_Threat != m_ThreatenedUntil.end() && s_Threat->second > p_Now;
    s_Context.m_Disarmed = m_Disarmed.contains(p_Npc.m_Key);

    if (s_Settings.m_Reputation) {
        s_Context.m_ArmedKills = m_ArmedKills;
        s_Context.m_CivilianKills = m_CivilianKills;
    }

    lw::LlmJob s_Job;
    s_Job.m_Messages = m_Book.BuildMessages(s_Context);
    s_Job.m_Schema = lw::ReplySchema();
    s_Job.m_Tag = p_Npc.m_Key;
    s_Job.m_Kind = static_cast<int>(p_Trigger);
    s_Job.m_Generation = m_Generation;
    s_Job.m_Priority = p_Priority;
    s_Job.m_Extra = p_Text;

    // Schneller: Mithoerer-Reaktionen und Ansprachen verfallen, wenn sie zu lange warten mussten;
    // eine neue Frage an denselben NPC ersetzt die alte.
    if (p_Trigger == lw::DialogueTrigger::Overheard) {
        s_Job.m_MaxWait = 6.0;
    }
    else if (p_Trigger == lw::DialogueTrigger::Initiate) {
        s_Job.m_MaxWait = 8.0;
    }
    else {
        s_Job.m_ReplaceSameTag = true;
    }

    // Gesprochenen Satz schon ausgeben, waehrend die KI noch die Zahlen dahinter schreibt.
    if (s_Settings.m_FastReplies) {
        s_Job.m_EarlyReady = [](const std::string& p_SoFar) {
            std::string s_Say;
            std::string s_Emotion;
            return lw::ExtractEarlyReply(p_SoFar, s_Say, s_Emotion);
        };
    }

    if (s_Settings.m_LogPrompts) {
        std::string s_Dump;

        for (const auto& s_Message : s_Job.m_Messages) {
            s_Dump += "\n--- " + s_Message.m_Role + " ---\n" + s_Message.m_Content;
        }

        lw::log::Info("Prompt fuer {}:{}", lw::DisplayName(p_Npc), s_Dump);
    }

    m_Llm.Submit(std::move(s_Job));

    if (p_Trigger != lw::DialogueTrigger::Overheard) {
        std::scoped_lock s_Lock(m_UiMutex);
        m_Thinking[p_Npc.m_Key] = RealTime();
    }
}

void LivingWorld::ProcessLlmResults(const lw::Settings& p_Settings, double p_Now) {
    lw::LlmResult s_Result;

    while (m_Llm.TryPopResult(s_Result)) {
        {
            std::scoped_lock s_Lock(m_UiMutex);
            m_Thinking.erase(s_Result.m_Tag);
        }

        if (s_Result.m_Generation != m_Generation) {
            continue;
        }

        if (s_Result.m_Dropped) {
            lw::log::Info("KI-Anfrage verworfen ({}, {:.1f} s gewartet).", s_Result.m_Error, s_Result.m_WaitSeconds);
            continue;
        }

        // Vorab: der Satz ist fertig – sofort sprechen, Rest (Aktion, Haltung) kommt mit dem Endergebnis.
        if (s_Result.m_Partial) {
            lw::NpcReply s_Early;

            if (!lw::ExtractEarlyReply(s_Result.m_Content, s_Early.m_Say, s_Early.m_Emotion)) {
                continue;
            }

            s_Early.m_Say = lw::StripAssistantPhrases(lw::CleanSpokenText(s_Early.m_Say));
            const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Result.m_Tag);

            if (!s_Npc || !s_Npc->CanTalk() || s_Early.m_Say.empty()) {
                continue; // dann eben mit dem Endergebnis
            }

            m_SpokenEarly.insert(s_Result.m_Id);
            lw::log::Info(
                "{} ({:.1f} s + {:.1f} s Warten, vorab): \"{}\" [{}]", lw::DisplayName(*s_Npc), s_Result.m_Seconds,
                s_Result.m_WaitSeconds, s_Early.m_Say, s_Early.m_Emotion
            );
            DeliverNpcReply(*s_Npc, s_Early, s_Result.m_Kind, p_Settings, p_Now);
            continue;
        }

        const bool s_AlreadySpoken = m_SpokenEarly.erase(s_Result.m_Id) > 0;

        if (!s_Result.m_Ok) {
            if (RealTime() - m_LastAiErrorShown > 20.0) {
                m_LastAiErrorShown = RealTime();
                SetStatus((IsGerman(p_Settings) ? "KI antwortet nicht: " : "AI not responding: ") + s_Result.m_Error, 8.f);
            }

            continue;
        }

        lw::NpcReply s_Reply = lw::ParseReply(s_Result.m_Content);
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Result.m_Tag);

        if (!s_Npc || !s_Npc->CanTalk() || !s_Reply.m_Valid) {
            continue;
        }

        // Kleine Modelle setzen Aktionen auch ohne Konfrontation – nur anwenden, wenn es Sinn ergibt.
        lw::NpcAction s_Action = s_Reply.m_Action;
        const lw::GateState s_State = m_Gate.State(s_Npc->m_Key, p_Now);
        const bool s_Armed = s_Npc->m_Guard || s_Npc->m_WeaponOut;

        if (!s_Armed) {
            s_Action = lw::NpcAction::None;
        }
        else if (s_Action == lw::NpcAction::OpenFire && s_State != lw::GateState::Warning && s_State != lw::GateState::Firing) {
            s_Action = lw::NpcAction::None;
        }
        else if (s_Action == lw::NpcAction::HoldFire && s_State == lw::GateState::Idle) {
            s_Action = lw::NpcAction::None;
        }
        else if (s_Action == lw::NpcAction::StandDown && s_State == lw::GateState::Idle && s_Reply.m_Trust <= 0) {
            s_Action = lw::NpcAction::None;
        }

        // Entwaffnet oder eingeschuechtert: kein Feuerbefehl mehr, egal was das Modell schreibt.
        if (s_Action == lw::NpcAction::OpenFire && (m_Disarmed.contains(s_Npc->m_Key) || m_Surrendered.contains(s_Npc->m_Key))) {
            s_Action = lw::NpcAction::StandDown;
        }

        m_Book.ApplyAttitude(s_Npc->m_Key, s_Reply);

        const std::string s_Name = lw::DisplayName(*s_Npc);

        if (s_Action != lw::NpcAction::None) {
            m_Gate.Apply(s_Npc->m_Key, s_Action, p_Now);
            lw::log::Info("{} {}.", s_Name, lw::ActionLabel(s_Action, lw::Language::German));

            if (p_Settings.m_ShowHints) {
                SetStatus(s_Name + " " + lw::ActionLabel(s_Action, p_Settings.m_Language) + ".", 4.f);
            }
        }

        // Zeiten aufgeschluesselt, damit man sieht, wo es haengt (Warteschlange, Modell laden, Prompt lesen, Schreiben).
        lw::log::Info(
            "{} ({:.1f} s: {:.1f} Warten, {:.1f} Laden, {:.1f} Prompt mit {} Tokens, {} Tokens Antwort; {}): \"{}\" [{}]",
            s_Name, s_Result.m_Seconds, s_Result.m_WaitSeconds, s_Result.m_LoadSeconds, s_Result.m_PromptSeconds,
            s_Result.m_PromptTokens, s_Result.m_OutputTokens,
            s_Result.m_Kind == static_cast<int>(lw::DialogueTrigger::Initiate) ? "spricht an" :
            s_Result.m_Kind == static_cast<int>(lw::DialogueTrigger::Overheard) ? "mitgehoert" : "Antwort",
            s_Reply.m_Say, s_Reply.m_Emotion
        );

        if (!s_AlreadySpoken) {
            DeliverNpcReply(*s_Npc, s_Reply, s_Result.m_Kind, p_Settings, p_Now);
        }
    }
}

void LivingWorld::SpeakNpcLine(
    const lw::NpcSnapshot& p_Npc, const std::string& p_Text, const std::string& p_Emotion,
    const lw::Settings& p_Settings, double p_Now
) {
    lw::NpcMind& s_Mind = m_Book.Mind(p_Npc, m_World, p_Settings.m_Language);

    {
        std::scoped_lock s_Lock(m_UiMutex);
        Subtitle s_Subtitle;
        s_Subtitle.m_Speaker = s_Mind.m_DisplayName;
        s_Subtitle.m_Text = p_Text;
        s_Subtitle.m_Emotion = p_Emotion;
        s_Subtitle.m_NpcKey = p_Npc.m_Key;
        s_Subtitle.m_Created = RealTime();
        s_Subtitle.m_Duration = SubtitleDuration(p_Text, p_Settings);
        m_Subtitles.push_back(std::move(s_Subtitle));

        while (m_Subtitles.size() > 8) {
            m_Subtitles.pop_front();
        }
    }

    if (!p_Settings.m_TtsEnabled || !m_Speech.IsReady()) {
        return;
    }

    lw::SpeakRequest s_Request;
    s_Request.m_Text = p_Text;
    s_Request.m_Female = p_Npc.m_Female;
    s_Request.m_VoiceName = p_Npc.m_Female ? p_Settings.m_VoiceFemale : p_Settings.m_VoiceMale;

    // "Automatisch": jede Person bekommt fest eine der passenden Stimmen (Frauen z. B. abwechselnd Katja/Hedda).
    if (s_Request.m_VoiceName.empty()) {
        std::vector<std::string> s_Matching;

        for (const auto& s_Voice : m_Speech.Voices()) {
            const bool s_LanguageOk = IsGerman(p_Settings) ? s_Voice.IsGerman() : s_Voice.IsEnglish();

            if (s_LanguageOk && s_Voice.m_Female == p_Npc.m_Female && s_Voice.m_OneCore) {
                s_Matching.push_back(s_Voice.m_Name);
            }
        }

        if (!s_Matching.empty()) {
            s_Request.m_VoiceName = s_Matching[lw::StableHash(std::to_string(p_Npc.m_Key)) % s_Matching.size()];
        }
    }
    s_Request.m_German = IsGerman(p_Settings);
    s_Request.m_Tag = p_Npc.m_Key;
    s_Request.m_Generation = m_Generation;

    int s_Rate = p_Settings.m_TtsBaseRate + s_Mind.m_VoiceRate;
    int s_Pitch = s_Mind.m_VoicePitch;
    int s_Volume = 100;
    lw::EmotionToStyle(p_Emotion, s_Rate, s_Pitch, s_Volume);
    s_Request.m_Style = { s_Rate, s_Pitch, s_Volume };

    s_Request.m_Volume = p_Settings.m_TtsVolume;

    if (p_Settings.m_SpatialAudio) {
        const float s_Listener[3] = { m_World.m_CameraPos.x, m_World.m_CameraPos.y, m_World.m_CameraPos.z };
        const float s_Right[3] = { m_World.m_CameraRight.x, m_World.m_CameraRight.y, m_World.m_CameraRight.z };
        const float s_Source[3] = { p_Npc.m_Head.x, p_Npc.m_Head.y, p_Npc.m_Head.z };
        const lw::SpatialResult s_Spatial = lw::ComputeSpatial(s_Listener, s_Right, s_Source, p_Settings.m_AudioMaxDistance);
        s_Request.m_Volume *= s_Spatial.m_Volume;
        s_Request.m_Pan = s_Spatial.m_Pan;
    }

    m_Speech.Speak(std::move(s_Request));
}

void LivingWorld::Speak47(const std::string& p_Text, const lw::Settings& p_Settings) {
    if (!p_Settings.m_TtsEnabled || !m_Speech.IsReady()) {
        return;
    }

    lw::SpeakRequest s_Request;
    s_Request.m_Text = p_Text;
    s_Request.m_Female = false;
    s_Request.m_VoiceName = p_Settings.m_Voice47.empty() ? p_Settings.m_VoiceMale : p_Settings.m_Voice47;
    s_Request.m_German = IsGerman(p_Settings);
    s_Request.m_Style = { p_Settings.m_TtsBaseRate - 1, -7, 90 };
    s_Request.m_Tag = 1;
    s_Request.m_Generation = m_Generation;
    s_Request.m_Volume = p_Settings.m_TtsVolume * 0.85f;
    m_Speech.Speak(std::move(s_Request));
}

// ---------------------------------------------------------------------------------------------------------------------
// Deeskalation
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::UpdateStandoffs(const lw::Settings& p_Settings, double p_Now) {
    std::vector<uint64_t> s_New;

    {
        std::scoped_lock s_Lock(m_StandoffMutex);
        s_New.swap(m_NewStandoffs);
    }

    for (const uint64_t s_Key : s_New) {
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Key);

        if (!s_Npc) {
            continue;
        }

        const std::string s_Name = lw::DisplayName(*s_Npc);
        lw::log::Info("{} zielt auf 47 – Schuesse werden {:.0f} s zurueckgehalten.", s_Name, p_Settings.m_WarnSeconds);

        if (p_Settings.m_AutoWarnings) {
            lw::NpcReply s_Warning;
            s_Warning.m_Say = lw::WarningLine(s_Key ^ static_cast<uint64_t>(p_Now * 7.0), false, p_Settings.m_Language);
            s_Warning.m_Emotion = "angry";
            m_Book.Mind(*s_Npc, m_World, p_Settings.m_Language);
            m_Book.RecordNpcLine(s_Key, s_Name, s_Warning, HearersAround(s_Npc->m_Pos, p_Settings.m_RadiusNormal), p_Now);
            SpeakNpcLine(*s_Npc, s_Warning.m_Say, s_Warning.m_Emotion, p_Settings, p_Now);
        }

        if (p_Settings.m_ShowHints) {
            SetStatus(
                IsGerman(p_Settings)
                    ? s_Name + " zielt auf dich – rede mit [" + lw::KeyName(p_Settings.m_ChatKey) + "] oder halte [" + lw::KeyName(p_Settings.m_PushToTalkKey) + "]."
                    : s_Name + " is aiming at you – talk with [" + lw::KeyName(p_Settings.m_ChatKey) + "] or hold [" + lw::KeyName(p_Settings.m_PushToTalkKey) + "].",
                6.f
            );
        }
    }

    for (const uint64_t s_Key : m_Gate.TakeLastWarnings(p_Now)) {
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Key);

        if (!s_Npc || !p_Settings.m_AutoWarnings || m_Speech.IsBusyWith(s_Key)) {
            continue;
        }

        {
            std::scoped_lock s_Lock(m_UiMutex);

            if (m_Thinking.contains(s_Key)) {
                continue;
            }
        }

        SpeakNpcLine(*s_Npc, lw::WarningLine(s_Key + 17, true, p_Settings.m_Language), "angry", p_Settings, p_Now);
    }

    // Solange 47 redet (tippt, spricht, auf Antwort wartet), warten die Bewaffneten ab.
    const bool s_Talking = m_ChatOpen || m_Recording || m_Stt.IsBusy();

    for (const uint64_t s_Key : m_Gate.ActiveStandoffs(p_Now)) {
        bool s_Waiting = s_Talking;

        if (!s_Waiting) {
            std::scoped_lock s_Lock(m_UiMutex);
            s_Waiting = m_Thinking.contains(s_Key);
        }

        if (s_Waiting) {
            m_Gate.KeepNegotiating(s_Key, p_Now, 4.f);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// NPCs sprechen 47 von sich aus an
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::UpdateAmbient(const lw::Settings& p_Settings, double p_Now) {
    // Wer nicht mehr nah ist, faengt wieder bei null an.
    std::erase_if(m_NearSince, [this](const auto& p_Entry) {
        const auto* s_Npc = m_World.FindNpc(p_Entry.first);
        return !s_Npc || s_Npc->m_Distance > 3.5f;
    });

    if (!p_Settings.m_NpcsInitiate || m_ChatOpen || m_Recording || m_World.m_Ai.m_Tension >= 4) {
        return;
    }

    if (m_Llm.PendingCount() > 0 || p_Now - m_LastAmbient < 20.0) {
        return;
    }

    for (const auto& s_Npc : m_World.m_Npcs) {
        if (s_Npc.m_Distance > 2.4f) {
            break; // sortiert nach Abstand
        }

        if (!s_Npc.CanTalk() || lw::IsHostileBehavior(s_Npc.m_Behavior) || lw::IsScaredBehavior(s_Npc.m_Behavior)) {
            continue;
        }

        const double s_Since = m_NearSince.try_emplace(s_Npc.m_Key, p_Now).first->second;

        if (p_Now - s_Since < p_Settings.m_InitiateAfterSeconds || s_Npc.m_ViewAngle > 70.f) {
            continue;
        }

        lw::NpcMind& s_Mind = m_Book.Mind(s_Npc, m_World, p_Settings.m_Language);

        // Wer gerade Todesangst hat oder verletzt ist, plaudert nicht los.
        if (s_Mind.m_Fear > 60 || s_Mind.m_Wounded) {
            continue;
        }

        if (p_Now - s_Mind.m_LastInitiated < p_Settings.m_InitiateCooldown || p_Now - s_Mind.m_LastSpoke < 25.0) {
            continue;
        }

        s_Mind.m_LastInitiated = p_Now;
        m_LastAmbient = p_Now;
        SubmitDialogue(s_Npc, lw::DialogueTrigger::Initiate, "", lw::VoiceVolume::Normal, "", 2, p_Now);
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Audio, Zeitlupe, Schaden
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::UpdateAudio(const lw::Settings& p_Settings) {
    lw::SpeechStarted s_Started;

    while (m_Speech.TryPopStarted(s_Started)) {
        if (s_Started.m_Generation == m_Generation && s_Started.m_Handle != 0) {
            m_PlayingVoices[s_Started.m_Handle] = s_Started.m_Tag;
        }
    }

    const float s_Listener[3] = { m_World.m_CameraPos.x, m_World.m_CameraPos.y, m_World.m_CameraPos.z };
    const float s_Right[3] = { m_World.m_CameraRight.x, m_World.m_CameraRight.y, m_World.m_CameraRight.z };

    for (auto s_It = m_PlayingVoices.begin(); s_It != m_PlayingVoices.end();) {
        if (!m_Speech.Audio().IsPlaying(s_It->first)) {
            s_It = m_PlayingVoices.erase(s_It);
            continue;
        }

        // Stimme wandert mit Kopf und Kamera mit.
        if (p_Settings.m_SpatialAudio && s_It->second > 1) {
            if (const auto* s_Npc = m_World.FindNpc(s_It->second)) {
                const float s_Source[3] = { s_Npc->m_Head.x, s_Npc->m_Head.y, s_Npc->m_Head.z };
                const lw::SpatialResult s_Spatial = lw::ComputeSpatial(
                    s_Listener, s_Right, s_Source, p_Settings.m_AudioMaxDistance
                );
                m_Speech.Audio().SetVolumePan(s_It->first, s_Spatial.m_Volume * p_Settings.m_TtsVolume, s_Spatial.m_Pan);
            }
        }

        ++s_It;
    }
}

void LivingWorld::UpdateTimeScale(const lw::Settings& p_Settings) {
    auto* s_TimeManager = Globals::GameTimeManager;

    if (!s_TimeManager) {
        return;
    }

    const bool s_Want = m_ChatOpen && p_Settings.m_Enabled && p_Settings.m_SlowTimeWhileTyping && m_World.m_InLevel;

    if (s_Want && !m_TimeScaleApplied) {
        m_PreviousTimeScale = s_TimeManager->m_fGameTimeMultiplier > 0.01f ? s_TimeManager->m_fGameTimeMultiplier : 1.f;
        s_TimeManager->m_fGameTimeMultiplier = std::min(m_PreviousTimeScale, p_Settings.m_TypingTimeScale);
        m_TimeScaleApplied = true;
    }
    else if (!s_Want && m_TimeScaleApplied) {
        s_TimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
        m_TimeScaleApplied = false;
    }
}

void LivingWorld::UpdatePlayerDamage(const lw::Settings& p_Settings) {
    const float s_Multiplier = p_Settings.m_PlayerDamageMultiplier;

    if (std::abs(s_Multiplier - 1.f) < 0.01f) {
        m_LastPlayerHp = -1.f;
        return;
    }

    const auto s_Player = SDK()->GetLocalPlayer();

    if (!s_Player || !s_Player.m_pInterfaceRef || !s_Player.m_pInterfaceRef->m_pHealth) {
        return;
    }

    ZHM5Health* s_Health = s_Player.m_pInterfaceRef->m_pHealth;
    float s_Hp = s_Health->m_fHitPoints.Get();
    const float s_MaxHp = s_Health->m_fMaxHitPoints.Get();

    if (m_LastPlayerHp > 0.f && s_Hp > 0.f && s_Hp < m_LastPlayerHp - 0.001f) {
        const float s_Lost = m_LastPlayerHp - s_Hp;
        float s_Adjusted = s_Hp - s_Lost * (s_Multiplier - 1.f);

        // Nie direkt toeten – der naechste Treffer erledigt das schon (das Spiel soll den Tod selbst ausloesen).
        s_Adjusted = std::clamp(s_Adjusted, std::min(1.f, s_Hp), s_MaxHp > 0.f ? s_MaxHp : s_Hp);

        if (std::abs(s_Adjusted - s_Hp) > 0.001f) {
            s_Health->m_fHitPoints.Set(s_Adjusted);
            s_Hp = s_Adjusted;
        }
    }

    m_LastPlayerHp = s_Hp;
}
