#include "Reactions.h"

#include "Text.h"

#include <cmath>
#include <cstring>
#include <iterator>

namespace lw {
    namespace {
        template <size_t N>
        const char* Pick(const char* const (&p_Lines)[N], uint64_t p_Seed) {
            return p_Lines[static_cast<size_t>((p_Seed * 2654435761ull) >> 11) % N];
        }
    }

    std::string ReactionLine(
        ViolenceKind p_Kind, ReactionRole p_Role, bool p_Guard, bool p_Killed, uint64_t p_Seed, Language p_Language
    ) {
        const bool s_De = p_Language == Language::German;

        // --- Selbst getroffen ---------------------------------------------------------------------------------------
        if (p_Role == ReactionRole::Victim) {
            switch (p_Kind) {
                case ViolenceKind::Gunshot: {
                    static const char* const s_De_[] = {
                        "Aaah! Ich bin getroffen!", "Mein Arm... er hat auf mich geschossen!", "Hilfe! Ich blute!",
                        "Argh! Nicht schießen, bitte!", "Ich bin angeschossen! Hilfe!",
                    };
                    static const char* const s_En_[] = {
                        "Aaah! I'm hit!", "My arm... he shot me!", "Help! I'm bleeding!", "Argh! Don't shoot, please!", "I've been shot! Help!",
                    };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
                case ViolenceKind::Stab: {
                    static const char* const s_De_[] = {
                        "Aah! Er hat ein Messer!", "Hilfe... ich wurde gestochen!", "Nein! Mein Bauch...!", "Er sticht mich ab! Hilfe!",
                    };
                    static const char* const s_En_[] = {
                        "Aah! He's got a knife!", "Help... I've been stabbed!", "No! My stomach...!", "He's stabbing me! Help!",
                    };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
                default: {
                    static const char* const s_De_[] = {
                        "Aua! Hey!", "Lass mich los!", "Was soll das?! Hilfe!", "Uff... spinnst du?!",
                    };
                    static const char* const s_En_[] = {
                        "Ow! Hey!", "Let go of me!", "What the hell?! Help!", "Oof... are you crazy?!",
                    };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
            }
        }

        // --- Nur gehoert ---------------------------------------------------------------------------------------------
        if (p_Role == ReactionRole::Earwitness) {
            if (p_Guard) {
                static const char* const s_De_[] = {
                    "Schüsse! Woher kam das?", "Das war ein Schuss! Alle wachsam!", "Schüsse gehört – ich seh nach!",
                };
                static const char* const s_En_[] = {
                    "Gunshots! Where did that come from?", "That was a shot! Stay alert!", "Shots fired – I'm checking it out!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }

            static const char* const s_De_[] = {
                "Was war das?! Ein Schuss?", "Oh Gott, war das ein Schuss?", "Habt ihr das gehört?!", "Das klang wie eine Waffe...",
            };
            static const char* const s_En_[] = {
                "What was that?! A gunshot?", "Oh God, was that a shot?", "Did you hear that?!", "That sounded like a gun...",
            };
            return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
        }

        // --- Direkt gesehen ------------------------------------------------------------------------------------------
        if (p_Guard) {
            if (p_Killed) {
                static const char* const s_De_[] = {
                    "Mann am Boden! Er hat ihn umgebracht!", "Mord! Keine Bewegung!", "Verdammt, er ist tot! Verstärkung!",
                };
                static const char* const s_En_[] = {
                    "Man down! He killed him!", "Murder! Don't move!", "Damn, he's dead! Backup!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }

            switch (p_Kind) {
                case ViolenceKind::Gunshot: {
                    static const char* const s_De_[] = { "Schüsse gefallen! Waffe fallen lassen!", "Er schießt! Deckung!", "Schütze! Sofort die Waffe runter!" };
                    static const char* const s_En_[] = { "Shots fired! Drop the weapon!", "He's shooting! Take cover!", "Shooter! Drop it, now!" };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
                case ViolenceKind::Stab: {
                    static const char* const s_De_[] = { "Messer! Lass das Messer fallen!", "Er hat jemanden niedergestochen!", "Klinge weg, sofort!" };
                    static const char* const s_En_[] = { "Knife! Drop the knife!", "He stabbed someone!", "Drop the blade, now!" };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
                case ViolenceKind::Takedown: {
                    static const char* const s_De_[] = { "Hey! Lass ihn los!", "Was machst du da mit ihm?!", "Er hat ihn ausgeschaltet! Halt!" };
                    static const char* const s_En_[] = { "Hey! Let him go!", "What are you doing to him?!", "He knocked him out! Stop!" };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
                default: {
                    static const char* const s_De_[] = { "Hey! Aufhören!", "Schlägerei! Auseinander!", "Finger weg von ihm!" };
                    static const char* const s_En_[] = { "Hey! Stop that!", "Fight! Break it up!", "Hands off him!" };
                    return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
                }
            }
        }

        if (p_Killed) {
            static const char* const s_De_[] = {
                "Oh mein Gott! Er hat ihn getötet!", "Neeein! Hilfe! Mord!", "Er ist tot! Er ist tot!", "Lauft! Er bringt alle um!",
            };
            static const char* const s_En_[] = {
                "Oh my God! He killed him!", "Nooo! Help! Murder!", "He's dead! He's dead!", "Run! He's killing everyone!",
            };
            return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
        }

        switch (p_Kind) {
            case ViolenceKind::Gunshot: {
                static const char* const s_De_[] = {
                    "Er hat eine Waffe! Lauft!", "Schüsse! Alle runter!", "Oh Gott, er hat geschossen!", "Hilfe! Ruft die Polizei!",
                };
                static const char* const s_En_[] = {
                    "He's got a gun! Run!", "Shots! Everybody down!", "Oh God, he fired!", "Help! Call the police!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            case ViolenceKind::Stab: {
                static const char* const s_De_[] = {
                    "Er hat ein Messer! Hilfe!", "Er hat ihn niedergestochen!", "Blut! Überall Blut!", "Holt einen Arzt!",
                };
                static const char* const s_En_[] = {
                    "He's got a knife! Help!", "He stabbed him!", "Blood! There's blood everywhere!", "Get a doctor!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            case ViolenceKind::Takedown: {
                static const char* const s_De_[] = {
                    "Was macht der da?! Hilfe!", "Er hat ihn gewürgt!", "Wachen! Hier wird jemand angegriffen!",
                };
                static const char* const s_En_[] = {
                    "What is he doing?! Help!", "He choked him!", "Guards! Someone's being attacked!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            default: {
                static const char* const s_De_[] = { "Eine Schlägerei! Hilfe!", "Hört auf! Wachen!", "Was ist denn hier los?!" };
                static const char* const s_En_[] = { "A fight! Help!", "Stop it! Guards!", "What's going on here?!" };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
        }
    }

    std::string ExperienceText(
        ViolenceKind p_Kind, ReactionRole p_Role, bool p_Killed, const std::string& p_VictimName, Language p_Language
    ) {
        const bool s_De = p_Language == Language::German;
        const std::string s_Victim = p_VictimName.empty() ? (s_De ? "jemanden" : "someone") : p_VictimName;

        if (p_Role == ReactionRole::Victim) {
            switch (p_Kind) {
                case ViolenceKind::Gunshot:
                    return s_De ? "Diese Person hat auf DICH geschossen – du bist angeschossen, hast Schmerzen und Todesangst."
                                : "This person SHOT YOU – you are wounded, in pain and terrified.";
                case ViolenceKind::Stab:
                    return s_De ? "Diese Person hat DICH mit einem Messer gestochen – du blutest, hast Schmerzen und Todesangst."
                                : "This person STABBED YOU with a knife – you are bleeding, in pain and terrified.";
                default:
                    return s_De ? "Diese Person hat DICH tätlich angegriffen. Du bist geschockt und wütend."
                                : "This person physically attacked YOU. You are shocked and angry.";
            }
        }

        if (p_Role == ReactionRole::Earwitness) {
            return s_De ? "Du hast vorhin in der Nähe Schüsse gehört und bist beunruhigt."
                        : "You heard gunshots nearby earlier and you are worried.";
        }

        switch (p_Kind) {
            case ViolenceKind::Gunshot:
                return s_De ? "Du hast gesehen, wie diese Person auf " + s_Victim + (p_Killed ? " geschossen und ihn/sie getötet hat." : " geschossen hat.")
                            : "You saw this person shoot " + s_Victim + (p_Killed ? " dead." : ".");
            case ViolenceKind::Stab:
                return s_De ? "Du hast gesehen, wie diese Person " + s_Victim + (p_Killed ? " erstochen hat." : " mit einem Messer niedergestochen hat.")
                            : "You saw this person " + std::string(p_Killed ? "stab " + s_Victim + " to death." : "stab " + s_Victim + ".");
            case ViolenceKind::Takedown:
                return s_De ? "Du hast gesehen, wie diese Person " + s_Victim + " überwältigt und bewusstlos gemacht hat."
                            : "You saw this person overpower " + s_Victim + " and knock them out.";
            default:
                return s_De ? "Du hast gesehen, wie diese Person " + s_Victim + (p_Killed ? " umgebracht hat." : " angegriffen hat.")
                            : "You saw this person " + std::string(p_Killed ? "kill " : "attack ") + s_Victim + ".";
        }
    }

    AttitudeShock ShockFor(ViolenceKind p_Kind, ReactionRole p_Role, bool p_Guard, bool p_Killed) {
        if (p_Role == ReactionRole::Victim) {
            return { -60, 60, p_Guard ? 45 : 80 };
        }

        if (p_Role == ReactionRole::Earwitness) {
            return { -5, 15, p_Guard ? 5 : 25 };
        }

        const bool s_Lethal = p_Killed || p_Kind == ViolenceKind::Gunshot || p_Kind == ViolenceKind::Stab;
        return { s_Lethal ? -70 : -40, s_Lethal ? 80 : 50, p_Guard ? (s_Lethal ? 25 : 10) : (s_Lethal ? 85 : 45) };
    }

    float SightRange(ViolenceKind p_Kind) {
        switch (p_Kind) {
            case ViolenceKind::Gunshot: return 18.f;
            case ViolenceKind::ShotHeard: return 0.f;
            default: return 10.f;
        }
    }

    float HearingRange(ViolenceKind p_Kind) {
        switch (p_Kind) {
            case ViolenceKind::Gunshot:
            case ViolenceKind::ShotHeard: return 40.f;
            default: return 10.f; // Schreie bei Messer/Nahkampf
        }
    }

    BodyZone EstimateHitZone(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_VictimFeet) {
        // Punkt auf dem Strahl, der der senkrechten Koerperachse des Opfers am naechsten kommt (in der Ebene).
        const float s_DirXY = p_RayDir.x * p_RayDir.x + p_RayDir.y * p_RayDir.y;

        if (s_DirXY < 1e-6f) {
            return BodyZone::Unknown;
        }

        const float s_T = ((p_VictimFeet.x - p_RayOrigin.x) * p_RayDir.x + (p_VictimFeet.y - p_RayOrigin.y) * p_RayDir.y) / s_DirXY;

        if (s_T < 0.f) {
            return BodyZone::Unknown;
        }

        const float s_X = p_RayOrigin.x + p_RayDir.x * s_T;
        const float s_Y = p_RayOrigin.y + p_RayDir.y * s_T;
        const float s_Z = p_RayOrigin.z + p_RayDir.z * s_T;
        const float s_Height = s_Z - p_VictimFeet.z;
        const float s_Side = std::sqrt((s_X - p_VictimFeet.x) * (s_X - p_VictimFeet.x) + (s_Y - p_VictimFeet.y) * (s_Y - p_VictimFeet.y));

        if (s_Height < -0.2f || s_Height > 2.2f) {
            return BodyZone::Unknown;
        }

        if (s_Height >= 1.47f) {
            return BodyZone::Head;
        }

        if (s_Height < 0.95f) {
            return BodyZone::Leg;
        }

        return s_Side > 0.22f ? BodyZone::Arm : BodyZone::Torso;
    }

    const char* BodyZoneName(BodyZone p_Zone, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        switch (p_Zone) {
            case BodyZone::Head: return s_De ? "Kopf" : "head";
            case BodyZone::Torso: return s_De ? "Oberkörper" : "torso";
            case BodyZone::Arm: return s_De ? "Arm" : "arm";
            case BodyZone::Leg: return s_De ? "Bein" : "leg";
            default: return s_De ? "unbekannt" : "unknown";
        }
    }

    std::string WoundLine(BodyZone p_Zone, ViolenceKind p_Kind, uint64_t p_Seed, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        if (p_Kind == ViolenceKind::Stab) {
            static const char* const s_De_[] = {
                "Aaargh! Er hat mich gestochen... ich blute!", "Nein... mein Bauch... Hilfe!", "Das Messer... oh Gott, ich blute!",
            };
            static const char* const s_En_[] = {
                "Aaargh! He stabbed me... I'm bleeding!", "No... my stomach... help!", "The knife... oh God, I'm bleeding!",
            };
            return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
        }

        switch (p_Zone) {
            case BodyZone::Leg: {
                static const char* const s_De_[] = {
                    "Aaah! Mein Bein! Ich kann nicht mehr laufen!", "Argh, mein Knie! Er hat mir ins Bein geschossen!", "Mein Bein... ich komm nicht weg!",
                };
                static const char* const s_En_[] = {
                    "Aaah! My leg! I can't walk!", "Argh, my knee! He shot me in the leg!", "My leg... I can't get away!",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            case BodyZone::Arm: {
                static const char* const s_De_[] = { "Aah! Mein Arm!", "Argh, meine Schulter! Hilfe!", "Mein Arm... er blutet!" };
                static const char* const s_En_[] = { "Aah! My arm!", "Argh, my shoulder! Help!", "My arm... it's bleeding!" };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            case BodyZone::Torso: {
                static const char* const s_De_[] = {
                    "Ugh... ich bin getroffen... ich krieg keine Luft...", "Aaah! Mein Bauch... Hilfe, ich blute!", "Nein... nein... das ist so viel Blut...",
                };
                static const char* const s_En_[] = {
                    "Ugh... I'm hit... I can't breathe...", "Aaah! My stomach... help, I'm bleeding!", "No... no... there's so much blood...",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            default:
                return ReactionLine(p_Kind, ReactionRole::Victim, false, false, p_Seed, p_Language);
        }
    }

    std::string MoanLine(WoundState p_State, uint64_t p_Seed, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        switch (p_State) {
            case WoundState::Limping: {
                static const char* const s_De_[] = { "Ah... verdammt, mein Bein...", "Hilfe! Ich kann kaum laufen!", "Bleib weg von mir!", "Ngh... jeder Schritt tut weh..." };
                static const char* const s_En_[] = { "Ah... damn, my leg...", "Help! I can barely walk!", "Stay away from me!", "Ngh... every step hurts..." };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            case WoundState::ArmHurt: {
                static const char* const s_De_[] = { "Mein Arm... ich kann ihn nicht bewegen...", "Verdammt, das tut weh...", "Ich brauche einen Arzt..." };
                static const char* const s_En_[] = { "My arm... I can't move it...", "Damn, that hurts...", "I need a doctor..." };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
            default: {
                static const char* const s_De_[] = {
                    "Hilfe... bitte... ich verblute...", "Mir ist so kalt...", "Ruft... einen Krankenwagen...", "Bitte... ich will nicht sterben...",
                    "Ngh... Hilfe...", "Bitte... lass mich...",
                };
                static const char* const s_En_[] = {
                    "Help... please... I'm bleeding out...", "I'm so cold...", "Call... an ambulance...", "Please... I don't want to die...",
                    "Ngh... help...", "Please... leave me...",
                };
                return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
            }
        }
    }

    std::string WoundStateText(WoundState p_State, BodyZone p_Zone, ViolenceKind p_Kind, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        switch (p_State) {
            case WoundState::Limping:
                return s_De ? "Dein Bein ist angeschossen. Du kannst kaum noch laufen, humpelst und hast starke Schmerzen."
                            : "Your leg has been shot. You can barely walk, you limp and you are in severe pain.";
            case WoundState::ArmHurt:
                return s_De ? "Dein Arm ist angeschossen und blutet, du kannst ihn kaum bewegen."
                            : "Your arm has been shot and is bleeding, you can hardly move it.";
            default:
                return p_Kind == ViolenceKind::Stab
                    ? (s_De ? "Du wurdest niedergestochen, liegst am Boden, kriechst und verblutest langsam. Du hast Todesangst und kannst nur noch keuchen und flehen."
                            : "You were stabbed, you are lying on the ground, crawling and slowly bleeding out. You are terrified and can only gasp and plead.")
                    : (s_De ? "Du bist schwer angeschossen, liegst am Boden und verblutest. Du kannst kaum sprechen, nur keuchen, flehen und um Hilfe rufen."
                            : "You are badly shot, lying on the ground and bleeding out. You can barely speak, only gasp, plead and call for help.");
        }
    }

    bool IsThreat(const std::string& p_Text) {
        std::string s_Lower = ToLowerUtf8(p_Text);

        // Entwarnungen zuerst herausnehmen ("ich habe keine Waffe", "unbewaffnet").
        for (const char* s_Calm : { "keine waffe", "unbewaffnet", "nicht bewaffnet", "ich lege die waffe", "ich leg die waffe",
                                    "ich nehme die waffe runter", "no gun", "unarmed", "not armed", "putting the gun down",
                                    "put my gun down" }) {
            for (size_t s_Pos; (s_Pos = s_Lower.find(s_Calm)) != std::string::npos;) {
                s_Lower.erase(s_Pos, std::strlen(s_Calm));
            }
        }

        static const char* const c_Words[] = {
            // Deutsch
            "waffe", "pistole", "gewehr", "knarre", "messer", "bewaffnet", "erschieß", "erschiess", "abknall", "abstech",
            "umbringen", "bring dich um", "bringe dich um", "bring ich dich um", "bringe ich dich um", "töte dich",
            "töte ich dich", "töten", "kill", "kopf weg", "abmurks", "mach dich kalt", "mach ich dich kalt", "erledige dich",
            "hände hoch", "keine bewegung", "auf den boden", "auf die knie", "oder du stirbst", "dann bist du tot",
            "du bist tot", "ich schieß", "ich schiess", "leg dich hin", "maul halten", "halt die fresse",
            // Englisch
            "gun", "pistol", "knife", "shoot you", "kill you", "hands up", "on the ground", "don't move", "you're dead",
        };

        for (const char* s_Word : c_Words) {
            if (s_Lower.find(s_Word) != std::string::npos) {
                return true;
            }
        }

        return false;
    }

    std::string ThreatReactionLine(bool p_Armed, int p_ArmedKills, uint64_t p_Seed, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        // Bewaffnete halten nur stand, solange 47 noch keinen Ruf als Killer hat.
        if (p_Armed && p_ArmedKills < 3) {
            static const char* const s_De_[] = { "Ganz ruhig! Keine Dummheiten!", "Ich warne dich – mach keinen Fehler!", "Leg sofort die Waffe weg!" };
            static const char* const s_En_[] = { "Easy! Don't do anything stupid!", "I'm warning you – don't make a mistake!", "Put the weapon down, now!" };
            return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
        }

        static const char* const s_De_[] = {
            "Nein, bitte! Bitte tun Sie mir nichts!", "Ich mach alles, was Sie wollen – nur nicht schießen!",
            "Bitte... ich hab Familie... bitte nicht!", "Okay, okay! Ich hab nichts gesehen, ich schwör's!",
            "Bitte, bitte, lassen Sie mich leben!",
        };
        static const char* const s_En_[] = {
            "No, please! Please don't hurt me!", "I'll do whatever you want – just don't shoot!",
            "Please... I have a family... please don't!", "Okay, okay! I didn't see anything, I swear!",
            "Please, please, let me live!",
        };
        return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
    }

    std::string DisarmedLine(uint64_t p_Seed, Language p_Language) {
        const bool s_De = p_Language == Language::German;
        static const char* const s_De_[] = {
            "Meine Waffe! Nein... nicht schießen, bitte!", "Okay, okay, ich ergebe mich! Bitte töten Sie mich nicht!",
            "Ich bin unbewaffnet! Bitte... ich will nicht sterben!", "Bitte, ich hab Kinder! Nicht schießen!",
        };
        static const char* const s_En_[] = {
            "My gun! No... don't shoot, please!", "Okay, okay, I surrender! Please don't kill me!",
            "I'm unarmed! Please... I don't want to die!", "Please, I have kids! Don't shoot!",
        };
        return s_De ? Pick(s_De_, p_Seed) : Pick(s_En_, p_Seed);
    }

    std::string ReputationText(int p_ArmedKills, int p_CivilianKills, bool p_ListenerArmed, Language p_Language) {
        if (p_ArmedKills <= 0 && p_CivilianKills <= 0) {
            return {};
        }

        const bool s_De = p_Language == Language::German;
        std::string s_Text;

        if (p_ArmedKills > 0) {
            s_Text = s_De
                ? "Alle reden darüber: Ein Glatzkopf hat heute schon " + std::to_string(p_ArmedKills) +
                  " bewaffnete Sicherheitsleute/Polizisten/Soldaten getötet. "
                : "Everyone is talking about it: a bald man has already killed " + std::to_string(p_ArmedKills) +
                  " armed guards/police/soldiers today. ";
        }

        if (p_CivilianKills > 0) {
            s_Text += s_De ? "Außerdem " + std::to_string(p_CivilianKills) + " Zivilisten. "
                           : "Also " + std::to_string(p_CivilianKills) + " civilians. ";
        }

        // Je mehr Bewaffnete tot sind, desto mehr Respekt bzw. Angst.
        if (p_ListenerArmed) {
            if (p_ArmedKills >= 5) {
                s_Text += s_De ? "Du hast riesige Angst vor ihm. Gegen ihn hast du keine Chance – lieber aufgeben und um dein Leben bitten als kämpfen."
                               : "You are terrified of him. You stand no chance – better surrender and beg for your life than fight.";
            }
            else if (p_ArmedKills >= 2) {
                s_Text += s_De ? "Du hast großen Respekt vor ihm, deine Kollegen sind tot. Du willst nicht der Nächste sein."
                               : "You have great respect for him, your colleagues are dead. You don't want to be next.";
            }
            else {
                s_Text += s_De ? "Du bist nervös und vorsichtig." : "You are nervous and careful.";
            }
        }
        else {
            s_Text += s_De ? "Du hast Todesangst vor diesem Mann." : "You are terrified of this man.";
        }

        return s_Text;
    }

    const char* ReactionEmotion(ReactionRole p_Role, bool p_Guard) {
        if (p_Role == ReactionRole::Victim) return "panicked";
        if (p_Guard) return "angry";
        return p_Role == ReactionRole::Earwitness ? "nervous" : "panicked";
    }
}
