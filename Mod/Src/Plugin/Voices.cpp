// Originalstimmen der NPCs abschalten, damit sie nur noch ueber die KI der Mod reden.
//
// Jeder NPC bekommt VOR seiner Aktivierung die Stimmvariante "keine" (Original wird gesichert und beim Ausschalten
// wiederhergestellt). Das Feld liegt zwischen zwei vom SDK per static_assert gepruefte Felder von ZActor.
//
// WICHTIG: Die Standardstimmen der Outfits (ZGlobalOutfitKit::m_rDefaultVoiceVariations) NICHT anfassen – deren Lage
// im SDK-Header stimmt fuer diese Spielversion nicht. Ein Versuch, sie zu lesen, hat das Spiel beim Missionsstart
// abstuerzen lassen (Zugriff auf Adresse 0x3A, Minidump vom 20.10.2026).

#include "LivingWorld.h"

#include "Log.h"

#include <Glacier/ZActor.h>

void LivingWorld::MuteActor(void* p_Actor) {
    auto* s_Actor = static_cast<ZActor*>(p_Actor);

    if (!s_Actor || s_Actor->m_eRequiredVoiceVariation == EActorVoiceVariation::eAVV_Undefined) {
        return;
    }

    std::scoped_lock s_Lock(m_VoiceMutex);
    m_SavedVoiceVariations.try_emplace(s_Actor, static_cast<int>(s_Actor->m_eRequiredVoiceVariation));
    s_Actor->m_eRequiredVoiceVariation = EActorVoiceVariation::eAVV_Undefined;
}

void LivingWorld::UpdateVanillaVoices(const lw::Settings& p_Settings) {
    if (RealTime() - m_LastVoiceMuteCheck < 1.0) {
        return;
    }

    m_LastVoiceMuteCheck = RealTime();

    if (!Globals::ActorManager || !Globals::NextActorId) {
        return;
    }

    const bool s_Mute = p_Settings.m_Enabled && p_Settings.m_MuteVanillaVoices;
    const int s_Count = std::min<int>(*Globals::NextActorId, 500);
    int s_Changed = 0;

    for (int i = 0; i < s_Count; ++i) {
        ZActor* s_Actor = Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef;

        if (!s_Actor) {
            continue;
        }

        if (s_Mute) {
            if (s_Actor->m_eRequiredVoiceVariation != EActorVoiceVariation::eAVV_Undefined) {
                MuteActor(s_Actor);
                ++s_Changed;
            }
        }
        else {
            std::scoped_lock s_Lock(m_VoiceMutex);

            if (const auto s_It = m_SavedVoiceVariations.find(s_Actor); s_It != m_SavedVoiceVariations.end()) {
                s_Actor->m_eRequiredVoiceVariation = static_cast<EActorVoiceVariation>(s_It->second);
                ++s_Changed;
            }
        }
    }

    if (!s_Mute) {
        std::scoped_lock s_Lock(m_VoiceMutex);
        m_SavedVoiceVariations.clear();
    }

    if (s_Changed > 0) {
        lw::log::Info("Originalstimmen {}: {} NPCs.", s_Mute ? "stummgeschaltet" : "wiederhergestellt", s_Changed);
    }
}

// Jede Entity, die das Spiel aktiviert, kommt hier vorbei – NPCs werden VOR ihrer Aktivierung stumm geschaltet,
// damit das Spiel gar nicht erst eine Stimme fuer sie laedt.
DEFINE_PLUGIN_DETOUR(LivingWorld, void, ZEntityManager_ActivateEntity, ZEntityManager* th, ZEntityRef* p_Entity, void* p_Unk) {
    if (m_MuteVoices && p_Entity && *p_Entity) {
        if (auto* s_Actor = p_Entity->QueryInterface<ZActor>()) {
            MuteActor(s_Actor);
        }
    }

    return { HookAction::Continue() };
}
