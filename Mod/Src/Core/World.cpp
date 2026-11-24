#include "World.h"

#include "Text.h"

#include <cmath>
#include <utility>

namespace lw {
    namespace {
        // Werte aus ECompiledBehaviorType (Glacier/Enums.h des SDK).
        enum Bt : int {
            BT_PlayAct = 13, BT_ConfiguredAct = 14, BT_PlayReaction = 15, BT_SimpleReaction = 16,
            BT_SituationAct = 17, BT_SituationApproach = 18, BT_SituationGetHelp = 19, BT_SituationFace = 20,
            BT_SituationConversation = 21, BT_Holster = 22, BT_SpeakWait = 23, BT_PlayConversation = 33,
            BT_PlayAnimation = 34, BT_MoveToLocation = 35, BT_MoveToTargetKnownPosition = 36,
            BT_MoveToTargetActualPosition = 37, BT_MoveToInteraction = 38, BT_MoveToNPC = 39,
            BT_FollowTargetKnownPosition = 40, BT_FollowTargetActualPosition = 41, BT_PickUpItem = 42,
            BT_Search = 45, BT_LimitedSearch = 46, BT_MoveTo = 47, BT_Reposition = 48, BT_SituationMoveTo = 49,
            BT_AmbientWalk = 52, BT_AmbientStand = 53, BT_CrowdAmbientStand = 54, BT_AmbientItemUse = 55,
            BT_AmbientLook = 56, BT_Act = 57, BT_Patrol = 58, BT_MoveToPosition = 59, BT_AlertedStand = 60,
            BT_AttentionToPerson = 62, BT_StunnedByFlashGrenade = 63, BT_CuriousIdle = 64,
            BT_InvestigateWeapon = 65, BT_DeliverWeapon = 66, BT_RecoverUnconscious = 67, BT_GetOutfit = 68,
            BT_RadioCall = 69, BT_EscortOut = 70, BT_StashItem = 71, BT_CautiousSearchPosition = 72,
            BT_LockdownWarning = 73, BT_WakeUpUnconscious = 74, BT_DeadBodyInvestigate = 75,
            BT_GuardDeadBody = 76, BT_DragDeadBody = 77, BT_CuriousBystander = 78, BT_DeadBodyBystander = 79,
            BT_StandOffArrest = 80, BT_StandOffReposition = 81, BT_StandAndAim = 82, BT_CloseCombat = 83,
            BT_MoveToCloseCombat = 84, BT_MoveAwayFromCloseCombat = 85, BT_CoverFightSeasonTwo = 86,
            BT_ShootFromPosition = 87, BT_StandAndShoot = 88, BT_CheckLastPosition = 89, BT_ProtoSearchIdle = 90,
            BT_ProtoApproachSearchArea = 91, BT_ProtoSearchPosition = 92, BT_ShootTarget = 93,
            BT_TriggerAlarm = 94, BT_MoveInCover = 95, BT_MoveToCover = 96, BT_HomeAttackOrigin = 97, BT_Shoot = 98,
            BT_Aim = 99, BT_MoveToRandomNeighbourNode = 100, BT_MoveToRandomNeighbourNodeAiming = 101,
            BT_MoveToAndPlayCombatPositionAct = 102, BT_MoveToAimingAndPlayCombatPositionAct = 103,
            BT_PlayJumpyReaction = 104, BT_JumpyInvestigation = 105, BT_AgitatedPatrol = 106,
            BT_AgitatedGuard = 107, BT_HeroEscort = 108, BT_Escort = 109, BT_ControlledFormationMove = 110,
            BT_EscortSearch = 111, BT_LeadEscort = 112, BT_LeadEscort2 = 113, BT_AimReaction = 114,
            BT_FollowHitman = 115, BT_Scared = 117, BT_Flee = 118, BT_AgitatedBystander = 119,
            BT_SentryFrisk = 120, BT_SentryIdle = 121, BT_SentryWarning = 122, BT_SentryCheckItem = 123,
            BT_VIPScared = 124, BT_VIPSafeRoomTrespasser = 125, BT_DefendVIP = 126, BT_CautiousVIP = 127,
            BT_CautiousGuardVIP = 128, BT_Conversation = 136, BT_RunToHelp = 137, BT_WaitForDialog = 138,
        };

        struct BehaviorText {
            int m_Id;
            const char* m_Name;
            const char* m_De;
            const char* m_En;
        };

        constexpr BehaviorText c_Behaviors[] = {
            { BT_AmbientWalk, "AmbientWalk", "schlenderst herum", "are strolling around" },
            { BT_AmbientStand, "AmbientStand", "stehst entspannt herum", "are standing around, relaxed" },
            { BT_CrowdAmbientStand, "CrowdAmbientStand", "stehst in der Menge", "are standing in the crowd" },
            { BT_AmbientItemUse, "AmbientItemUse", "bist mit etwas beschäftigt", "are busy with something" },
            { BT_AmbientLook, "AmbientLook", "schaust dich um", "are looking around" },
            { BT_Act, "Act", "gehst deiner Arbeit nach", "are doing your job" },
            { BT_PlayAct, "PlayAct", "gehst deiner Tätigkeit nach", "are going about your business" },
            { BT_ConfiguredAct, "ConfiguredAct", "gehst deiner Tätigkeit nach", "are going about your business" },
            { BT_Patrol, "Patrol", "bist auf Patrouille", "are on patrol" },
            { BT_PlayConversation, "PlayConversation", "unterhältst dich gerade mit jemandem", "are chatting with someone" },
            { BT_Conversation, "Conversation", "unterhältst dich gerade mit jemandem", "are chatting with someone" },
            { BT_SituationConversation, "SituationConversation", "redest mit Kollegen über einen Vorfall", "are discussing an incident with colleagues" },
            { BT_MoveTo, "MoveTo", "bist unterwegs", "are on your way somewhere" },
            { BT_MoveToLocation, "MoveToLocation", "bist unterwegs", "are on your way somewhere" },
            { BT_MoveToPosition, "MoveToPosition", "bist unterwegs", "are on your way somewhere" },
            { BT_AlertedStand, "AlertedStand", "stehst alarmiert da und siehst dich um", "are standing alert, looking around" },
            { BT_AttentionToPerson, "AttentionToPerson", "beobachtest die Person misstrauisch", "are watching the person suspiciously" },
            { BT_CuriousIdle, "CuriousIdle", "bist neugierig geworden", "have become curious" },
            { BT_CuriousBystander, "CuriousBystander", "schaust neugierig zu, was los ist", "are curiously watching what's going on" },
            { BT_InvestigateWeapon, "InvestigateWeapon", "untersuchst eine herumliegende Waffe", "are investigating a weapon lying around" },
            { BT_DeliverWeapon, "DeliverWeapon", "bringst eine gefundene Waffe weg", "are bringing a found weapon away" },
            { BT_RadioCall, "RadioCall", "funkst gerade", "are on the radio" },
            { BT_EscortOut, "EscortOut", "führst jemanden aus dem Sperrbereich hinaus", "are escorting someone out of a restricted area" },
            { BT_Search, "Search", "suchst jemanden", "are searching for someone" },
            { BT_LimitedSearch, "LimitedSearch", "suchst die Umgebung ab", "are searching the area" },
            { BT_CautiousSearchPosition, "CautiousSearchPosition", "durchsuchst vorsichtig die Umgebung", "are cautiously searching the area" },
            { BT_CheckLastPosition, "CheckLastPosition", "prüfst, wo du jemanden zuletzt gesehen hast", "are checking where you last saw someone" },
            { BT_ProtoSearchIdle, "ProtoSearchIdle", "suchst jemanden", "are searching for someone" },
            { BT_ProtoSearchPosition, "ProtoSearchPosition", "suchst jemanden", "are searching for someone" },
            { BT_LockdownWarning, "LockdownWarning", "warnst vor einer Abriegelung", "are warning about a lockdown" },
            { BT_DeadBodyInvestigate, "DeadBodyInvestigate", "untersuchst eine Leiche", "are examining a dead body" },
            { BT_GuardDeadBody, "GuardDeadBody", "bewachst eine Leiche", "are guarding a dead body" },
            { BT_DeadBodyBystander, "DeadBodyBystander", "stehst geschockt bei einer Leiche", "are standing in shock near a body" },
            { BT_RecoverUnconscious, "RecoverUnconscious", "kümmerst dich um eine bewusstlose Person", "are tending to an unconscious person" },
            { BT_WakeUpUnconscious, "WakeUpUnconscious", "kommst gerade wieder zu dir", "are just coming round" },
            { BT_StandOffArrest, "StandOffArrest", "zielst mit deiner Waffe auf die Person und willst sie festnehmen", "are aiming your weapon at the person, trying to arrest them" },
            { BT_StandOffReposition, "StandOffReposition", "gehst mit gezogener Waffe in Position", "are repositioning with your weapon drawn" },
            { BT_StandAndAim, "StandAndAim", "zielst mit deiner Waffe auf die Person", "are aiming your weapon at the person" },
            { BT_Aim, "Aim", "zielst mit deiner Waffe auf die Person", "are aiming your weapon at the person" },
            { BT_AimReaction, "AimReaction", "hast Angst, weil jemand eine Waffe auf dich richtet", "are scared because a gun is pointed at you" },
            { BT_CloseCombat, "CloseCombat", "bist im Nahkampf", "are in a close-quarters fight" },
            { BT_MoveToCloseCombat, "MoveToCloseCombat", "stürmst auf die Person los", "are rushing at the person" },
            { BT_CoverFightSeasonTwo, "CoverFight", "bist in einem Feuergefecht in Deckung", "are in a firefight, in cover" },
            { BT_ShootFromPosition, "ShootFromPosition", "bist in einem Feuergefecht", "are in a firefight" },
            { BT_StandAndShoot, "StandAndShoot", "bist in einem Feuergefecht", "are in a firefight" },
            { BT_ShootTarget, "ShootTarget", "bist in einem Feuergefecht", "are in a firefight" },
            { BT_Shoot, "Shoot", "bist in einem Feuergefecht", "are in a firefight" },
            { BT_MoveToCover, "MoveToCover", "suchst Deckung", "are moving to cover" },
            { BT_MoveInCover, "MoveInCover", "bewegst dich in Deckung", "are moving in cover" },
            { BT_TriggerAlarm, "TriggerAlarm", "willst den Alarm auslösen", "are about to trigger the alarm" },
            { BT_PlayJumpyReaction, "PlayJumpyReaction", "bist zusammengezuckt", "have just flinched" },
            { BT_JumpyInvestigation, "JumpyInvestigation", "schaust nervös nach, was das war", "are nervously checking what that was" },
            { BT_AgitatedPatrol, "AgitatedPatrol", "patrouillierst angespannt", "are patrolling, on edge" },
            { BT_AgitatedGuard, "AgitatedGuard", "stehst angespannt Wache", "are standing guard, on edge" },
            { BT_AgitatedBystander, "AgitatedBystander", "bist aufgeregt und beunruhigt", "are agitated and worried" },
            { BT_Escort, "Escort", "eskortierst jemanden", "are escorting someone" },
            { BT_HeroEscort, "HeroEscort", "führst die Person irgendwohin", "are leading the person somewhere" },
            { BT_LeadEscort, "LeadEscort", "führst eine Eskorte", "are leading an escort" },
            { BT_FollowHitman, "FollowHitman", "folgst der Person", "are following the person" },
            { BT_Scared, "Scared", "hast Todesangst und kauerst dich hin", "are terrified and cowering" },
            { BT_VIPScared, "VIPScared", "hast Todesangst", "are terrified" },
            { BT_Flee, "Flee", "fliehst in Panik", "are fleeing in panic" },
            { BT_SentryIdle, "SentryIdle", "kontrollierst den Zugang an einem Kontrollpunkt", "are guarding a checkpoint" },
            { BT_SentryWarning, "SentryWarning", "verweigerst der Person den Zutritt am Kontrollpunkt", "are refusing the person entry at the checkpoint" },
            { BT_SentryFrisk, "SentryFrisk", "durchsuchst die Person nach Waffen", "are frisking the person for weapons" },
            { BT_SentryCheckItem, "SentryCheckItem", "kontrollierst einen Gegenstand", "are checking an item" },
            { BT_DefendVIP, "DefendVIP", "beschützt deine Schutzperson", "are protecting your VIP" },
            { BT_CautiousVIP, "CautiousVIP", "bist als wichtige Person vorsichtig geworden", "have become cautious" },
            { BT_CautiousGuardVIP, "CautiousGuardVIP", "bewachst aufmerksam deine Schutzperson", "are watchfully guarding your VIP" },
            { BT_RunToHelp, "RunToHelp", "rennst los, um Hilfe zu holen", "are running to get help" },
            { BT_SituationGetHelp, "SituationGetHelp", "holst Hilfe", "are getting help" },
            { BT_StunnedByFlashGrenade, "Stunned", "bist geblendet und benommen", "are dazed and blinded" },
            { BT_FollowTargetKnownPosition, "FollowTarget", "verfolgst jemanden", "are following someone" },
            { BT_FollowTargetActualPosition, "FollowTarget", "verfolgst jemanden", "are following someone" },
        };

        const BehaviorText* FindBehavior(int p_Behavior) {
            for (const auto& s_Entry : c_Behaviors) {
                if (s_Entry.m_Id == p_Behavior) {
                    return &s_Entry;
                }
            }

            return nullptr;
        }

        struct LocationText {
            const char* m_Key; // Teilstring der Location-ID oder des Szenenpfads (Kleinbuchstaben)
            const char* m_De;
            const char* m_En;
        };

        // Reihenfolge wichtig: spezifische Varianten vor den allgemeinen.
        constexpr LocationText c_Locations[] = {
            { "ica_facility", "ICA-Trainingsanlage (geheimes Ausbildungsgelände)", "ICA training facility" },
            { "polarbear", "ICA-Trainingsanlage (geheimes Ausbildungsgelände)", "ICA training facility" },
            { "paris", "Paris – Modenschau im Palais de Walewska", "Paris – fashion show at the Palais de Walewska" },
            { "movieset", "Sapienza – Filmset am Meer", "Sapienza – seaside film set" },
            { "coastaltown", "Sapienza – italienisches Küstenstädtchen, Villa Caruso", "Sapienza – Italian coastal town, Villa Caruso" },
            { "marrakech_night", "Marrakesch bei Nacht", "Marrakesh at night" },
            { "marrakech", "Marrakesch – Konsulat und Schule am Basar", "Marrakesh – consulate and school by the bazaar" },
            { "bangkok", "Bangkok – Luxushotel Himmapan am Fluss", "Bangkok – Himmapan luxury hotel by the river" },
            { "colorado", "Colorado – Farm einer Miliz", "Colorado – militia farm" },
            { "hokkaido", "Hokkaido – Luxusklinik GAMA in den Bergen", "Hokkaido – GAMA luxury clinic in the mountains" },
            { "sheep", "Hawke's Bay, Neuseeland – Strandhaus bei Nacht", "Hawke's Bay, New Zealand – beach house at night" },
            { "newzealand", "Hawke's Bay, Neuseeland – Strandhaus bei Nacht", "Hawke's Bay, New Zealand – beach house at night" },
            { "flamingo", "Miami – Autorennen und Technikmesse", "Miami – car race and tech expo" },
            { "miami", "Miami – Autorennen und Technikmesse", "Miami – car race and tech expo" },
            { "hippo", "Santa Fortuna, Kolumbien – Drogenkartell im Dschungeldorf", "Santa Fortuna, Colombia – cartel village" },
            { "colombia", "Santa Fortuna, Kolumbien – Drogenkartell im Dschungeldorf", "Santa Fortuna, Colombia – cartel village" },
            { "mongoose", "Mumbai – Filmdreh und Slums", "Mumbai – film shoot and slums" },
            { "mumbai", "Mumbai – Filmdreh und Slums", "Mumbai – film shoot and slums" },
            { "skunk", "Whittleton Creek – amerikanische Vorstadt", "Whittleton Creek – American suburb" },
            { "northamerica", "Whittleton Creek – amerikanische Vorstadt", "Whittleton Creek – American suburb" },
            { "magpie", "Isle of Sgail – Zeremonie einer Geheimgesellschaft", "Isle of Sgail – secret society ceremony" },
            { "northsea", "Isle of Sgail – Zeremonie einer Geheimgesellschaft", "Isle of Sgail – secret society ceremony" },
            { "racoon", "New York – Bank in Manhattan", "New York – Manhattan bank" },
            { "greedy", "New York – Bank in Manhattan", "New York – Manhattan bank" },
            { "stingray", "Haven Island, Malediven – Luxusresort", "Haven Island, Maldives – luxury resort" },
            { "opulent", "Haven Island, Malediven – Luxusresort", "Haven Island, Maldives – luxury resort" },
            { "golden", "Dubai – Eröffnungsfeier im höchsten Wolkenkratzer", "Dubai – opening party in the tallest skyscraper" },
            { "ancestral", "Dartmoor, England – Herrenhaus der Familie Carlisle", "Dartmoor, England – Carlisle family manor" },
            { "edgy", "Berlin – Techno-Club in einem alten Kraftwerk", "Berlin – techno club in an old power plant" },
            { "wet", "Chongqing, China – regnerische Neonstadt", "Chongqing, China – rainy neon city" },
            { "elegant", "Mendoza, Argentinien – Weingut", "Mendoza, Argentina – winery" },
            { "trapped", "Karpaten – fahrender Zug", "Carpathian Mountains – moving train" },
            { "rocky", "Ambrose Island – Piraten und Miliz", "Ambrose Island – pirates and militia" },
        };
    }

    std::string BehaviorName(int p_Behavior) {
        if (const auto* s_Entry = FindBehavior(p_Behavior)) {
            return s_Entry->m_Name;
        }

        return p_Behavior < 0 ? "-" : "BT#" + std::to_string(p_Behavior);
    }

    std::string DescribeBehavior(int p_Behavior, Language p_Language) {
        if (const auto* s_Entry = FindBehavior(p_Behavior)) {
            return p_Language == Language::German ? s_Entry->m_De : s_Entry->m_En;
        }

        return p_Language == Language::German ? "gehst deinem Alltag nach" : "are going about your day";
    }

    bool IsHostileBehavior(int p_Behavior) {
        switch (p_Behavior) {
            case BT_StandOffArrest:
            case BT_StandOffReposition:
            case BT_StandAndAim:
            case BT_Aim:
            case BT_CloseCombat:
            case BT_MoveToCloseCombat:
            case BT_CoverFightSeasonTwo:
            case BT_ShootFromPosition:
            case BT_StandAndShoot:
            case BT_ShootTarget:
            case BT_Shoot:
            case BT_MoveToCover:
            case BT_MoveInCover:
            case BT_MoveToRandomNeighbourNodeAiming:
            case BT_MoveToAimingAndPlayCombatPositionAct:
            case BT_HomeAttackOrigin:
                return true;
            default:
                return false;
        }
    }

    bool IsScaredBehavior(int p_Behavior) {
        return p_Behavior == BT_Scared || p_Behavior == BT_VIPScared || p_Behavior == BT_Flee ||
            p_Behavior == BT_AimReaction || p_Behavior == BT_StunnedByFlashGrenade;
    }

    bool IsSentryBehavior(int p_Behavior) {
        return p_Behavior >= BT_SentryFrisk && p_Behavior <= BT_SentryCheckItem;
    }

    std::string DescribeTension(int p_Tension, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        switch (p_Tension) {
            case 2: return s_De ? "leicht unruhig – jemand hat etwas Seltsames bemerkt" : "slightly uneasy – someone noticed something odd";
            case 3: return s_De ? "es wird nach jemandem gesucht" : "someone is being searched for";
            case 4: return s_De ? "Alarmbereitschaft" : "on alert";
            case 5: return s_De ? "hohe Alarmbereitschaft" : "high alert";
            case 6: return s_De ? "Jagd auf einen Eindringling" : "hunting an intruder";
            case 7: return s_De ? "eine Festnahme läuft" : "an arrest is under way";
            case 8: return s_De ? "Schusswechsel / Kampf" : "firefight / combat";
            default: return s_De ? "ruhig, alles normal" : "calm, business as usual";
        }
    }

    std::string FriendlyLocation(const std::string& p_LocationId, const std::string& p_Scene, Language p_Language) {
        const std::string s_Haystack = ToLowerAscii(p_LocationId + "|" + p_Scene);

        for (const auto& s_Entry : c_Locations) {
            if (s_Haystack.find(s_Entry.m_Key) != std::string::npos) {
                return p_Language == Language::German ? s_Entry.m_De : s_Entry.m_En;
            }
        }

        if (!p_LocationId.empty()) {
            return p_LocationId;
        }

        return p_Language == Language::German ? "unbekannter Ort" : "unknown place";
    }

    std::string DescribeDirection(const Vec3& p_From, const Vec3& p_To, Language p_Language) {
        const float s_Dx = p_To.x - p_From.x;
        const float s_Dy = p_To.y - p_From.y;
        const float s_Dz = p_To.z - p_From.z;
        const bool s_De = p_Language == Language::German;

        std::string s_Result;

        if (std::sqrt(s_Dx * s_Dx + s_Dy * s_Dy) > 3.f) {
            // Winkel 0 = Norden (+Y), im Uhrzeigersinn.
            float s_Angle = std::atan2(s_Dx, s_Dy) * 57.2957795f;

            if (s_Angle < 0.f) {
                s_Angle += 360.f;
            }

            static const char* s_De8[] = { "nördlich", "nordöstlich", "östlich", "südöstlich", "südlich", "südwestlich", "westlich", "nordwestlich" };
            static const char* s_En8[] = { "north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west" };
            const int s_Sector = static_cast<int>((s_Angle + 22.5f) / 45.f) % 8;
            s_Result = s_De ? s_De8[s_Sector] : s_En8[s_Sector];
        }
        else {
            s_Result = s_De ? "ganz in der Nähe" : "very close";
        }

        if (s_Dz > 2.5f) {
            s_Result += s_De ? ", ein Stockwerk höher" : ", one floor up";
        }
        else if (s_Dz < -2.5f) {
            s_Result += s_De ? ", ein Stockwerk tiefer" : ", one floor down";
        }

        return s_Result;
    }

    bool LooksLikePersonName(const std::string& p_Name) {
        if (p_Name.size() < 3) {
            return false;
        }

        // Interne Bezeichnungen haben oft Unterstriche, Ziffern oder Woerter wie "Guard"/"NPC".
        for (const char c : p_Name) {
            if (c == '_' || (c >= '0' && c <= '9') || c == '[' || c == ':' || c == '/') {
                return false;
            }
        }

        const std::string s_Lower = ToLowerAscii(p_Name);

        for (const char* s_Generic : { "guard", "npc", "civ", "crowd", "actor", "staff", "worker", "security", "waiter", "bodyguard" }) {
            if (s_Lower.find(s_Generic) != std::string::npos) {
                return false;
            }
        }

        return p_Name.find(' ') != std::string::npos;
    }
}
