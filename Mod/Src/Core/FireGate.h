#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace lw {
    enum class NpcAction { None = 0, HoldFire, OpenFire, StandDown };

    struct FireGateConfig {
        bool m_Enabled = true;
        float m_WarnSeconds = 7.f;
        float m_NegotiationMaxSeconds = 50.f;
        float m_HoldFireExtend = 14.f;
        float m_StandDownSeconds = 180.f;
        float m_AggressionMemorySeconds = 25.f;
        bool m_Group = true;
    };

    struct GateDecision {
        bool m_Block = false;
        bool m_FirstAttempt = false; // erster abgefangener Schuss dieser Begegnung -> Warnung aussprechen
    };

    enum class GateState { Idle, Warning, Firing, Persuaded };

    // "Erst reden, dann schiessen": haelt die Schuesse bewaffneter NPCs zurueck, solange verhandelt wird.
    // Alle Zeiten in Spielsekunden. Threadsicher (der Schuss-Hook kann aus einem Job-Thread kommen).
    class FireGate {
    public:
        void Configure(const FireGateConfig& p_Config);
        FireGateConfig Config() const;
        void Reset();

        // 47 hat geschossen oder jemanden angegriffen.
        void OnPlayerAggression(double p_Now);
        bool RecentAggression(double p_Now) const;

        // Wird bei jedem Schussversuch eines NPCs gefragt.
        GateDecision OnShotAttempt(uint64_t p_Npc, double p_Now);

        // Verhandlung laeuft (Chat offen, Antwort in Arbeit): Frist mindestens bis jetzt + p_Seconds.
        void KeepNegotiating(uint64_t p_Npc, double p_Now, float p_Seconds);

        // Ergebnis eines Gespraechs anwenden.
        void Apply(uint64_t p_Npc, NpcAction p_Action, double p_Now);

        GateState State(uint64_t p_Npc, double p_Now) const;
        float SecondsLeft(uint64_t p_Npc, double p_Now) const;
        std::vector<uint64_t> ActiveStandoffs(double p_Now) const;

        // NPCs, deren Frist gleich ablaeuft (fuer "Letzte Warnung!"). Jeder NPC nur einmal pro Begegnung.
        std::vector<uint64_t> TakeLastWarnings(double p_Now);

    private:
        struct Npc {
            double m_FirstAttempt = -1.0;
            double m_WindowEnd = 0.0;
            double m_LastAttempt = -1.0;
            double m_PersuadedUntil = 0.0;
            bool m_OpenFire = false;
            bool m_LastWarned = false;
            int m_Blocked = 0;
        };

        bool InWindow(const Npc& p_Npc, double p_Now) const;
        void ExtendLocked(Npc& p_Npc, double p_Now, float p_Seconds);

        mutable std::mutex m_Mutex;
        FireGateConfig m_Config;
        std::unordered_map<uint64_t, Npc> m_Npcs;
        double m_LastAggression = -1e9;
    };
}
