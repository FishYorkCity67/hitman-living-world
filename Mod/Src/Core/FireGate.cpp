#include "FireGate.h"

#include <algorithm>

namespace lw {
    namespace {
        // Ohne Schussversuch so lange -> die Begegnung gilt als beendet, die naechste startet frisch.
        constexpr double c_EncounterTimeout = 30.0;
    }

    void FireGate::Configure(const FireGateConfig& p_Config) {
        std::scoped_lock s_Lock(m_Mutex);
        m_Config = p_Config;
    }

    FireGateConfig FireGate::Config() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_Config;
    }

    void FireGate::Reset() {
        std::scoped_lock s_Lock(m_Mutex);
        m_Npcs.clear();
        m_LastAggression = -1e9;
    }

    void FireGate::OnPlayerAggression(double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        m_LastAggression = p_Now;

        // Wer gerade mit 47 im Clinch ist, darf jetzt sofort zurueckschiessen; Ueberredete vergessen ihr Versprechen.
        for (auto& [s_Key, s_Npc] : m_Npcs) {
            s_Npc.m_PersuadedUntil = 0.0;

            if (s_Npc.m_LastAttempt >= 0.0 && p_Now - s_Npc.m_LastAttempt < c_EncounterTimeout) {
                s_Npc.m_OpenFire = true;
            }
        }
    }

    bool FireGate::RecentAggression(double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        return p_Now - m_LastAggression < m_Config.m_AggressionMemorySeconds;
    }

    bool FireGate::InWindow(const Npc& p_Npc, double p_Now) const {
        return p_Npc.m_FirstAttempt >= 0.0 && !p_Npc.m_OpenFire && p_Now < p_Npc.m_WindowEnd;
    }

    void FireGate::ExtendLocked(Npc& p_Npc, double p_Now, float p_Seconds) {
        if (p_Npc.m_FirstAttempt < 0.0 || p_Npc.m_OpenFire) {
            return;
        }

        // Nie laenger als die maximale Verhandlungszeit seit dem ersten Zielen.
        const double s_Cap = p_Npc.m_FirstAttempt + m_Config.m_NegotiationMaxSeconds;
        const double s_Wanted = std::max(p_Npc.m_WindowEnd, p_Now + p_Seconds);
        p_Npc.m_WindowEnd = std::min(s_Wanted, std::max(s_Cap, p_Npc.m_WindowEnd));

        if (p_Npc.m_WindowEnd - p_Now > 3.0) {
            p_Npc.m_LastWarned = false;
        }
    }

    GateDecision FireGate::OnShotAttempt(uint64_t p_Npc, double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        GateDecision s_Decision;

        if (!m_Config.m_Enabled) {
            return s_Decision;
        }

        Npc& s_Npc = m_Npcs[p_Npc];

        // Lange nichts passiert -> neue Begegnung (Ueberredung bleibt bestehen).
        if (s_Npc.m_LastAttempt >= 0.0 && p_Now - s_Npc.m_LastAttempt > c_EncounterTimeout) {
            const double s_Persuaded = s_Npc.m_PersuadedUntil;
            s_Npc = Npc {};
            s_Npc.m_PersuadedUntil = s_Persuaded;
        }

        s_Npc.m_LastAttempt = p_Now;

        // 47 hat angegriffen: keine Gnade fuer diese Begegnung.
        if (p_Now - m_LastAggression < m_Config.m_AggressionMemorySeconds) {
            s_Npc.m_OpenFire = true;
            return s_Decision;
        }

        if (s_Npc.m_PersuadedUntil > p_Now) {
            s_Decision.m_Block = true;
            ++s_Npc.m_Blocked;
            return s_Decision;
        }

        if (s_Npc.m_OpenFire) {
            return s_Decision;
        }

        if (s_Npc.m_FirstAttempt < 0.0) {
            s_Npc.m_FirstAttempt = p_Now;
            s_Npc.m_WindowEnd = p_Now + m_Config.m_WarnSeconds;
            s_Npc.m_LastWarned = false;
            s_Decision.m_FirstAttempt = m_Config.m_WarnSeconds > 0.f;
        }

        if (p_Now < s_Npc.m_WindowEnd) {
            s_Decision.m_Block = true;
            ++s_Npc.m_Blocked;
        }

        return s_Decision;
    }

    void FireGate::KeepNegotiating(uint64_t p_Npc, double p_Now, float p_Seconds) {
        std::scoped_lock s_Lock(m_Mutex);

        if (!m_Config.m_Enabled) {
            return;
        }

        if (auto s_It = m_Npcs.find(p_Npc); s_It != m_Npcs.end()) {
            if (InWindow(s_It->second, p_Now)) {
                ExtendLocked(s_It->second, p_Now, p_Seconds);
            }
        }

        if (m_Config.m_Group) {
            // Die anderen Bewaffneten warten ab, wie das Gespraech ausgeht.
            for (auto& [s_Key, s_Npc] : m_Npcs) {
                if (s_Key != p_Npc && InWindow(s_Npc, p_Now)) {
                    ExtendLocked(s_Npc, p_Now, p_Seconds);
                }
            }
        }
    }

    void FireGate::Apply(uint64_t p_Npc, NpcAction p_Action, double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);

        if (!m_Config.m_Enabled || p_Action == NpcAction::None) {
            return;
        }

        const auto s_ForGroup = [&](auto&& p_Fn) {
            p_Fn(m_Npcs[p_Npc]);

            if (!m_Config.m_Group) {
                return;
            }

            for (auto& [s_Key, s_Npc] : m_Npcs) {
                const bool s_InSameStandoff = InWindow(s_Npc, p_Now) ||
                    (s_Npc.m_LastAttempt >= 0.0 && p_Now - s_Npc.m_LastAttempt < 8.0);

                if (s_Key != p_Npc && s_InSameStandoff) {
                    p_Fn(s_Npc);
                }
            }
        };

        switch (p_Action) {
            case NpcAction::HoldFire: {
                Npc& s_Npc = m_Npcs[p_Npc];

                if (s_Npc.m_FirstAttempt < 0.0) {
                    // Noch nie gezielt: nichts zu verlaengern.
                    break;
                }

                // Auch ein schon schiessender NPC kann sich zum Reden entschliessen.
                s_Npc.m_OpenFire = false;
                s_Npc.m_WindowEnd = std::max(s_Npc.m_WindowEnd, p_Now + m_Config.m_HoldFireExtend);
                s_Npc.m_LastWarned = false;
                break;
            }

            case NpcAction::OpenFire:
                s_ForGroup([&](Npc& p_Npc) {
                    p_Npc.m_OpenFire = true;
                    p_Npc.m_PersuadedUntil = 0.0;
                });
                break;

            case NpcAction::StandDown:
                s_ForGroup([&](Npc& p_Npc) {
                    p_Npc.m_PersuadedUntil = p_Now + m_Config.m_StandDownSeconds;
                    p_Npc.m_OpenFire = false;
                    p_Npc.m_FirstAttempt = -1.0;
                    p_Npc.m_WindowEnd = 0.0;
                });
                break;

            default:
                break;
        }
    }

    GateState FireGate::State(uint64_t p_Npc, double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        const auto s_It = m_Npcs.find(p_Npc);

        if (s_It == m_Npcs.end()) {
            return GateState::Idle;
        }

        const Npc& s_Npc = s_It->second;

        if (s_Npc.m_PersuadedUntil > p_Now) {
            return GateState::Persuaded;
        }

        if (s_Npc.m_LastAttempt < 0.0 || p_Now - s_Npc.m_LastAttempt > c_EncounterTimeout) {
            return GateState::Idle;
        }

        return InWindow(s_Npc, p_Now) ? GateState::Warning : GateState::Firing;
    }

    float FireGate::SecondsLeft(uint64_t p_Npc, double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        const auto s_It = m_Npcs.find(p_Npc);

        if (s_It == m_Npcs.end()) {
            return 0.f;
        }

        if (s_It->second.m_PersuadedUntil > p_Now) {
            return static_cast<float>(s_It->second.m_PersuadedUntil - p_Now);
        }

        return InWindow(s_It->second, p_Now) ? static_cast<float>(s_It->second.m_WindowEnd - p_Now) : 0.f;
    }

    std::vector<uint64_t> FireGate::ActiveStandoffs(double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        std::vector<uint64_t> s_Result;

        for (const auto& [s_Key, s_Npc] : m_Npcs) {
            // Ein Schussversuch in den letzten Sekunden zeigt, dass der NPC wirklich noch zielt.
            if (InWindow(s_Npc, p_Now) && p_Now - s_Npc.m_LastAttempt < 3.0) {
                s_Result.push_back(s_Key);
            }
        }

        return s_Result;
    }

    std::vector<uint64_t> FireGate::TakeLastWarnings(double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        std::vector<uint64_t> s_Result;

        for (auto& [s_Key, s_Npc] : m_Npcs) {
            if (!InWindow(s_Npc, p_Now) || s_Npc.m_LastWarned || p_Now - s_Npc.m_LastAttempt > 3.0) {
                continue;
            }

            const double s_Left = s_Npc.m_WindowEnd - p_Now;

            if (s_Left < 2.5 && s_Npc.m_WindowEnd - s_Npc.m_FirstAttempt > 4.0) {
                s_Npc.m_LastWarned = true;
                s_Result.push_back(s_Key);
            }
        }

        return s_Result;
    }
}
