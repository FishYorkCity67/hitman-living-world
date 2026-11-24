#pragma once

#include "Settings.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace lw {
    enum class VoiceVolume { Whisper = 0, Normal = 1, Shout = 2 };

    struct Vec3 {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    inline float Distance(const Vec3& p_A, const Vec3& p_B) {
        const float s_Dx = p_A.x - p_B.x;
        const float s_Dy = p_A.y - p_B.y;
        const float s_Dz = p_A.z - p_B.z;
        return std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy + s_Dz * s_Dz);
    }

    // Was die Mod pro Bild ueber einen NPC weiss (vom Spiel-Thread befuellt, danach nur gelesen).
    struct NpcSnapshot {
        uint64_t m_Key = 0; // eindeutig innerhalb der aktuellen Mission
        int m_Index = -1;
        void* m_Actor = nullptr;  // ZActor* (nur auf dem Spiel-Thread benutzen)
        int m_OriginalVoice = -1; // urspruengliche Stimmvariante des Spiels (EActorVoiceVariation)
        std::string m_Name;
        std::string m_Outfit;
        std::string m_OutfitTitle;
        bool m_Female = false;
        bool m_Guard = false;
        bool m_Target = false;
        bool m_Named = false;
        bool m_Authority = false;
        bool m_WeaponsAllowed = false;
        bool m_WeaponOut = false;
        bool m_Alive = true;
        bool m_Pacified = false;
        bool m_Enforcer = false;
        bool m_Crowd = false;
        int m_Rank = 0;
        int m_Behavior = -1; // ECompiledBehaviorType
        float m_Health = 100.f;
        Vec3 m_Pos;
        Vec3 m_Head;
        float m_Distance = 0.f;  // zu 47
        float m_ViewAngle = 180.f; // Grad zwischen Blickrichtung der Kamera und NPC

        bool CanTalk() const { return m_Alive && !m_Pacified; }
        bool IsArmed() const { return m_Guard || m_WeaponOut; }
    };

    struct PlayerSnapshot {
        bool m_Valid = false;
        Vec3 m_Pos;
        std::string m_Outfit;
        std::string m_OutfitTitle;
        bool m_InSuit = true;
        bool m_OutfitIsGuard = false;
    };

    struct AiSnapshot {
        int m_Tension = 1; // EGameTension
        bool m_Trespassing = false;
        bool m_InDisguise = false;
        bool m_DisguiseSuspicious = false;
        bool m_DisguiseBroken = false;
        bool m_IllegalWeapon = false;
        bool m_BodyFound = false;
        bool m_BeingFrisked = false;
        int m_EnemiesAlerted = 0;
        int m_EnemiesInCombat = 0;
    };

    struct TargetSnapshot {
        uint64_t m_Key = 0;
        std::string m_Name;
        Vec3 m_Pos;
        bool m_Alive = true;
    };

    struct WorldSnapshot {
        bool m_InLevel = false;
        double m_GameTime = 0.0;
        int m_ActorCount = 0; // alle aktiven Akteure der Mission (Diagnose)
        std::string m_LocationId;
        std::string m_Scene;
        std::string m_LocationName;
        PlayerSnapshot m_Player;
        AiSnapshot m_Ai;
        std::vector<NpcSnapshot> m_Npcs;
        std::vector<TargetSnapshot> m_Targets;
        Vec3 m_CameraPos;
        Vec3 m_CameraForward;
        Vec3 m_CameraRight;

        const NpcSnapshot* FindNpc(uint64_t p_Key) const {
            for (const auto& s_Npc : m_Npcs) {
                if (s_Npc.m_Key == p_Key) {
                    return &s_Npc;
                }
            }

            return nullptr;
        }
    };

    // Beschreibungen fuer Prompts und Anzeige.
    std::string DescribeBehavior(int p_Behavior, Language p_Language);
    std::string BehaviorName(int p_Behavior);
    bool IsHostileBehavior(int p_Behavior);
    bool IsScaredBehavior(int p_Behavior);
    bool IsSentryBehavior(int p_Behavior);
    std::string DescribeTension(int p_Tension, Language p_Language);
    std::string FriendlyLocation(const std::string& p_LocationId, const std::string& p_Scene, Language p_Language);

    // Himmelsrichtung von A nach B (Glacier: +X = Osten, +Y = Norden, +Z = oben).
    std::string DescribeDirection(const Vec3& p_From, const Vec3& p_To, Language p_Language);

    // Wie gut erkennt man einen NPC-Namen als "echten" Namen (statt interner Bezeichnung)?
    bool LooksLikePersonName(const std::string& p_Name);
}
