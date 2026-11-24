// Liest pro Bild den fuer Living World relevanten Zustand aus dem Spiel (nur auf dem Spiel-Thread aufrufen).

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"
#include "VoiceGender.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZAIGameState.h>
#include <Glacier/ZBehavior.h>
#include <Glacier/ZCameraEntity.h>
#include <Glacier/ZContract.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZOutfit.h>
#include <Glacier/ZSpatialEntity.h>

#include <algorithm>
#include <cmath>

namespace {
    constexpr float c_DetailRadius = 45.f; // weiter entfernte NPCs nur grob erfassen
    constexpr float c_HeadHeight = 1.72f;

    lw::Vec3 ToVec(const float4& p_Value) {
        return { p_Value.x, p_Value.y, p_Value.z };
    }

    std::string ToStd(const ZString& p_Value) {
        return std::string(p_Value.c_str(), p_Value.size());
    }

    float HorizontalAngleDegrees(const lw::Vec3& p_Forward, const lw::Vec3& p_From, const lw::Vec3& p_To) {
        const float s_Dx = p_To.x - p_From.x;
        const float s_Dy = p_To.y - p_From.y;
        const float s_Length = std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy);
        const float s_ForwardLength = std::sqrt(p_Forward.x * p_Forward.x + p_Forward.y * p_Forward.y);

        if (s_Length < 0.01f || s_ForwardLength < 0.01f) {
            return 0.f;
        }

        const float s_Dot = (s_Dx * p_Forward.x + s_Dy * p_Forward.y) / (s_Length * s_ForwardLength);
        return std::acos(std::clamp(s_Dot, -1.f, 1.f)) * 57.2957795f;
    }
}

bool LivingWorld::ReadWorld(lw::WorldSnapshot& p_World, const lw::Settings& p_Settings) {
    p_World = {};
    p_World.m_GameTime = GameNow();

    if (!Globals::ActorManager || !Globals::NextActorId || !Globals::Hitman5Module) {
        return false;
    }

    const auto* s_SceneContext = Globals::Hitman5Module->m_pEntitySceneContext;

    if (!s_SceneContext || !s_SceneContext->m_pScene) {
        return false;
    }

    p_World.m_Scene = ToStd(s_SceneContext->m_SceneInitParameters.m_SceneResource);

    // Im Hauptmenue gibt es auch einen "Spieler" – dort nichts tun.
    if (lw::ToLowerAscii(p_World.m_Scene).find("/frontend/") != std::string::npos) {
        return false;
    }

    const auto s_Player = SDK()->GetLocalPlayer();

    if (!s_Player || !s_Player.m_pInterfaceRef) {
        return false;
    }

    auto* s_PlayerSpatial = s_Player.m_entityRef.QueryInterface<ZSpatialEntity>();

    if (!s_PlayerSpatial) {
        return false;
    }

    const SMatrix s_PlayerMatrix = s_PlayerSpatial->GetObjectToWorldMatrix();
    p_World.m_Player.m_Valid = true;
    p_World.m_Player.m_Pos = ToVec(s_PlayerMatrix.Trans);

    if (const auto* s_Kit = s_Player.m_pInterfaceRef->m_rOutfitKit.m_pInterfaceRef) {
        p_World.m_Player.m_Outfit = ToStd(s_Kit->m_sCommonName);
        p_World.m_Player.m_OutfitTitle = ToStd(s_Kit->m_sTitle);
        p_World.m_Player.m_InSuit = s_Kit->m_bIsHitmanSuit;
        p_World.m_Player.m_OutfitIsGuard = s_Kit->m_eActorType == EActorType::eAT_Guard;
    }

    // Kamera: Glacier-Kameras schauen entlang ihrer -Z-Achse.
    if (auto* s_Camera = Functions::GetCurrentCamera->Call()) {
        const SMatrix s_CameraMatrix = s_Camera->GetObjectToWorldMatrix();
        p_World.m_CameraPos = ToVec(s_CameraMatrix.Trans);
        p_World.m_CameraForward = { -s_CameraMatrix.ZAxis.x, -s_CameraMatrix.ZAxis.y, -s_CameraMatrix.ZAxis.z };
        p_World.m_CameraRight = ToVec(s_CameraMatrix.XAxis);
    }
    else {
        p_World.m_CameraPos = p_World.m_Player.m_Pos;
        p_World.m_CameraForward = { -s_PlayerMatrix.YAxis.x, -s_PlayerMatrix.YAxis.y, 0.f };
        p_World.m_CameraRight = ToVec(s_PlayerMatrix.XAxis);
    }

    if (Globals::GameStatsManager) {
        const ZAIGameState& s_State = Globals::GameStatsManager->m_gameState;
        p_World.m_Ai.m_Tension = static_cast<int>(s_State.m_eGameTension);
        p_World.m_Ai.m_Trespassing = s_State.m_bTrespassing;
        p_World.m_Ai.m_InDisguise = s_State.m_bInDisguise;
        p_World.m_Ai.m_DisguiseSuspicious = s_State.m_bDisguiseSuspicious;
        p_World.m_Ai.m_DisguiseBroken = s_State.m_bDisguiseBroken;
        p_World.m_Ai.m_IllegalWeapon = s_State.m_bHoldingIllegalWeapon01 || s_State.m_bHoldingIllegalWeapon02;
        p_World.m_Ai.m_BodyFound = s_State.m_bBodyFound || s_State.m_bDeadBodySeen;
        p_World.m_Ai.m_BeingFrisked = s_State.m_bBeingFrisked;
        p_World.m_Ai.m_EnemiesAlerted = s_State.m_actorCounts.m_nEnemiesIsAlerted;
        p_World.m_Ai.m_EnemiesInCombat = s_State.m_actorCounts.m_nEnemiesInCombat;
    }

    if (Globals::ContractsManager) {
        p_World.m_LocationId = ToStd(Globals::ContractsManager->m_contractContext.m_sLocationId);
    }

    p_World.m_LocationName = lw::FriendlyLocation(p_World.m_LocationId, p_World.m_Scene, p_Settings.m_Language);

    // --- NPCs ---------------------------------------------------------------------------------------------------------
    const int s_Count = std::min<int>(*Globals::NextActorId, 500);
    p_World.m_ActorCount = s_Count;
    p_World.m_Npcs.reserve(64);

    for (int i = 0; i < s_Count; ++i) {
        ZActor* s_Actor = Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef;

        if (!s_Actor) {
            continue;
        }

        ZEntityRef s_Ref;
        s_Actor->GetID(s_Ref);

        if (!s_Ref) {
            continue;
        }

        auto* s_Spatial = s_Ref.QueryInterface<ZSpatialEntity>();

        if (!s_Spatial) {
            continue;
        }

        const lw::Vec3 s_Pos = ToVec(s_Spatial->GetObjectToWorldMatrix().Trans);
        const float s_Distance = lw::Distance(s_Pos, p_World.m_Player.m_Pos);
        const bool s_Target = s_Actor->m_bContractTarget;

        if (s_Distance > c_DetailRadius && !s_Target) {
            continue;
        }

        lw::NpcSnapshot s_Npc;
        s_Npc.m_Key = reinterpret_cast<uint64_t>(s_Ref.m_pObj);
        s_Npc.m_Index = i;
        s_Npc.m_Actor = s_Actor;

        {
            // Wurde die Originalstimme stummgeschaltet, steht die echte Variante in unserer Sicherung.
            std::scoped_lock s_Lock(m_VoiceMutex);
            const auto s_Saved = m_SavedVoiceVariations.find(s_Actor);
            s_Npc.m_OriginalVoice = s_Saved != m_SavedVoiceVariations.end()
                ? s_Saved->second
                : static_cast<int>(s_Actor->m_eRequiredVoiceVariation);
        }
        s_Npc.m_Name = ToStd(s_Actor->m_sActorName);
        s_Npc.m_Pos = s_Pos;
        s_Npc.m_Head = { s_Pos.x, s_Pos.y, s_Pos.z + c_HeadHeight };
        s_Npc.m_Distance = s_Distance;
        s_Npc.m_Target = s_Target;
        s_Npc.m_Named = s_Actor->m_bIsNamedNPC;
        s_Npc.m_WeaponOut = s_Actor->m_bWeaponUnholstered;
        s_Npc.m_Enforcer = s_Actor->m_bIsActiveEnforcer || s_Actor->m_bIsDynamicEnforcer;
        s_Npc.m_Crowd = s_Actor->m_bCrowdCharacter;
        s_Npc.m_Alive = s_Actor->IsAlive();
        s_Npc.m_Pacified = s_Actor->IsPacified();

        if (const auto* s_Kit = s_Actor->m_rOutfit.m_pInterfaceRef) {
            s_Npc.m_Outfit = ToStd(s_Kit->m_sCommonName);
            s_Npc.m_OutfitTitle = ToStd(s_Kit->m_sTitle);
            // Frau, wenn das Outfit es sagt ODER die Stimme des Spiels eine Frauenstimme ist.
            s_Npc.m_Female = s_Kit->m_bIsFemale || lw::IsFemaleVoiceVariation(s_Npc.m_OriginalVoice);
            s_Npc.m_Guard = s_Kit->m_eActorType == EActorType::eAT_Guard;
            s_Npc.m_Rank = static_cast<int>(s_Kit->m_eActorRank);
            s_Npc.m_Authority = s_Kit->m_bAuthorityFigure;
            s_Npc.m_WeaponsAllowed = s_Kit->m_bWeaponsAllowed;

            if (s_Kit->m_fHitPoints > 0.f) {
                s_Npc.m_Health = std::clamp(s_Actor->m_fCurrentHitPoints / s_Kit->m_fHitPoints * 100.f, 0.f, 100.f);
            }
        }

        if (Globals::BehaviorService) {
            if (const SBehaviorBase* s_Behavior = Globals::BehaviorService->m_aBehaviorStates[i].m_pCurrentBehavior) {
                s_Npc.m_Behavior = static_cast<int>(s_Behavior->eBehaviorType);
            }
        }

        s_Npc.m_ViewAngle = HorizontalAngleDegrees(p_World.m_CameraForward, p_World.m_CameraPos, s_Pos);

        if (s_Target) {
            lw::TargetSnapshot s_TargetInfo;
            s_TargetInfo.m_Key = s_Npc.m_Key;
            s_TargetInfo.m_Name = lw::LooksLikePersonName(s_Npc.m_Name) ? s_Npc.m_Name : lw::DisplayName(s_Npc);
            s_TargetInfo.m_Pos = s_Pos;
            s_TargetInfo.m_Alive = s_Npc.m_Alive;
            p_World.m_Targets.push_back(std::move(s_TargetInfo));
        }

        if (s_Distance <= c_DetailRadius) {
            p_World.m_Npcs.push_back(std::move(s_Npc));
        }
    }

    std::sort(p_World.m_Npcs.begin(), p_World.m_Npcs.end(), [](const lw::NpcSnapshot& a, const lw::NpcSnapshot& b) {
        return a.m_Distance < b.m_Distance;
    });

    p_World.m_InLevel = true;
    return true;
}

float LivingWorld::RadiusFor(lw::VoiceVolume p_Volume, const lw::Settings& p_Settings) const {
    switch (p_Volume) {
        case lw::VoiceVolume::Whisper: return p_Settings.m_RadiusWhisper;
        case lw::VoiceVolume::Shout: return p_Settings.m_RadiusShout;
        default: return p_Settings.m_RadiusNormal;
    }
}

std::vector<uint64_t> LivingWorld::HearersAround(const lw::Vec3& p_Pos, float p_Radius, uint64_t p_Exclude) const {
    std::vector<uint64_t> s_Result;

    for (const auto& s_Npc : m_World.m_Npcs) {
        if (s_Npc.m_Key != p_Exclude && s_Npc.CanTalk() && lw::Distance(s_Npc.m_Pos, p_Pos) <= p_Radius) {
            s_Result.push_back(s_Npc.m_Key);
        }
    }

    return s_Result;
}
