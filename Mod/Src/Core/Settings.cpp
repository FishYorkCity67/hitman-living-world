#include "Settings.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace lw {
    namespace {
        // Ein Eintrag pro Einstellung; Laden und Speichern laufen ueber dieselbe Tabelle.
        struct Binder {
            const SettingsStore& m_Store;
            bool m_Saving;

            void Bool(const char* p_Section, const char* p_Key, bool& p_Value) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, p_Value ? "true" : "false");
                    return;
                }

                const std::string s_Raw = m_Store.m_Get(p_Section, p_Key, p_Value ? "true" : "false");
                p_Value = (s_Raw == "true" || s_Raw == "1" || s_Raw == "yes");
            }

            void Int(const char* p_Section, const char* p_Key, int& p_Value, int p_Min, int p_Max) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, std::to_string(p_Value));
                    return;
                }

                const std::string s_Raw = m_Store.m_Get(p_Section, p_Key, std::to_string(p_Value));
                int s_Parsed = p_Value;

                if (std::from_chars(s_Raw.data(), s_Raw.data() + s_Raw.size(), s_Parsed).ec == std::errc()) {
                    p_Value = std::clamp(s_Parsed, p_Min, p_Max);
                }
            }

            void Float(const char* p_Section, const char* p_Key, float& p_Value, float p_Min, float p_Max) const {
                if (m_Saving) {
                    char s_Buffer[32];
                    snprintf(s_Buffer, sizeof(s_Buffer), "%.4f", p_Value);
                    m_Store.m_Set(p_Section, p_Key, s_Buffer);
                    return;
                }

                char s_Default[32];
                snprintf(s_Default, sizeof(s_Default), "%.4f", p_Value);
                const std::string s_Raw = m_Store.m_Get(p_Section, p_Key, s_Default);

                // strtof haengt am Gebietsschema; from_chars nicht.
                float s_Parsed = p_Value;

                if (std::from_chars(s_Raw.data(), s_Raw.data() + s_Raw.size(), s_Parsed).ec == std::errc()) {
                    p_Value = std::clamp(s_Parsed, p_Min, p_Max);
                }
            }

            void String(const char* p_Section, const char* p_Key, std::string& p_Value) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, p_Value);
                    return;
                }

                p_Value = m_Store.m_Get(p_Section, p_Key, p_Value);
            }

            template <typename E>
            void Enum(const char* p_Section, const char* p_Key, E& p_Value, int p_Max) const {
                int s_Raw = static_cast<int>(p_Value);
                Int(p_Section, p_Key, s_Raw, 0, p_Max);
                p_Value = static_cast<E>(s_Raw);
            }
        };

        void Bind(Settings& s, const Binder& b) {
            b.Bool("general", "enabled", s.m_Enabled);
            b.Enum("general", "language", s.m_Language, 1);
            b.Int("general", "chat_key", s.m_ChatKey, 1, 254);
            b.Int("general", "ptt_key", s.m_PushToTalkKey, 1, 254);
            b.Int("general", "menu_key", s.m_MenuKey, 1, 254);
            b.Bool("general", "slow_time_typing", s.m_SlowTimeWhileTyping);
            b.Float("general", "typing_time_scale", s.m_TypingTimeScale, 0.05f, 1.f);
            b.Bool("general", "subtitles", s.m_ShowSubtitles);
            b.Bool("general", "bubbles", s.m_ShowBubbles);
            b.Bool("general", "hints", s.m_ShowHints);
            b.Float("general", "subtitle_seconds", s.m_SubtitleSeconds, 2.f, 30.f);
            b.Float("general", "ui_scale", s.m_UiScale, 0.6f, 2.f);

            b.Enum("llm", "provider", s.m_LlmProvider, 1);
            b.String("llm", "url", s.m_LlmUrl);
            b.String("llm", "model", s.m_LlmModel);
            b.String("llm", "api_key", s.m_LlmApiKey);
            b.Float("llm", "temperature", s.m_Temperature, 0.f, 2.f);
            b.Int("llm", "max_tokens", s.m_MaxTokens, 40, 1024);
            b.Int("llm", "context_size", s.m_ContextSize, 1024, 32768);
            b.Int("llm", "gpu_layers", s.m_GpuLayers, -1, 999);
            b.Int("llm", "threads", s.m_LlmThreads, 0, 64);
            b.Int("llm", "keep_alive_minutes", s.m_KeepAliveMinutes, 0, 1440);
            b.Bool("llm", "autostart_ollama", s.m_AutoStartOllama);
            b.Int("llm", "history_turns", s.m_HistoryTurns, 0, 30);
            b.Int("llm", "timeout_seconds", s.m_TimeoutSeconds, 5, 300);

            b.Bool("tts", "enabled", s.m_TtsEnabled);
            b.Bool("tts", "mute_vanilla", s.m_MuteVanillaVoices);
            b.Float("tts", "volume", s.m_TtsVolume, 0.f, 2.f);
            b.Int("tts", "base_rate", s.m_TtsBaseRate, -10, 10);
            b.String("tts", "voice_male", s.m_VoiceMale);
            b.String("tts", "voice_female", s.m_VoiceFemale);
            b.Bool("tts", "speak_47", s.m_Tts47);
            b.String("tts", "voice_47", s.m_Voice47);
            b.Bool("tts", "spatial", s.m_SpatialAudio);
            b.Float("tts", "max_distance", s.m_AudioMaxDistance, 5.f, 100.f);

            b.Bool("stt", "enabled", s.m_SttEnabled);
            b.String("stt", "model", s.m_WhisperModel);
            b.Int("stt", "mic_device", s.m_MicDevice, -1, 64);
            b.Int("stt", "threads", s.m_SttThreads, 1, 32);
            b.Bool("stt", "mic_sets_volume", s.m_MicSetsVolume);
            b.Float("stt", "whisper_db", s.m_WhisperThresholdDb, -80.f, 0.f);
            b.Float("stt", "shout_db", s.m_ShoutThresholdDb, -80.f, 0.f);

            b.Float("hearing", "radius_whisper", s.m_RadiusWhisper, 0.5f, 10.f);
            b.Float("hearing", "radius_normal", s.m_RadiusNormal, 1.f, 30.f);
            b.Float("hearing", "radius_shout", s.m_RadiusShout, 2.f, 80.f);
            b.Bool("hearing", "bystanders_react", s.m_BystandersReact);
            b.Int("hearing", "max_bystander_replies", s.m_MaxBystanderReplies, 0, 5);
            b.Bool("hearing", "npcs_initiate", s.m_NpcsInitiate);
            b.Float("hearing", "initiate_after", s.m_InitiateAfterSeconds, 1.f, 60.f);
            b.Float("hearing", "initiate_cooldown", s.m_InitiateCooldown, 10.f, 900.f);
            b.Bool("hearing", "npcs_know_targets", s.m_NpcsKnowTargets);

            b.Bool("realism", "deescalation", s.m_Deescalation);
            b.Float("realism", "warn_seconds", s.m_WarnSeconds, 0.f, 60.f);
            b.Float("realism", "negotiation_max", s.m_NegotiationMaxSeconds, 5.f, 300.f);
            b.Float("realism", "hold_fire_extend", s.m_HoldFireExtend, 1.f, 120.f);
            b.Float("realism", "stand_down_seconds", s.m_StandDownSeconds, 10.f, 1800.f);
            b.Float("realism", "aggression_memory", s.m_AggressionMemorySeconds, 0.f, 300.f);
            b.Bool("realism", "group_deescalation", s.m_GroupDeescalation);
            b.Bool("realism", "auto_warnings", s.m_AutoWarnings);
            b.Float("realism", "player_damage_multiplier", s.m_PlayerDamageMultiplier, 0.25f, 5.f);
            b.Bool("realism", "violence_reactions", s.m_ViolenceReactions);
            b.Int("realism", "max_reaction_voices", s.m_MaxReactionVoices, 0, 6);
            b.Bool("realism", "injuries", s.m_Injuries);
            b.Bool("realism", "realistic_hit_zones", s.m_RealisticHitZones);
            b.Bool("realism", "bleed_out", s.m_BleedOut);
            b.Float("realism", "bleed_seconds_min", s.m_BleedSecondsMin, 10.f, 600.f);
            b.Float("realism", "bleed_seconds_max", s.m_BleedSecondsMax, 10.f, 900.f);
            b.Float("realism", "limp_speed", s.m_LimpSpeed, 0.3f, 3.f);
            b.Bool("realism", "stab_wounds", s.m_StabWounds);
            b.Bool("realism", "disarm", s.m_Disarm);
            b.Int("realism", "disarm_chance", s.m_DisarmChance, 0, 100);
            b.Bool("realism", "threat_reactions", s.m_ThreatReactions);
            b.Bool("realism", "reputation", s.m_Reputation);
            b.Bool("realism", "surrender_freeze", s.m_SurrenderFreeze);
            b.Float("realism", "surrender_seconds", s.m_SurrenderSeconds, 5.f, 180.f);
            b.Bool("llm", "fast_replies", s.m_FastReplies);

            b.Bool("debug", "overlay", s.m_DebugOverlay);
            b.Bool("debug", "log", s.m_DebugLog);
            b.Bool("debug", "log_prompts", s.m_LogPrompts);
        }
    }

    void LoadSettings(Settings& p_Settings, const SettingsStore& p_Store) {
        Bind(p_Settings, Binder { p_Store, false });
    }

    void SaveSettings(const Settings& p_Settings, const SettingsStore& p_Store) {
        Bind(const_cast<Settings&>(p_Settings), Binder { p_Store, true });
    }

    std::string KeyName(int p_VirtualKey) {
        switch (p_VirtualKey) {
            case VK_LBUTTON: return "Linke Maustaste";
            case VK_RBUTTON: return "Rechte Maustaste";
            case VK_MBUTTON: return "Mittlere Maustaste";
            case VK_XBUTTON1: return "Maus 4";
            case VK_XBUTTON2: return "Maus 5";
            case VK_SPACE: return "Leertaste";
            case VK_TAB: return "Tab";
            case VK_CAPITAL: return "Feststelltaste";
            case VK_LSHIFT: return "Shift links";
            case VK_RSHIFT: return "Shift rechts";
            case VK_LCONTROL: return "Strg links";
            case VK_RCONTROL: return "Strg rechts";
            case VK_LMENU: return "Alt links";
            case VK_RMENU: return "Alt Gr";
            case VK_RETURN: return "Enter";
            case VK_BACK: return "Ruecktaste";
            case VK_INSERT: return "Einfg";
            case VK_DELETE: return "Entf";
            case VK_HOME: return "Pos1";
            case VK_END: return "Ende";
            case VK_PRIOR: return "Bild auf";
            case VK_NEXT: return "Bild ab";
            default: break;
        }

        if (p_VirtualKey >= VK_F1 && p_VirtualKey <= VK_F24) {
            return "F" + std::to_string(p_VirtualKey - VK_F1 + 1);
        }

        if (p_VirtualKey >= VK_NUMPAD0 && p_VirtualKey <= VK_NUMPAD9) {
            return "Num " + std::to_string(p_VirtualKey - VK_NUMPAD0);
        }

        if ((p_VirtualKey >= '0' && p_VirtualKey <= '9') || (p_VirtualKey >= 'A' && p_VirtualKey <= 'Z')) {
            return std::string(1, static_cast<char>(p_VirtualKey));
        }

        // Fuer alles andere den Tastaturtreiber fragen (beruecksichtigt das Layout, z. B. "Ü").
        const UINT s_ScanCode = MapVirtualKeyW(static_cast<UINT>(p_VirtualKey), MAPVK_VK_TO_VSC);
        wchar_t s_Name[64] = {};

        if (s_ScanCode != 0 && GetKeyNameTextW(static_cast<LONG>(s_ScanCode << 16), s_Name, 64) > 0) {
            char s_Utf8[192] = {};
            WideCharToMultiByte(CP_UTF8, 0, s_Name, -1, s_Utf8, sizeof(s_Utf8), nullptr, nullptr);
            return s_Utf8;
        }

        return "Taste " + std::to_string(p_VirtualKey);
    }
}
