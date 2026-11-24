// Entwaffnen, Drohungen und der Ruf von 47 (Spiel-Thread).

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZSpatialEntity.h>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------------------------------------------------
// Waffe aus der Hand: faellt zu Boden, der NPC bettelt um sein Leben
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::ProcessPendingDisarms(const lw::Settings& p_Settings, double p_Now) {
    std::vector<void*> s_Disarms;

    {
        std::scoped_lock s_Lock(m_KillMutex);
        s_Disarms.swap(m_PendingDisarms);
    }

    if (s_Disarms.empty() || !Globals::ActorManager || !Globals::NextActorId) {
        return;
    }

    const int s_Count = std::min<int>(*Globals::NextActorId, 500);

    for (void* s_Pointer : s_Disarms) {
        ZActor* s_Actor = nullptr;

        // Nur NPCs anfassen, die es noch gibt und die bei Bewusstsein sind.
        for (int i = 0; i < s_Count; ++i) {
            if (Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef == s_Pointer) {
                s_Actor = Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef;
                break;
            }
        }

        if (!s_Actor || !s_Actor->IsAlive() || s_Actor->IsPacified()) {
            continue;
        }

        ZActorInventoryHandler* s_Inventory = s_Actor->m_pInventoryHandler;

        if (!s_Inventory || !s_Inventory->m_rMainWeapon) {
            continue;
        }

        // Wie die Editor-Mod des SDK, aber mit "in die Physikwelt": die Waffe faellt sichtbar zu Boden.
        const TEntityRef<IItem> s_Weapon(s_Inventory->m_rMainWeapon.m_entityRef);
        s_Actor->ReleaseItem(s_Weapon, true);
        s_Inventory->m_rMainWeapon = {};
        s_Actor->m_bWeaponUnholstered = false;

        ZEntityRef s_Ref;
        s_Actor->GetID(s_Ref);
        const auto s_Key = reinterpret_cast<uint64_t>(s_Ref.m_pObj);
        m_Disarmed.insert(s_Key);

        // Schiessen kann er ohnehin nicht mehr – und er gibt auf, statt loszurennen.
        m_Gate.Apply(s_Key, lw::NpcAction::StandDown, p_Now);
        Surrender(s_Key, p_Settings, p_Now);

        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Key);
        const std::string s_Name = s_Npc ? lw::DisplayName(*s_Npc) : "NPC";
        lw::log::Info("{} wurde entwaffnet – die Waffe liegt am Boden.", s_Name);

        if (!s_Npc) {
            continue;
        }

        lw::NpcMind& s_Mind = m_Book.Mind(*s_Npc, m_World, p_Settings.m_Language);
        m_Book.AddExperience(
            s_Key,
            p_Settings.m_Language == lw::Language::German
                ? "Diese Person hat dir die Waffe aus der Hand geschossen/geschlagen. Du bist wehrlos und hast Todesangst."
                : "This person shot/knocked the weapon out of your hand. You are defenceless and terrified.",
            -40, 50, 60
        );

        lw::NpcReply s_Beg;
        s_Beg.m_Say = lw::DisarmedLine(s_Key ^ static_cast<uint64_t>(p_Now * 5.0), p_Settings.m_Language);
        s_Beg.m_Emotion = "panicked";
        m_Book.RecordNpcLine(s_Key, s_Name, s_Beg, HearersAround(s_Npc->m_Pos, p_Settings.m_RadiusNormal), p_Now);
        m_Book.AddToGlobalLog({ p_Now, s_Name, s_Beg.m_Say, false, s_Beg.m_Emotion });
        SpeakNpcLine(*s_Npc, s_Beg.m_Say, s_Beg.m_Emotion, p_Settings, p_Now);
        s_Mind.m_LastReaction = p_Now;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Ruf: jeder getoetete Bewaffnete macht 47 gefuerchteter
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::RegisterKill(const lw::NpcSnapshot& p_Victim, const lw::Settings& p_Settings) {
    if (!p_Settings.m_Reputation || !m_CountedKills.insert(p_Victim.m_Key).second) {
        return;
    }

    const bool s_Armed = p_Victim.m_Guard || p_Victim.m_WeaponsAllowed;

    if (s_Armed) {
        ++m_ArmedKills;
    }
    else {
        ++m_CivilianKills;
    }

    lw::log::Info(
        "Ruf: {} getoetet ({}). Bisher {} Bewaffnete, {} Zivilisten.", lw::DisplayName(p_Victim),
        s_Armed ? "bewaffnet" : "Zivilist", m_ArmedKills.load(), m_CivilianKills.load()
    );

    // Es spricht sich herum: alle bekannten NPCs bekommen mehr Angst.
    m_Book.ForEachMind([&](lw::NpcMind& p_Mind) {
        p_Mind.m_Fear = std::min(100, p_Mind.m_Fear + (s_Armed ? 10 : 5));
        p_Mind.m_Trust = std::max(0, p_Mind.m_Trust - 5);
    });

    // Wachen zoegern laenger (siehe ApplySettingsToServices).
    ApplySettingsToServices(p_Settings);
}

// ---------------------------------------------------------------------------------------------------------------------
// Drohungen: Panik, Betteln, Aufgeben
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::HandleThreat(const std::vector<uint64_t>& p_Hearers, uint64_t p_Addressee, const lw::Settings& p_Settings, double p_Now) {
    const bool s_De = p_Settings.m_Language == lw::Language::German;
    const int s_Kills = p_Settings.m_Reputation ? m_ArmedKills.load() : 0;
    int s_Voices = 0;

    for (const uint64_t s_Key : p_Hearers) {
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_Key);

        if (!s_Npc || !s_Npc->CanTalk()) {
            continue;
        }

        m_ThreatenedUntil[s_Key] = p_Now + 40.0;

        const bool s_CanResist = s_Npc->IsArmed() && !m_Disarmed.contains(s_Key) && s_Kills < 3;
        lw::NpcMind& s_Mind = m_Book.Mind(*s_Npc, m_World, p_Settings.m_Language);
        m_Book.AddExperience(
            s_Key,
            s_De ? (s_Key == p_Addressee ? "Diese Person hat DIR mit dem Tod gedroht und gesagt, sie sei bewaffnet."
                                         : "Du hast gehört, wie diese Person jemandem mit dem Tod gedroht hat.")
                 : (s_Key == p_Addressee ? "This person threatened to KILL YOU and said he is armed."
                                         : "You heard this person threaten someone with death."),
            -20, 30, s_CanResist ? 20 : 45
        );

        // Bewaffnete, die gegen seinen Ruf keine Chance sehen, geben auf.
        if (s_Npc->IsArmed() && !s_CanResist) {
            m_Gate.Apply(s_Key, lw::NpcAction::StandDown, p_Now);
        }

        // Wer keine Chance sieht, erstarrt vor Angst (statt wie im Original-Spiel wegzurennen und Alarm zu schlagen).
        if (!s_CanResist && (s_Key == p_Addressee || s_Npc->m_Distance < 8.f)) {
            Surrender(s_Key, p_Settings, p_Now);
        }

        // Sofortige Reaktion (die ausfuehrliche Antwort der KI folgt): Angesprochener + bis zu 1 weiterer.
        if (s_Voices < 2 && p_Now - s_Mind.m_LastReaction > 4.0 && !m_Speech.IsBusyWith(s_Key)) {
            s_Mind.m_LastReaction = p_Now;
            ++s_Voices;

            lw::NpcReply s_Line;
            s_Line.m_Say = lw::ThreatReactionLine(s_CanResist, s_Kills, s_Key ^ static_cast<uint64_t>(p_Now * 9.0), p_Settings.m_Language);
            s_Line.m_Emotion = s_CanResist ? "nervous" : "panicked";
            const std::string s_Name = lw::DisplayName(*s_Npc);
            m_Book.RecordNpcLine(s_Key, s_Name, s_Line, HearersAround(s_Npc->m_Pos, p_Settings.m_RadiusNormal), p_Now);
            m_Book.AddToGlobalLog({ p_Now, s_Name, s_Line.m_Say, false, s_Line.m_Emotion });
            SpeakNpcLine(*s_Npc, s_Line.m_Say, s_Line.m_Emotion, p_Settings, p_Now);
        }
    }

    lw::log::Info("Drohung erkannt – {} NPCs haben sie gehoert.", p_Hearers.size());
}

// ---------------------------------------------------------------------------------------------------------------------
// Erstarren vor Angst: die Spiel-KI darf sie nur noch im Schritttempo bewegen
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::Surrender(uint64_t p_Key, const lw::Settings& p_Settings, double p_Now) {
    if (!p_Settings.m_SurrenderFreeze) {
        return;
    }

    SurrenderState& s_State = m_Surrendered[p_Key];

    if (s_State.m_Until < p_Now) {
        s_State.m_HasLastPos = false;
    }

    s_State.m_Until = p_Now + p_Settings.m_SurrenderSeconds;
}

void LivingWorld::UpdateSurrenders(const lw::Settings& p_Settings, double p_Now, double p_Delta) {
    // Langsam zurueckweichen ist erlaubt, wegrennen nicht.
    constexpr float c_MaxSpeed = 0.35f;

    for (auto s_It = m_Surrendered.begin(); s_It != m_Surrendered.end();) {
        const lw::NpcSnapshot* s_Npc = m_World.FindNpc(s_It->first);
        SurrenderState& s_State = s_It->second;

        if (!p_Settings.m_SurrenderFreeze || p_Now > s_State.m_Until || !s_Npc || !s_Npc->CanTalk() || !s_Npc->m_Actor) {
            s_It = m_Surrendered.erase(s_It);
            continue;
        }

        // Verwundete regelt UpdateInjuries (Humpeln/Kriechen).
        if (m_Injuries.contains(s_It->first)) {
            ++s_It;
            continue;
        }

        ZEntityRef s_Ref;
        static_cast<ZActor*>(s_Npc->m_Actor)->GetID(s_Ref);
        ZSpatialEntity* s_Spatial = s_Ref.QueryInterface<ZSpatialEntity>();

        if (!s_Spatial) {
            ++s_It;
            continue;
        }

        SMatrix s_Matrix = s_Spatial->GetObjectToWorldMatrix();

        if (s_State.m_HasLastPos && p_Delta > 0.0) {
            const float s_Dx = s_Matrix.Trans.x - s_State.m_LastPos.x;
            const float s_Dy = s_Matrix.Trans.y - s_State.m_LastPos.y;
            const float s_Moved = std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy);
            const float s_MaxStep = c_MaxSpeed * static_cast<float>(p_Delta);

            // Nur normales Laufen bremsen (keine Teleports/Animationen mit grossen Spruengen).
            if (s_Moved > s_MaxStep && s_Moved < 1.0f) {
                const float s_Scale = s_MaxStep / s_Moved;
                s_Matrix.Trans.x = s_State.m_LastPos.x + s_Dx * s_Scale;
                s_Matrix.Trans.y = s_State.m_LastPos.y + s_Dy * s_Scale;
                s_Spatial->SetObjectToWorldMatrixFromEditor(s_Matrix);
            }
        }

        s_State.m_LastPos = { s_Matrix.Trans.x, s_Matrix.Trans.y, s_Matrix.Trans.z };
        s_State.m_HasLastPos = true;
        ++s_It;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// KI-Antwort ausgeben (vorab gestreamt oder komplett)
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DeliverNpcReply(
    const lw::NpcSnapshot& p_Npc, lw::NpcReply p_Reply, int p_Kind, const lw::Settings& p_Settings, double p_Now
) {
    // Wer verwundet, bedroht oder entwaffnet ist, klingt nicht entspannt – egal was das Modell meint.
    const lw::NpcMind* s_Mind = m_Book.Find(p_Npc.m_Key);
    const auto s_Threat = m_ThreatenedUntil.find(p_Npc.m_Key);
    const bool s_Terrified = m_Disarmed.contains(p_Npc.m_Key) ||
        (s_Threat != m_ThreatenedUntil.end() && s_Threat->second > p_Now && !(p_Npc.IsArmed() && m_ArmedKills < 3));
    const bool s_Relaxed = p_Reply.m_Emotion == "neutral" || p_Reply.m_Emotion == "friendly" ||
        p_Reply.m_Emotion == "amused" || p_Reply.m_Emotion == "annoyed";

    if (s_Terrified && (s_Relaxed || p_Reply.m_Emotion == "suspicious" || p_Reply.m_Emotion == "angry")) {
        p_Reply.m_Emotion = "panicked";
    }
    else if (s_Mind && s_Mind->m_Wounded && s_Relaxed) {
        p_Reply.m_Emotion = "scared";
    }

    if (p_Reply.m_Say.empty()) {
        return;
    }

    const std::string s_Name = lw::DisplayName(p_Npc);
    const std::vector<uint64_t> s_Hearers = HearersAround(p_Npc.m_Pos, p_Settings.m_RadiusNormal);
    m_Book.RecordNpcLine(p_Npc.m_Key, s_Name, p_Reply, s_Hearers, p_Now);
    m_Book.AddToGlobalLog({ p_Now, s_Name, p_Reply.m_Say, false, p_Reply.m_Emotion });
    SpeakNpcLine(p_Npc, p_Reply.m_Say, p_Reply.m_Emotion, p_Settings, p_Now);

    if (p_Kind == static_cast<int>(lw::DialogueTrigger::Initiate) || p_Kind == static_cast<int>(lw::DialogueTrigger::PlayerSpoke)) {
        // Wer 47 anspricht oder ihm antwortet, ist ab jetzt der Gespraechspartner.
        m_LastAddressee = p_Npc.m_Key;
        m_LastAddressTime = p_Now;
    }
}
