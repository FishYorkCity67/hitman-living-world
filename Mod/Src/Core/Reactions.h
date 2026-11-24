#pragma once

#include "Settings.h"
#include "World.h"

#include <cstdint>
#include <string>

namespace lw {
    // Was ist passiert? (Das Spiel verraet die Waffe nicht direkt – die Mod leitet sie aus den Umstaenden ab.)
    enum class ViolenceKind {
        Gunshot = 0,   // 47 hat kurz vorher geschossen
        Stab = 1,      // Nahkampf, waehrend 47 sichtbar eine Waffe/Klinge haelt
        Melee = 2,     // Nahkampf ohne sichtbare Waffe (Schlag, Griff)
        Takedown = 3,  // jemand wurde neben 47 bewusstlos (Wuergegriff, Schlag)
        ShotHeard = 4, // Schuss ohne Treffer
    };

    enum class ReactionRole {
        Victim,        // selbst getroffen
        Eyewitness,    // hat es direkt neben sich gesehen
        Earwitness,    // hat nur etwas gehoert
    };

    // Sofort-Reaktion ohne KI (damit Schreie ohne Verzoegerung kommen).
    std::string ReactionLine(
        ViolenceKind p_Kind, ReactionRole p_Role, bool p_Guard, bool p_Killed, uint64_t p_Seed, Language p_Language
    );

    // Erinnerung fuer das Gedaechtnis des NPCs (landet spaeter im KI-Prompt).
    std::string ExperienceText(
        ViolenceKind p_Kind, ReactionRole p_Role, bool p_Killed, const std::string& p_VictimName, Language p_Language
    );

    // Wie stark sich Angst/Vertrauen/Misstrauen gegenueber 47 aendern.
    struct AttitudeShock {
        int m_Trust;
        int m_Suspicion;
        int m_Fear;
    };

    AttitudeShock ShockFor(ViolenceKind p_Kind, ReactionRole p_Role, bool p_Guard, bool p_Killed);

    // Reichweiten: wer sieht es, wer hoert es noch?
    float SightRange(ViolenceKind p_Kind);
    float HearingRange(ViolenceKind p_Kind);

    // Emotion fuer Stimme/Untertitel.
    const char* ReactionEmotion(ReactionRole p_Role, bool p_Guard);

    // --- Verwundungen -------------------------------------------------------------------------------------------------

    enum class BodyZone { Unknown = 0, Head, Torso, Arm, Leg };

    // Wo hat der Schuss getroffen? Geschaetzt aus der Zielrichtung (Kamera-Strahl) und der Position des Opfers:
    // Hoehe ueber den Fuessen, an der der Strahl die Koerperachse passiert (+ seitlicher Abstand fuer Arme).
    BodyZone EstimateHitZone(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_VictimFeet);

    const char* BodyZoneName(BodyZone p_Zone, Language p_Language);

    // Was passiert mit dem Verletzten?
    enum class WoundState {
        Limping,   // Bein getroffen: humpelt, kommt kaum vorwaerts
        ArmHurt,   // Arm getroffen: Schmerzen
        Down,      // Bauch/Brust oder Stich: liegt am Boden, kriecht, verblutet evtl.
    };

    // Ersten Schrei nach einer Verwundung (je nach Koerperteil).
    std::string WoundLine(BodyZone p_Zone, ViolenceKind p_Kind, uint64_t p_Seed, Language p_Language);

    // Stoehnen/Flehen, solange jemand verletzt ist.
    std::string MoanLine(WoundState p_State, uint64_t p_Seed, Language p_Language);

    // Zustand fuer den KI-Prompt ("Du liegst am Boden und verblutest ...").
    std::string WoundStateText(WoundState p_State, BodyZone p_Zone, ViolenceKind p_Kind, Language p_Language);

    // --- Drohungen, Entwaffnen, Ruf -----------------------------------------------------------------------------------

    // Droht 47 mit dem Tod oder sagt, dass er bewaffnet ist? (Schluesselwoerter, Deutsch + Englisch)
    bool IsThreat(const std::string& p_Text);

    // Erste, sofortige Reaktion auf eine Drohung (ohne auf die KI zu warten).
    std::string ThreatReactionLine(bool p_Armed, int p_ArmedKills, uint64_t p_Seed, Language p_Language);

    // Waffe aus der Hand geschossen/geschlagen: Flehen um das eigene Leben.
    std::string DisarmedLine(uint64_t p_Seed, Language p_Language);

    // Was alle ueber 47 wissen, nachdem er Bewaffnete getoetet hat (leer, wenn nichts passiert ist).
    std::string ReputationText(int p_ArmedKills, int p_CivilianKills, bool p_ListenerArmed, Language p_Language);
}
