// Verwundungen (experimentell):
//   - Beinschuss: der NPC humpelt (Tempo gedeckelt) und stolpert ab und zu. Zweiter Beinschuss: geht zu Boden.
//   - Bauch/Brust oder Messerstich: geht zu Boden (Ragdoll), kriecht von 47 weg, stoehnt – und verblutet nach einer Weile.
//   - Armschuss: Schmerzen.
//   - "Realistische Trefferzonen": siehe ZActor_YouGotHit (LivingWorld.cpp) – nur ein Teil des Schadens an Beinen/Armen/Rumpf.
// Alles laeuft auf dem Spiel-Thread.

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZContentKitManager.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZOutfit.h>
#include <Glacier/ZSetpieceEntity.h>
#include <Glacier/ZSpatialEntity.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace {
    // Knochen-Indizes aus der Clumsy-Mod des SDK: Becken = 1, Kopf = 11.
    constexpr uint32_t c_PelvisBone = 1;

    double RandomRange(double p_Min, double p_Max) {
        static std::mt19937 s_Random { std::random_device {}() };
        std::uniform_real_distribution<double> s_Dist(p_Min, std::max(p_Min, p_Max));
        return s_Dist(s_Random);
    }

    ZSpatialEntity* SpatialOf(ZActor* p_Actor) {
        ZEntityRef s_Ref;
        p_Actor->GetID(s_Ref);
        return s_Ref.QueryInterface<ZSpatialEntity>();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Toedliche Treffer nach abgeschwaechtem Schaden: sauber sterben lassen
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::ProcessPendingKills() {
    std::vector<void*> s_Kills;

    {
        std::scoped_lock s_Lock(m_KillMutex);
        s_Kills.swap(m_PendingKills);
    }

    if (s_Kills.empty() || !Globals::ActorManager || !Globals::NextActorId) {
        return;
    }

    const int s_Count = std::min<int>(*Globals::NextActorId, 500);

    for (void* s_Pointer : s_Kills) {
        // Nur NPCs anfassen, die es noch gibt und die noch leben.
        for (int i = 0; i < s_Count; ++i) {
            ZActor* s_Actor = Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef;

            if (s_Actor == s_Pointer && s_Actor->IsAlive()) {
                TEntityRef<IItem> s_Item;
                TEntityRef<ZSetpieceEntity> s_Setpiece;
                Functions::ZActor_KillActor->Call(s_Actor, s_Item, s_Setpiece, EDamageEvent::eDE_Shoot, EDeathBehavior::eDB_IMPACT_ANIM);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Neue Verwundung
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::ApplyWound(const lw::NpcSnapshot& p_Npc, lw::BodyZone p_Zone, lw::ViolenceKind p_Kind,
                             const lw::Settings& p_Settings, double p_Now) {
    if (!p_Settings.m_Injuries || !p_Npc.m_Actor || !p_Npc.CanTalk()) {
        return;
    }

    auto* s_Actor = static_cast<ZActor*>(p_Npc.m_Actor);
    Injury& s_Injury = m_Injuries[p_Npc.m_Key];
    s_Injury.m_Kind = p_Kind;

    if (p_Kind == lw::ViolenceKind::Stab) {
        p_Zone = lw::BodyZone::Torso;
    }

    switch (p_Zone) {
        case lw::BodyZone::Leg: ++s_Injury.m_LegHits; break;
        case lw::BodyZone::Arm: ++s_Injury.m_ArmHits; break;
        case lw::BodyZone::Torso: ++s_Injury.m_TorsoHits; break;
        default: break;
    }

    const bool s_GoDown = s_Injury.m_TorsoHits > 0 || s_Injury.m_LegHits >= 2;
    lw::WoundState s_State = s_GoDown ? lw::WoundState::Down
        : s_Injury.m_LegHits > 0 ? lw::WoundState::Limping : lw::WoundState::ArmHurt;

    if (s_GoDown && !s_Injury.m_Down) {
        s_Injury.m_Down = true;
        s_Injury.m_DownPos = p_Npc.m_Pos;
        s_Injury.m_NextCrawl = p_Now + 2.5;
        s_Injury.m_NextRagdollCheck = p_Now + 3.0;

        if (p_Settings.m_BleedOut) {
            // Stich/Bauchschuss: verblutet nach einer Weile; zwei Beinschuesse: langsamer.
            const double s_Factor = s_Injury.m_TorsoHits > 0 ? 1.0 : 1.8;
            s_Injury.m_BleedDeath = p_Now + RandomRange(p_Settings.m_BleedSecondsMin, p_Settings.m_BleedSecondsMax) * s_Factor;
        }

        Functions::ZHM5BaseCharacter_ActivateRagdoll->Call(s_Actor, true);
        lw::log::Info("{} geht verwundet zu Boden ({}).", lw::DisplayName(p_Npc), lw::BodyZoneName(p_Zone, lw::Language::German));
    }
    else if (s_State == lw::WoundState::Limping && s_Injury.m_LegHits == 1) {
        // Kurz einknicken.
        Functions::ZHM5BaseCharacter_ActivatePoweredRagdoll->Call(s_Actor, 0.35f, true, false, 0.15f, false);
        s_Injury.m_NextStumble = p_Now + RandomRange(5.0, 9.0);
        lw::log::Info("{} humpelt (Beinschuss).", lw::DisplayName(p_Npc));
    }

    s_Injury.m_State = s_State;
    s_Injury.m_NextMoan = p_Now + RandomRange(4.0, 7.0);

    // Die KI soll wissen, wie es dem NPC geht.
    lw::NpcMind& s_Mind = m_Book.Mind(p_Npc, m_World, p_Settings.m_Language);
    s_Mind.m_Wounded = true;
    s_Mind.m_Condition = lw::WoundStateText(s_State, p_Zone, p_Kind, p_Settings.m_Language);
}

// ---------------------------------------------------------------------------------------------------------------------
// Jeden Frame: humpeln, kriechen, stoehnen, verbluten
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::UpdateInjuries(const lw::Settings& p_Settings, double p_Now, double p_Delta) {
    if (m_Injuries.empty()) {
        return;
    }

    for (auto s_It = m_Injuries.begin(); s_It != m_Injuries.end();) {
        const uint64_t s_Key = s_It->first;
        Injury& s_Injury = s_It->second;
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Key);

        // Tot, bewusstlos oder ausser Reichweite: vergessen.
        if (!s_Npc || !s_Npc->m_Alive || !s_Npc->m_Actor) {
            s_It = m_Injuries.erase(s_It);
            continue;
        }

        if (!p_Settings.m_Injuries) {
            s_It = m_Injuries.erase(s_It);
            continue;
        }

        auto* s_Actor = static_cast<ZActor*>(s_Npc->m_Actor);
        ZSpatialEntity* s_Spatial = SpatialOf(s_Actor);

        if (!s_Spatial) {
            ++s_It;
            continue;
        }

        const bool s_Conscious = !s_Npc->m_Pacified;

        // --- Verbluten ----------------------------------------------------------------------------------------------
        if (s_Injury.m_BleedDeath > 0.0 && p_Now >= s_Injury.m_BleedDeath) {
            lw::log::Info("{} ist verblutet.", lw::DisplayName(*s_Npc));
            RegisterKill(*s_Npc, p_Settings);

            TEntityRef<IItem> s_Item;
            TEntityRef<ZSetpieceEntity> s_Setpiece;
            Functions::ZActor_KillActor->Call(
                s_Actor, s_Item, s_Setpiece,
                s_Injury.m_Kind == lw::ViolenceKind::Stab ? EDamageEvent::eDE_CloseCombat : EDamageEvent::eDE_Shoot,
                EDeathBehavior::eDB_NO_IMPACT_ANIM
            );

            s_It = m_Injuries.erase(s_It);
            continue;
        }

        // --- Am Boden: liegen bleiben und kriechen -------------------------------------------------------------------
        if (s_Injury.m_Down) {
            const lw::Vec3 s_Pos = s_Npc->m_Pos;

            // Steht die KI ihn wieder auf und laeuft los, sackt er erneut zusammen.
            if (p_Now >= s_Injury.m_NextRagdollCheck) {
                s_Injury.m_NextRagdollCheck = p_Now + 2.0;
                const float s_Dx = s_Pos.x - s_Injury.m_DownPos.x;
                const float s_Dy = s_Pos.y - s_Injury.m_DownPos.y;

                if (std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy) > 1.5f) {
                    Functions::ZHM5BaseCharacter_ActivateRagdoll->Call(s_Actor, true);
                }

                s_Injury.m_DownPos = s_Pos;
            }

            // Kriechen: kleine Schuebe am Becken, weg von 47.
            if (s_Conscious && p_Now >= s_Injury.m_NextCrawl && s_Actor->m_pRagdollHandler) {
                s_Injury.m_NextCrawl = p_Now + RandomRange(1.4, 2.4);

                float s_Ax = s_Pos.x - m_World.m_Player.m_Pos.x;
                float s_Ay = s_Pos.y - m_World.m_Player.m_Pos.y;
                const float s_Length = std::max(0.01f, std::sqrt(s_Ax * s_Ax + s_Ay * s_Ay));
                s_Ax /= s_Length;
                s_Ay /= s_Length;

                Functions::ZRagdollHandler_ApplyImpulseOnRagdoll->Call(
                    s_Actor->m_pRagdollHandler, float4 {}, float4 { s_Ax * 9.f, s_Ay * 9.f, 2.5f, 0.f }, c_PelvisBone, false
                );
            }
        }
        // --- Humpeln: Tempo deckeln und gelegentlich stolpern --------------------------------------------------------
        else if (s_Injury.m_LegHits > 0) {
            SMatrix s_Matrix = s_Spatial->GetObjectToWorldMatrix();
            const lw::Vec3 s_Pos { s_Matrix.Trans.x, s_Matrix.Trans.y, s_Matrix.Trans.z };

            if (s_Injury.m_HasLastPos && p_Delta > 0.0) {
                const float s_Dx = s_Pos.x - s_Injury.m_LastPos.x;
                const float s_Dy = s_Pos.y - s_Injury.m_LastPos.y;
                const float s_Moved = std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy);
                const float s_MaxStep = p_Settings.m_LimpSpeed * static_cast<float>(p_Delta);

                // Nur normales Laufen bremsen (keine Teleports/Animationen mit grossen Spruengen).
                if (s_Moved > s_MaxStep && s_Moved < 1.0f) {
                    const float s_Scale = s_MaxStep / s_Moved;
                    s_Matrix.Trans.x = s_Injury.m_LastPos.x + s_Dx * s_Scale;
                    s_Matrix.Trans.y = s_Injury.m_LastPos.y + s_Dy * s_Scale;
                    s_Spatial->SetObjectToWorldMatrixFromEditor(s_Matrix);
                }

                // Wer sich bewegt, stolpert ab und zu.
                if (s_Moved > 0.01f && p_Now >= s_Injury.m_NextStumble) {
                    s_Injury.m_NextStumble = p_Now + RandomRange(5.0, 9.0);
                    Functions::ZHM5BaseCharacter_ActivatePoweredRagdoll->Call(s_Actor, 0.3f, true, false, 0.15f, false);
                }
            }

            s_Injury.m_LastPos = { s_Matrix.Trans.x, s_Matrix.Trans.y, s_Matrix.Trans.z };
            s_Injury.m_HasLastPos = true;
        }

        // --- Stoehnen / um Hilfe flehen ------------------------------------------------------------------------------
        if (s_Conscious && p_Now >= s_Injury.m_NextMoan && !m_Speech.IsBusyWith(s_Key)) {
            s_Injury.m_NextMoan = p_Now + (s_Injury.m_Down ? RandomRange(6.0, 11.0) : RandomRange(9.0, 16.0));

            const std::string s_Line = lw::MoanLine(s_Injury.m_State, s_Key ^ static_cast<uint64_t>(p_Now * 3.0), p_Settings.m_Language);
            lw::NpcReply s_Moan;
            s_Moan.m_Say = s_Line;
            s_Moan.m_Emotion = "panicked";
            m_Book.RecordNpcLine(s_Key, lw::DisplayName(*s_Npc), s_Moan, HearersAround(s_Npc->m_Pos, 10.f), p_Now);
            SpeakNpcLine(*s_Npc, s_Line, s_Injury.m_Down ? "scared" : "panicked", p_Settings, p_Now);
        }

        ++s_It;
    }
}
