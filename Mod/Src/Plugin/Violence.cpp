// Realistische Reaktionen auf Schuesse, Messerstiche, Schlaege und Tote: Opfer schreien, Zeugen geraten in Panik
// oder greifen ein, und alle merken sich, was 47 getan hat (fliesst in spaetere KI-Antworten ein).

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <algorithm>

namespace {
    // Erst kurz warten, damit klar ist, ob das Opfer den Treffer ueberlebt hat.
    constexpr double c_EventDelay = 0.3;

    // Ein Treffer kurz vor dem Tod erklaert die Todesursache.
    constexpr double c_HitExplainsDeath = 3.0;
}

void LivingWorld::QueueViolence(uint64_t p_Victim, lw::ViolenceKind p_Kind, const lw::Vec3& p_Pos, double p_Time, bool p_Killed,
                                lw::BodyZone p_Zone) {
    std::scoped_lock s_Lock(m_ViolenceMutex);

    // Salven aus automatischen Waffen nur einmal als "Schuesse gehoert" melden.
    if (p_Kind == lw::ViolenceKind::ShotHeard) {
        if (p_Time - m_LastShotEvent < 1.5) {
            return;
        }

        m_LastShotEvent = p_Time;
    }

    if (m_ViolenceEvents.size() < 64) {
        m_ViolenceEvents.push_back({ p_Victim, p_Kind, p_Zone, p_Pos, p_Time, p_Killed });
    }
}

void LivingWorld::DetectDeaths(double p_Now) {
    for (const auto& s_Npc : m_World.m_Npcs) {
        const auto s_Previous = m_PreviousStates.find(s_Npc.m_Key);
        const bool s_Known = s_Previous != m_PreviousStates.end();
        const bool s_WasAlive = s_Known ? s_Previous->second.first : s_Npc.m_Alive;
        const bool s_WasPacified = s_Known ? s_Previous->second.second : s_Npc.m_Pacified;

        m_PreviousStates[s_Npc.m_Key] = { s_Npc.m_Alive, s_Npc.m_Pacified };

        if (!s_Known) {
            continue;
        }

        const bool s_Died = s_WasAlive && !s_Npc.m_Alive;
        const bool s_KnockedOut = !s_WasPacified && s_Npc.m_Pacified && s_Npc.m_Alive;

        if (!s_Died && !s_KnockedOut) {
            continue;
        }

        // Kam ein Treffer kurz vorher, kuemmert sich dieses Ereignis schon darum.
        const auto s_Hit = m_LastHits.find(s_Npc.m_Key);

        if (s_Hit != m_LastHits.end() && p_Now - s_Hit->second.first < c_HitExplainsDeath) {
            continue;
        }

        // Ohne Treffer: nur wenn 47 direkt daneben steht (Genickbruch, Wuergegriff, Faserdraht ...).
        // Unfaelle oder Gift aus der Ferne loesen keine "47 war's"-Reaktion aus.
        if (s_Npc.m_Distance > 3.f) {
            continue;
        }

        const lw::ViolenceKind s_Kind = s_KnockedOut ? lw::ViolenceKind::Takedown
            : (m_World.m_Ai.m_IllegalWeapon ? lw::ViolenceKind::Stab : lw::ViolenceKind::Melee);
        QueueViolence(s_Npc.m_Key, s_Kind, s_Npc.m_Pos, p_Now, s_Died);
    }

    // Wer laenger nicht mehr in der Naehe ist, wird vergessen.
    if (m_PreviousStates.size() > 600) {
        m_PreviousStates.clear();
    }
}

void LivingWorld::ProcessViolence(const lw::Settings& p_Settings, double p_Now) {
    std::vector<ViolenceEvent> s_Ready;

    {
        std::scoped_lock s_Lock(m_ViolenceMutex);

        if (!p_Settings.m_ViolenceReactions) {
            m_ViolenceEvents.clear();
            return;
        }

        for (auto s_It = m_ViolenceEvents.begin(); s_It != m_ViolenceEvents.end();) {
            if (p_Now - s_It->m_Time >= c_EventDelay || p_Now < s_It->m_Time) {
                s_Ready.push_back(*s_It);
                s_It = m_ViolenceEvents.erase(s_It);
            }
            else {
                ++s_It;
            }
        }
    }

    const lw::Language s_Lang = p_Settings.m_Language;

    for (const ViolenceEvent& s_Event : s_Ready) {
        ++m_ViolenceCount;

        const lw::NpcSnapshot* s_Victim = s_Event.m_Victim ? m_World.FindNpc(s_Event.m_Victim) : nullptr;
        const bool s_Killed = s_Event.m_Killed || (s_Victim && !s_Victim->m_Alive);
        const std::string s_VictimName = s_Victim ? lw::DisplayName(*s_Victim) : "";
        const lw::Vec3 s_Pos = s_Victim ? s_Victim->m_Pos : s_Event.m_Pos;

        if (s_Victim && s_Event.m_Kind != lw::ViolenceKind::ShotHeard) {
            m_LastHits[s_Victim->m_Key] = { p_Now, s_Event.m_Kind };
        }

        lw::log::Info(
            "Gewalt: {} {} [{}] ({})", s_VictimName.empty() ? "-" : s_VictimName,
            s_Event.m_Kind == lw::ViolenceKind::Gunshot ? "angeschossen" :
            s_Event.m_Kind == lw::ViolenceKind::Stab ? "gestochen" :
            s_Event.m_Kind == lw::ViolenceKind::Melee ? "angegriffen" :
            s_Event.m_Kind == lw::ViolenceKind::Takedown ? "ausgeschaltet" : "Schuss gehoert",
            lw::BodyZoneName(s_Event.m_Zone, lw::Language::German), s_Killed ? "tot" : "lebt"
        );

        // Ruf: jeder Tote durch 47 spricht sich herum.
        if (s_Victim && s_Killed && s_Event.m_Kind != lw::ViolenceKind::ShotHeard) {
            RegisterKill(*s_Victim, p_Settings);
        }

        // --- Das Opfer selbst (nur wer noch bei Bewusstsein ist, kann schreien) ---------------------------------------
        if (s_Victim && s_Victim->CanTalk() && s_Event.m_Kind != lw::ViolenceKind::Takedown &&
            s_Event.m_Kind != lw::ViolenceKind::ShotHeard) {
            lw::NpcMind& s_Mind = m_Book.Mind(*s_Victim, m_World, s_Lang);
            s_Mind.m_Wounded = s_Event.m_Kind != lw::ViolenceKind::Melee;

            const lw::AttitudeShock s_Shock = lw::ShockFor(s_Event.m_Kind, lw::ReactionRole::Victim, s_Victim->m_Guard, false);
            m_Book.AddExperience(
                s_Victim->m_Key, lw::ExperienceText(s_Event.m_Kind, lw::ReactionRole::Victim, false, "", s_Lang),
                s_Shock.m_Trust, s_Shock.m_Suspicion, s_Shock.m_Fear
            );

            // Koerperliche Folgen: humpeln, zu Boden gehen, verbluten (siehe Injuries.cpp).
            const bool s_Wound = s_Event.m_Kind == lw::ViolenceKind::Gunshot || s_Event.m_Kind == lw::ViolenceKind::Stab;

            if (s_Wound) {
                ApplyWound(*s_Victim, s_Event.m_Zone, s_Event.m_Kind, p_Settings, p_Now);
            }

            if (p_Now - s_Mind.m_LastReaction > 3.0) {
                s_Mind.m_LastReaction = p_Now;

                const uint64_t s_Seed = s_Victim->m_Key ^ static_cast<uint64_t>(p_Now * 13.0);
                lw::NpcReply s_Scream;
                s_Scream.m_Say = s_Wound && p_Settings.m_Injuries
                    ? lw::WoundLine(s_Event.m_Zone, s_Event.m_Kind, s_Seed, s_Lang)
                    : lw::ReactionLine(s_Event.m_Kind, lw::ReactionRole::Victim, s_Victim->m_Guard, false, s_Seed, s_Lang);
                s_Scream.m_Emotion = lw::ReactionEmotion(lw::ReactionRole::Victim, s_Victim->m_Guard);
                m_Book.RecordNpcLine(s_Victim->m_Key, s_VictimName, s_Scream, HearersAround(s_Pos, 10.f), p_Now);
                m_Book.AddToGlobalLog({ p_Now, s_VictimName, s_Scream.m_Say, false, s_Scream.m_Emotion });
                SpeakNpcLine(*s_Victim, s_Scream.m_Say, s_Scream.m_Emotion, p_Settings, p_Now);
            }
        }

        // --- Zeugen ------------------------------------------------------------------------------------------------------
        int s_Voices = 0;

        // Die naechsten Zeugen zuerst (m_Npcs ist nach Abstand zu 47 sortiert – fuer das Ereignis neu sortieren).
        std::vector<const lw::NpcSnapshot*> s_Candidates;

        for (const auto& s_Npc : m_World.m_Npcs) {
            if (s_Npc.CanTalk() && (!s_Victim || s_Npc.m_Key != s_Victim->m_Key)) {
                s_Candidates.push_back(&s_Npc);
            }
        }

        std::sort(s_Candidates.begin(), s_Candidates.end(), [&](const lw::NpcSnapshot* a, const lw::NpcSnapshot* b) {
            return lw::Distance(a->m_Pos, s_Pos) < lw::Distance(b->m_Pos, s_Pos);
        });

        for (const lw::NpcSnapshot* s_Npc : s_Candidates) {
            const float s_Distance = lw::Distance(s_Npc->m_Pos, s_Pos);
            lw::ViolenceKind s_Kind = s_Event.m_Kind;
            lw::ReactionRole s_Role;

            if (s_Event.m_Kind == lw::ViolenceKind::ShotHeard) {
                // Wer neben 47 steht, sieht ihn schiessen; alle anderen hoeren es nur.
                if (s_Distance <= 10.f) {
                    s_Kind = lw::ViolenceKind::Gunshot;
                    s_Role = lw::ReactionRole::Eyewitness;
                }
                else if (s_Distance <= lw::HearingRange(s_Event.m_Kind)) {
                    s_Role = lw::ReactionRole::Earwitness;
                }
                else {
                    continue;
                }
            }
            else if (s_Distance <= lw::SightRange(s_Event.m_Kind)) {
                s_Role = lw::ReactionRole::Eyewitness;
            }
            else if (s_Distance <= lw::HearingRange(s_Event.m_Kind) && s_Event.m_Kind == lw::ViolenceKind::Gunshot) {
                s_Role = lw::ReactionRole::Earwitness;
            }
            else {
                continue;
            }

            lw::NpcMind& s_Mind = m_Book.Mind(*s_Npc, m_World, s_Lang);
            const lw::AttitudeShock s_Shock = lw::ShockFor(s_Kind, s_Role, s_Npc->m_Guard, s_Killed);
            m_Book.AddExperience(
                s_Npc->m_Key, lw::ExperienceText(s_Kind, s_Role, s_Killed, s_VictimName, s_Lang),
                s_Shock.m_Trust, s_Shock.m_Suspicion, s_Shock.m_Fear
            );

            // Nur ein paar sind wirklich zu hoeren – sonst schreit der ganze Raum durcheinander.
            const bool s_EarwitnessAllowed = s_Role != lw::ReactionRole::Earwitness || p_Now - m_LastEarwitnessVoice > 8.0;

            if (s_Voices >= p_Settings.m_MaxReactionVoices || p_Now - s_Mind.m_LastReaction < 6.0 || !s_EarwitnessAllowed) {
                continue;
            }

            if (m_Speech.IsBusyWith(s_Npc->m_Key)) {
                continue;
            }

            s_Mind.m_LastReaction = p_Now;

            if (s_Role == lw::ReactionRole::Earwitness) {
                m_LastEarwitnessVoice = p_Now;
            }

            lw::NpcReply s_Line;
            s_Line.m_Say = lw::ReactionLine(
                s_Kind, s_Role, s_Npc->m_Guard, s_Killed, s_Npc->m_Key ^ static_cast<uint64_t>(p_Now * 7.0), s_Lang
            );
            s_Line.m_Emotion = lw::ReactionEmotion(s_Role, s_Npc->m_Guard);

            const std::string s_Name = lw::DisplayName(*s_Npc);
            m_Book.RecordNpcLine(s_Npc->m_Key, s_Name, s_Line, HearersAround(s_Npc->m_Pos, p_Settings.m_RadiusNormal), p_Now);
            m_Book.AddToGlobalLog({ p_Now, s_Name, s_Line.m_Say, false, s_Line.m_Emotion });
            SpeakNpcLine(*s_Npc, s_Line.m_Say, s_Line.m_Emotion, p_Settings, p_Now);
            ++s_Voices;
        }
    }

    // Alte Treffer vergessen.
    std::erase_if(m_LastHits, [p_Now](const auto& p_Entry) { return p_Now - p_Entry.second.first > 30.0; });
}
