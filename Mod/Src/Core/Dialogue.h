#pragma once

#include "FireGate.h"
#include "LlmClient.h"
#include "Settings.h"
#include "World.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lw {
    enum class DialogueTrigger {
        PlayerSpoke = 0, // 47 spricht den NPC direkt an
        Overheard = 1,   // NPC hat mitgehoert und reagiert evtl.
        Initiate = 2,    // NPC spricht 47 von sich aus an
    };

    struct DialogueLine {
        std::string m_Speaker;      // Anzeigename
        std::string m_Text;
        bool m_FromPlayer = false;
        bool m_Own = false;         // vom NPC selbst gesagt
        bool m_Overheard = false;   // nicht an diesen NPC gerichtet
        std::string m_AddressedTo;  // bei mitgehoerten Saetzen: an wen
        VoiceVolume m_Volume = VoiceVolume::Normal;
        std::string m_Emotion;
        double m_Time = 0.0;
    };

    struct NpcMind {
        uint64_t m_Key = 0;
        std::string m_DisplayName;
        std::string m_Personality;
        std::deque<DialogueLine> m_Lines;
        std::deque<std::string> m_Experiences; // Erlebtes (Schuesse, Messerstiche ...) – fliesst in jede Antwort ein
        bool m_Wounded = false;
        std::string m_Condition; // aktueller koerperlicher Zustand (Verwundung) fuer den Prompt
        double m_LastReaction = -1e9;
        int m_Trust = 40;
        int m_Suspicion = 10;
        int m_Fear = 0;
        int m_VoicePitch = 0;
        int m_VoiceRate = 0;
        double m_LastSpoke = -1e9;
        double m_LastInitiated = -1e9;
        int m_Exchanges = 0;
        bool m_Initialized = false;
    };

    struct NpcReply {
        bool m_Valid = false;
        std::string m_Say;
        std::string m_Emotion = "neutral";
        NpcAction m_Action = NpcAction::None;
        int m_Trust = 0;
        int m_Suspicion = 0;
        int m_Fear = 0;
    };

    struct PromptContext {
        const Settings* m_Settings = nullptr;
        const WorldSnapshot* m_World = nullptr;
        const NpcSnapshot* m_Npc = nullptr;
        DialogueTrigger m_Trigger = DialogueTrigger::PlayerSpoke;
        std::string m_PlayerText;
        VoiceVolume m_Volume = VoiceVolume::Normal;
        std::string m_AddressedTo; // bei Overheard
        GateState m_Gate = GateState::Idle;
        float m_GateSecondsLeft = 0.f;
        bool m_Threatened = false;  // 47 droht gerade mit dem Tod / sagt, er sei bewaffnet
        bool m_Disarmed = false;    // dem NPC wurde die Waffe aus der Hand geschossen/geschlagen
        int m_ArmedKills = 0;       // Ruf: getoetete Bewaffnete in dieser Mission
        int m_CivilianKills = 0;
    };

    // Gedaechtnis aller NPCs der laufenden Mission.
    class DialogueBook {
    public:
        NpcMind& Mind(const NpcSnapshot& p_Npc, const WorldSnapshot& p_World, Language p_Language);
        NpcMind* Find(uint64_t p_Key);
        void Clear();

        // 47 hat etwas gesagt: Angesprochener + alle Mithoerer merken es sich.
        void RecordPlayerLine(
            const std::string& p_Text, VoiceVolume p_Volume, uint64_t p_Addressee, const std::string& p_AddresseeName,
            const std::vector<uint64_t>& p_Hearers, double p_Time
        );

        // Ein NPC hat etwas gesagt: er selbst und die Mithoerer merken es sich.
        void RecordNpcLine(
            uint64_t p_Speaker, const std::string& p_SpeakerName, const NpcReply& p_Reply,
            const std::vector<uint64_t>& p_Hearers, double p_Time
        );

        void ApplyAttitude(uint64_t p_Key, const NpcReply& p_Reply);

        // Alle bekannten NPCs (z. B. wenn sich herumspricht, dass 47 jemanden getoetet hat).
        template <typename Fn>
        void ForEachMind(Fn&& p_Fn) {
            for (auto& [s_Key, s_Mind] : m_Minds) {
                p_Fn(s_Mind);
            }
        }

        // Erlebnis merken (doppelte direkt hintereinander werden zusammengefasst) und Haltung aendern.
        void AddExperience(uint64_t p_Key, const std::string& p_Text, int p_Trust, int p_Suspicion, int p_Fear);

        std::vector<ChatMessage> BuildMessages(const PromptContext& p_Context);

        // Fuer die Gespraechsansicht.
        struct LogEntry {
            double m_Time;
            std::string m_Speaker;
            std::string m_Text;
            bool m_FromPlayer;
            std::string m_Emotion;
        };

        std::vector<LogEntry> GlobalLog() const;
        void AddToGlobalLog(const LogEntry& p_Entry);

        std::mutex& Mutex() { return m_Mutex; }

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<uint64_t, NpcMind> m_Minds;
        std::deque<LogEntry> m_Log;
    };

    // Wer hoert 47, und wen spricht er an? (Name im Satz > letzter Gespraechspartner > wer vor ihm steht.)
    struct AddresseeChoice {
        uint64_t m_Key = 0;
        std::vector<uint64_t> m_Hearers; // alle in Hoerweite, inkl. Angesprochenem
    };

    AddresseeChoice ChooseAddressee(
        const WorldSnapshot& p_World, const std::string& p_Text, float p_Radius, uint64_t p_LastAddressee,
        double p_LastAddressTime, double p_Now
    );

    // Hilfen (oeffentlich fuer Tests).
    nlohmann::ordered_json ReplySchema();

    // Aus einer noch unvollstaendigen (gestreamten) Antwort: sind "say" und "emotion" schon komplett?
    bool ExtractEarlyReply(const std::string& p_Partial, std::string& p_Say, std::string& p_Emotion);
    NpcReply ParseReply(const std::string& p_Content);    // inkl. Entfernen von Chatbot-Floskeln
    NpcReply ParseReplyRaw(const std::string& p_Content); // nur JSON auswerten
    std::string StripAssistantPhrases(const std::string& p_Text);
    std::string SystemPrompt(Language p_Language);
    std::string DisplayName(const NpcSnapshot& p_Npc);
    std::string DescribeAppearance(const NpcSnapshot& p_Npc, const WorldSnapshot& p_World, Language p_Language);
    std::string PersonalityFor(const NpcSnapshot& p_Npc, Language p_Language);

    // Feste Saetze fuer Sofort-Reaktionen (ohne auf die KI zu warten).
    std::string WarningLine(uint64_t p_Seed, bool p_Last, Language p_Language);
    std::string ActionLabel(NpcAction p_Action, Language p_Language);
    NpcAction ParseAction(const std::string& p_Text);

    // Stimme: Hoehe/Tempo je nach Emotion anpassen.
    void EmotionToStyle(const std::string& p_Emotion, int& p_Rate, int& p_Pitch, int& p_Volume);
}
