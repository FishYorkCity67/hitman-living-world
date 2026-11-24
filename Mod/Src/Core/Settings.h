#pragma once

#include <functional>
#include <string>

namespace lw {
    enum class Language { German = 0, English = 1 };
    enum class LlmProvider { Ollama = 0, OpenAiCompatible = 1 };

    // Alle Einstellungen der Mod. Standardwerte sind auf "funktioniert ohne Zutun" ausgelegt.
    struct Settings {
        // --- Allgemein ---------------------------------------------------------------------------------------------
        bool m_Enabled = true;
        Language m_Language = Language::German;
        int m_ChatKey = 'T';
        int m_PushToTalkKey = 'B';
        int m_MenuKey = 0x77; // VK_F8
        bool m_SlowTimeWhileTyping = true;
        float m_TypingTimeScale = 0.35f;
        bool m_ShowSubtitles = true;
        bool m_ShowBubbles = true;
        bool m_ShowHints = true;
        float m_SubtitleSeconds = 8.f;
        float m_UiScale = 1.f;

        // --- KI-Modell ---------------------------------------------------------------------------------------------
        LlmProvider m_LlmProvider = LlmProvider::Ollama;
        std::string m_LlmUrl = "http://127.0.0.1:11434";
        std::string m_LlmModel = "qwen3:1.7b"; // klein genug fuer neben HITMAN, hält sich gut an die Rolle
        std::string m_LlmApiKey;
        float m_Temperature = 0.8f;
        int m_MaxTokens = 100;
        int m_ContextSize = 3072;
        int m_GpuLayers = -1; // -1 = automatisch, 0 = nur CPU
        int m_LlmThreads = 0; // 0 = automatisch (Ollama: num_thread)
        int m_KeepAliveMinutes = 30;
        bool m_AutoStartOllama = true;
        int m_HistoryTurns = 4;
        int m_TimeoutSeconds = 60;

        // --- Sprachausgabe -----------------------------------------------------------------------------------------
        bool m_TtsEnabled = true;
        bool m_MuteVanillaVoices = true; // Original-Saetze der NPCs stummschalten (experimentell)
        float m_TtsVolume = 1.f;
        int m_TtsBaseRate = 1; // SAPI -10..10
        std::string m_VoiceMale;   // leer = automatisch
        std::string m_VoiceFemale; // leer = automatisch
        bool m_Tts47 = false;
        std::string m_Voice47;
        bool m_SpatialAudio = true;
        float m_AudioMaxDistance = 30.f;

        // --- Spracheingabe -----------------------------------------------------------------------------------------
        bool m_SttEnabled = true;
        std::string m_WhisperModel = "ggml-small-q5_1.bin";
        int m_MicDevice = -1; // -1 = Windows-Standard
        int m_SttThreads = 4;
        bool m_MicSetsVolume = true;
        float m_WhisperThresholdDb = -38.f;
        float m_ShoutThresholdDb = -14.f;

        // --- Hoeren & Reden ----------------------------------------------------------------------------------------
        float m_RadiusWhisper = 2.5f;
        float m_RadiusNormal = 7.f;
        float m_RadiusShout = 18.f;
        bool m_BystandersReact = true;
        int m_MaxBystanderReplies = 1;
        bool m_NpcsInitiate = true;
        float m_InitiateAfterSeconds = 5.f;
        float m_InitiateCooldown = 90.f;
        bool m_NpcsKnowTargets = true;

        // --- Deeskalation & Realismus ------------------------------------------------------------------------------
        bool m_Deescalation = true;
        float m_WarnSeconds = 7.f;
        float m_NegotiationMaxSeconds = 50.f;
        float m_HoldFireExtend = 14.f;
        float m_StandDownSeconds = 180.f;
        float m_AggressionMemorySeconds = 25.f;
        bool m_GroupDeescalation = true;
        bool m_AutoWarnings = true;
        float m_PlayerDamageMultiplier = 1.f;
        bool m_ViolenceReactions = true;   // Getroffene und Zeugen reagieren auf Schuesse/Messerstiche
        int m_MaxReactionVoices = 2;       // wie viele Zeugen gleichzeitig hoerbar reagieren

        // Verwundungen (experimentell)
        bool m_Injuries = true;            // Beinschuss = humpeln, Bauch/Stich = am Boden kriechen
        bool m_RealisticHitZones = true;   // Beine/Arme toeten nicht sofort (Schadenswerte der Outfits)
        bool m_BleedOut = true;            // Schwer Verletzte verbluten nach einer Weile
        float m_BleedSecondsMin = 45.f;
        float m_BleedSecondsMax = 120.f;
        float m_LimpSpeed = 0.9f;          // m/s mit angeschossenem Bein
        bool m_StabWounds = true;          // Messerstiche toeten nicht sofort: zu Boden, kriechen, verbluten
        bool m_Disarm = true;              // Waffe aus der Hand schiessen (Arm) oder schlagen (Nahkampf)
        int m_DisarmChance = 70;           // % pro Arm-Treffer / Schlag
        bool m_ThreatReactions = true;     // Drohungen loesen Panik und Betteln aus
        bool m_Reputation = true;          // getoetete Bewaffnete = mehr Respekt/Angst
        bool m_SurrenderFreeze = true;     // Eingeschuechterte/Entwaffnete laufen nicht weg (Spiel-KI wird gebremst)
        float m_SurrenderSeconds = 30.f;
        bool m_FastReplies = true;         // Antwort streamen und schon sprechen, bevor die KI ganz fertig ist

        // --- Diagnose ----------------------------------------------------------------------------------------------
        bool m_DebugOverlay = false;
        bool m_DebugLog = false;
        bool m_LogPrompts = false;
    };

    // Schnittstelle zum Speichern; die Mod haengt hier die INI-Funktionen des SDK an, die Tests eine Map.
    struct SettingsStore {
        std::function<std::string(const std::string& p_Section, const std::string& p_Key, const std::string& p_Default)> m_Get;
        std::function<void(const std::string& p_Section, const std::string& p_Key, const std::string& p_Value)> m_Set;
    };

    void LoadSettings(Settings& p_Settings, const SettingsStore& p_Store);
    void SaveSettings(const Settings& p_Settings, const SettingsStore& p_Store);

    // Anzeigename einer Taste (virtueller Tastencode), z. B. "T", "F8", "Maus 4".
    std::string KeyName(int p_VirtualKey);
}
