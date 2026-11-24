// Kern-Tests fuer Living World – laufen ohne das Spiel.
//
//   LivingWorldTests.exe                 schnelle Logik-Tests (offline)
//   LivingWorldTests.exe --llm           + echte Anfrage an den lokalen KI-Server (Ollama)
//   LivingWorldTests.exe --tts           + Sprachausgabe erzeugen (WAV in %TEMP%, nichts abspielen)
//   LivingWorldTests.exe --stt <modell>  + Spracherkennung: TTS-Satz -> whisper -> Text vergleichen
//   LivingWorldTests.exe --play          + erzeugte Sprache leise abspielen
//   LivingWorldTests.exe --all <modell>  alles ausser --play

#include "Audio.h"
#include "Dialogue.h"
#include "FireGate.h"
#include "Http.h"
#include "LlmClient.h"
#include "Log.h"
#include "Reactions.h"
#include "Settings.h"
#include "Speech.h"
#include "Text.h"
#include "Voice.h"
#include "World.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <thread>

using namespace lw;

namespace {
    int g_Failures = 0;
    int g_Checks = 0;

    void Check(bool p_Condition, const char* p_What) {
        ++g_Checks;

        if (!p_Condition) {
            ++g_Failures;
            std::printf("  [FEHLER] %s\n", p_What);
        }
        else {
            std::printf("  [ok] %s\n", p_What);
        }
    }

    void Section(const char* p_Name) {
        std::printf("\n=== %s ===\n", p_Name);
    }

    WorldSnapshot MakeWorld() {
        WorldSnapshot s_World;
        s_World.m_InLevel = true;
        s_World.m_LocationId = "LOCATION_PARIS";
        s_World.m_LocationName = FriendlyLocation("LOCATION_PARIS", "", Language::German);
        s_World.m_Player.m_Valid = true;
        s_World.m_Player.m_Outfit = "Waiter";
        s_World.m_Player.m_InSuit = false;
        s_World.m_Ai.m_Tension = 1;

        NpcSnapshot s_Guard;
        s_Guard.m_Key = 101;
        s_Guard.m_Name = "Security Guard";
        s_Guard.m_Outfit = "Security Guard";
        s_Guard.m_Guard = true;
        s_Guard.m_WeaponsAllowed = true;
        s_Guard.m_Behavior = 58; // Patrol
        s_Guard.m_Distance = 2.5f;
        s_World.m_Npcs.push_back(s_Guard);

        NpcSnapshot s_Waiter;
        s_Waiter.m_Key = 202;
        s_Waiter.m_Name = "Sophie Martin";
        s_Waiter.m_Outfit = "Waiter";
        s_Waiter.m_Female = true;
        s_Waiter.m_Enforcer = true;
        s_Waiter.m_Behavior = 53; // AmbientStand
        s_Waiter.m_Distance = 4.f;
        s_World.m_Npcs.push_back(s_Waiter);

        TargetSnapshot s_Target;
        s_Target.m_Key = 303;
        s_Target.m_Name = "Viktor Novikov";
        s_Target.m_Pos = { 30.f, 5.f, 4.f };
        s_World.m_Targets.push_back(s_Target);
        return s_World;
    }

    void TestText() {
        Section("Text");
        Check(Trim("  hallo \n") == "hallo", "Trim");
        Check(ToLowerUtf8("ÄÖÜ Straße") == "äöü straße", "ToLowerUtf8 mit Umlauten");
        Check(CleanSpokenText("*seufzt* Na gut. [lacht]") == "Na gut.", "Regieanweisungen entfernen");
        Check(CleanSpokenText("\"Hallo da!\"") == "Hallo da!", "Anfuehrungszeichen entfernen");
        Check(TruncateUtf8("äää", 3) == "ä", "UTF-8 sauber kuerzen");
        Check(ContainsWord("Hey Sophie, komm mal her", "sophie"), "Namen im Satz finden");
        Check(LooksLikePersonName("Viktor Novikov") && !LooksLikePersonName("Security Guard"), "Personennamen erkennen");
    }

    void TestSettings() {
        Section("Einstellungen");
        std::map<std::string, std::string> s_Store;
        SettingsStore s_Io;
        s_Io.m_Get = [&](const std::string& p_Section, const std::string& p_Key, const std::string& p_Default) {
            const auto s_It = s_Store.find(p_Section + "." + p_Key);
            return s_It == s_Store.end() ? p_Default : s_It->second;
        };
        s_Io.m_Set = [&](const std::string& p_Section, const std::string& p_Key, const std::string& p_Value) {
            s_Store[p_Section + "." + p_Key] = p_Value;
        };

        Settings s_Original;
        s_Original.m_LlmModel = "llama3.2:3b";
        s_Original.m_WarnSeconds = 12.5f;
        s_Original.m_Language = Language::English;
        s_Original.m_PushToTalkKey = VK_XBUTTON1;
        SaveSettings(s_Original, s_Io);

        Settings s_Loaded;
        LoadSettings(s_Loaded, s_Io);
        Check(s_Loaded.m_LlmModel == "llama3.2:3b", "Text-Einstellung gespeichert");
        Check(std::abs(s_Loaded.m_WarnSeconds - 12.5f) < 0.001f, "Kommazahl gespeichert (unabhaengig vom Gebietsschema)");
        Check(s_Loaded.m_Language == Language::English, "Aufzaehlung gespeichert");
        Check(s_Loaded.m_PushToTalkKey == VK_XBUTTON1, "Taste gespeichert");

        s_Store["realism.warn_seconds"] = "9999";
        LoadSettings(s_Loaded, s_Io);
        Check(s_Loaded.m_WarnSeconds <= 60.f, "Unsinnige Werte werden begrenzt");
        Check(KeyName('T') == "T" && KeyName(VK_F8) == "F8" && KeyName(VK_XBUTTON1) == "Maus 4", "Tastennamen");
    }

    void TestReplyParsing() {
        Section("KI-Antworten auswerten");
        NpcReply s_Reply = ParseReply(
            R"({"say":"Hände hoch!","emotion":"angry","action":"hold_fire","trust":-5,"suspicion":12,"fear":3})"
        );
        Check(s_Reply.m_Valid && s_Reply.m_Say == "Hände hoch!", "Sauberes JSON");
        Check(s_Reply.m_Action == NpcAction::HoldFire && s_Reply.m_Suspicion == 12, "Aktion und Werte");

        s_Reply = ParseReply("Klar doch:\n```json\n{\"say\": \"Na gut, gehen Sie.\", \"emotion\": \"annoyed\", \"action\": \"stand_down\", \"trust\": \"4\", \"suspicion\": 0, \"fear\": 0}\n```");
        Check(s_Reply.m_Valid && s_Reply.m_Action == NpcAction::StandDown && s_Reply.m_Trust == 4, "JSON in Text/Codeblock + Zahl als Text");

        s_Reply = ParseReply(R"({"say":"Guten Abend. Wie kann ich Ihnen helfen? Die Küche ist hinten links.","emotion":"neutral","action":"none","trust":0,"suspicion":0,"fear":0})");
        Check(s_Reply.m_Say == "Guten Abend. Die Küche ist hinten links.", "Chatbot-Floskel wird entfernt");

        s_Reply = ParseReply(R"({"say":"Wie kann ich dir helfen?","emotion":"neutral","action":"none","trust":0,"suspicion":0,"fear":0})");
        Check(s_Reply.m_Say == "Hm?", "Nur Floskel -> knappes Hm?");

        s_Reply = ParseReply("Was wollen Sie hier?");
        Check(s_Reply.m_Valid && s_Reply.m_Say == "Was wollen Sie hier?" && s_Reply.m_Action == NpcAction::None, "Reiner Text als Notloesung");

        s_Reply = ParseReply(R"({"say":"*schaut genervt* Was denn?","emotion":"annoyed","action":"open_fire","trust":0,"suspicion":99,"fear":0})");
        Check(s_Reply.m_Say == "Was denn?" && s_Reply.m_Suspicion == 30, "Regieanweisung raus, Extremwerte begrenzt");

        // Gestreamte Antwort: vorab sprechen, sobald Gefuehl und Satz komplett sind.
        std::string s_Say;
        std::string s_Emotion;
        Check(!ExtractEarlyReply(R"({"emotion":"scared","say":"Bitte, nicht schie)", s_Say, s_Emotion), "Halber Satz: noch nicht sprechen");
        Check(ExtractEarlyReply(R"({"emotion":"scared","say":"Bitte, \"nicht\" schießen!","act)", s_Say, s_Emotion) &&
              s_Say == "Bitte, \"nicht\" schießen!" && s_Emotion == "scared", "Satz komplett: vorab sprechen (mit Anfuehrungszeichen)");
        Check(ExtractEarlyReply(R"({ "action": "none", "fear": 12, "say": "Was?", "emotion": "nervous")", s_Say, s_Emotion) &&
              s_Say == "Was?", "Andere Reihenfolge der Felder geht auch");
        Check(!ExtractEarlyReply(R"({"say":"Nur der Satz")", s_Say, s_Emotion), "Ohne Gefuehl noch nicht");

        const std::string s_Schema = ReplySchema().dump();
        Check(s_Schema.find("\"emotion\"") < s_Schema.find("\"say\"") && s_Schema.find("\"say\"") < s_Schema.find("\"action\""),
              "Schema: erst Gefuehl, dann Satz, dann der Rest");

        LlmConfig s_Config;
        LlmJob s_Job;
        s_Job.m_Schema = ReplySchema();
        const std::string s_Body = LlmClient::BuildRequestBody(s_Config, s_Job, true).dump();
        Check(s_Body.find("\"emotion\"") < s_Body.find("\"say\"") && s_Body.find("\"stream\":true") != std::string::npos,
              "Anfrage behaelt die Reihenfolge und streamt");
    }

    void TestConfrontation() {
        Section("Drohen, Entwaffnen, Ruf");
        Check(IsThreat("Keine Bewegung, ich bin bewaffnet!"), "\"ich bin bewaffnet\" ist eine Drohung");
        Check(IsThreat("Wenn du schreist, bring ich dich um."), "\"bring ich dich um\" ist eine Drohung");
        Check(IsThreat("Hände hoch oder ich schieße"), "\"Hände hoch\" ist eine Drohung");
        Check(!IsThreat("Ich habe keine Waffe, ganz ruhig."), "\"keine Waffe\" ist keine Drohung");
        Check(!IsThreat("Ganz ruhig! Ich lege die Waffe weg."), "\"Ich lege die Waffe weg\" ist keine Drohung");
        Check(!IsThreat("Guten Abend, wo ist die Küche?"), "Normale Frage ist keine Drohung");

        const std::string s_Beg = ThreatReactionLine(false, 0, 5, Language::German);
        const std::string s_Resist = ThreatReactionLine(true, 0, 5, Language::German);
        const std::string s_Disarmed = DisarmedLine(3, Language::German);
        std::printf("    Unbewaffnet: %s\n    Wache: %s\n    Entwaffnet: %s\n", s_Beg.c_str(), s_Resist.c_str(), s_Disarmed.c_str());
        Check(!s_Beg.empty() && !s_Resist.empty() && !s_Disarmed.empty(), "Saetze fuer Betteln, Gegenwehr, Entwaffnet");

        Check(ReputationText(0, 0, true, Language::German).empty(), "Ohne Tote kein Ruf");
        const std::string s_Feared = ReputationText(6, 1, true, Language::German);
        std::printf("    Ruf (6 Bewaffnete): %s\n", s_Feared.c_str());
        Check(!s_Feared.empty() && s_Feared.find('6') != std::string::npos, "Ruf nennt die getoeteten Bewaffneten");

        // Prompt: bedroht + entwaffnet + Ruf
        WorldSnapshot s_World = MakeWorld();
        Settings s_Settings;
        DialogueBook s_Book;
        PromptContext s_Context;
        s_Context.m_Settings = &s_Settings;
        s_Context.m_World = &s_World;
        s_Context.m_Npc = &s_World.m_Npcs[0];
        s_Context.m_PlayerText = "Keine Bewegung oder du bist tot.";
        s_Context.m_Threatened = true;
        s_Context.m_Disarmed = true;
        s_Context.m_ArmedKills = 4;
        const std::string s_Prompt = s_Book.BuildMessages(s_Context).back().m_Content;
        Check(s_Prompt.find("ENTWAFFNET") != std::string::npos && s_Prompt.find("bettelst") != std::string::npos,
              "Prompt: Entwaffnet und bettelt");
        Check(s_Prompt.find("WAS MAN ÜBER IHN WEISS") != std::string::npos, "Prompt: Ruf von 47");
    }

    void TestFireGate() {
        Section("Deeskalation (Feuerfreigabe)");
        FireGate s_Gate;
        FireGateConfig s_Config;
        s_Config.m_WarnSeconds = 5.f;
        s_Config.m_NegotiationMaxSeconds = 30.f;
        s_Config.m_HoldFireExtend = 10.f;
        s_Config.m_StandDownSeconds = 60.f;
        s_Config.m_AggressionMemorySeconds = 20.f;
        s_Gate.Configure(s_Config);

        GateDecision s_Decision = s_Gate.OnShotAttempt(1, 100.0);
        Check(s_Decision.m_Block && s_Decision.m_FirstAttempt, "Erster Schuss wird abgefangen + Warnung");
        Check(s_Gate.OnShotAttempt(1, 103.0).m_Block, "Innerhalb der Warnzeit kein Schuss");
        Check(!s_Gate.OnShotAttempt(1, 105.5).m_Block, "Nach Ablauf der Warnzeit wird geschossen");

        // Verhandlung verlaengert die Frist
        s_Gate.Reset();
        s_Gate.OnShotAttempt(2, 200.0);
        s_Gate.KeepNegotiating(2, 204.0, 6.f);
        Check(s_Gate.OnShotAttempt(2, 209.0).m_Block, "Reden verlaengert die Frist");
        Check(!s_Gate.OnShotAttempt(2, 231.0).m_Block, "Aber nie ueber die Maximalzeit hinaus");

        // Ueberreden
        s_Gate.Reset();
        s_Gate.OnShotAttempt(3, 300.0);
        s_Gate.Apply(3, NpcAction::StandDown, 302.0);
        Check(s_Gate.OnShotAttempt(3, 330.0).m_Block, "Ueberredeter NPC schiesst nicht");
        Check(s_Gate.State(3, 330.0) == GateState::Persuaded, "Status: ueberredet");

        // 47 greift an
        s_Gate.OnPlayerAggression(331.0);
        Check(!s_Gate.OnShotAttempt(3, 331.5).m_Block, "Nach Angriff von 47 darf zurueckgeschossen werden");
        Check(!s_Gate.OnShotAttempt(4, 340.0).m_Block, "Auch neu dazukommende Wachen (Angriff ist frisch)");

        // Gruppe
        s_Gate.Reset();
        s_Gate.OnShotAttempt(5, 400.0);
        s_Gate.OnShotAttempt(6, 400.5);
        s_Gate.Apply(5, NpcAction::OpenFire, 401.0);
        Check(!s_Gate.OnShotAttempt(6, 401.2).m_Block, "Gruppe: wenn einer schiesst, schiessen alle");

        // Letzte Warnung
        s_Gate.Reset();
        s_Gate.OnShotAttempt(7, 500.0);
        s_Gate.OnShotAttempt(7, 502.0);
        s_Gate.OnShotAttempt(7, 503.0);
        Check(s_Gate.TakeLastWarnings(503.0).size() == 1, "Letzte Warnung kurz vor Ablauf");
        Check(s_Gate.TakeLastWarnings(503.1).empty(), "... aber nur einmal");

        // Abgeschaltet
        s_Config.m_Enabled = false;
        s_Gate.Configure(s_Config);
        s_Gate.Reset();
        Check(!s_Gate.OnShotAttempt(8, 600.0).m_Block, "Deeskalation aus = Vanilla-Verhalten");
    }

    void TestPrompt(bool p_Print) {
        Section("Prompt-Aufbau");
        Settings s_Settings;
        WorldSnapshot s_World = MakeWorld();
        DialogueBook s_Book;

        const NpcSnapshot& s_Waiter = s_World.m_Npcs[1];
        s_Book.Mind(s_Waiter, s_World, Language::German);
        s_Book.Mind(s_World.m_Npcs[0], s_World, Language::German);

        s_Book.RecordPlayerLine("Guten Abend, ich bin neu hier.", VoiceVolume::Normal, 202, "Sophie Martin", { 101, 202 }, 10.0);

        NpcReply s_Answer;
        s_Answer.m_Say = "Neu? Davon weiß ich nichts.";
        s_Answer.m_Emotion = "suspicious";
        s_Book.RecordNpcLine(202, "Sophie Martin", s_Answer, { 101, 202 }, 12.0);

        PromptContext s_Context;
        s_Context.m_Settings = &s_Settings;
        s_Context.m_World = &s_World;
        s_Context.m_Npc = &s_Waiter;
        s_Context.m_Trigger = DialogueTrigger::PlayerSpoke;
        s_Context.m_PlayerText = "Der Chef hat mich geschickt. Wo ist Herr Novikov?";

        const auto s_Messages = s_Book.BuildMessages(s_Context);
        Check(s_Messages.size() >= 4, "System + Rolle + Verlauf + Lage");
        Check(s_Messages.front().m_Role == "system", "Beginnt mit System-Regeln");
        Check(s_Messages.back().m_Content.find("Novikov") != std::string::npos, "Zielperson ist im Wissen enthalten");
        Check(s_Messages.back().m_Content.find("noch nie gesehen") != std::string::npos, "Enforcer erkennt Fremden in gleicher Kleidung");

        bool s_HasAssistant = false;

        for (const auto& s_Message : s_Messages) {
            s_HasAssistant |= s_Message.m_Role == "assistant";
        }

        Check(s_HasAssistant, "Eigene fruehere Antwort ist im Verlauf");

        // Mithoerer
        NpcMind* s_GuardMind = s_Book.Find(101);
        Check(s_GuardMind && s_GuardMind->m_Lines.size() == 2 && s_GuardMind->m_Lines[0].m_Overheard, "Wache hat das Gespraech mitgehoert");

        if (p_Print) {
            for (const auto& s_Message : s_Messages) {
                std::printf("\n--- %s ---\n%s\n", s_Message.m_Role.c_str(), s_Message.m_Content.c_str());
            }
        }
    }

    void TestAddressee() {
        Section("Wen spricht 47 an?");
        WorldSnapshot s_World = MakeWorld();

        // Wache: 2,5 m, direkt vor 47. Kellnerin Sophie: 4 m, seitlich.
        s_World.m_Npcs[0].m_ViewAngle = 5.f;
        s_World.m_Npcs[1].m_ViewAngle = 75.f;

        AddresseeChoice s_Choice = ChooseAddressee(s_World, "Guten Abend!", 7.f, 0, -1e9, 100.0);
        Check(s_Choice.m_Key == 101 && s_Choice.m_Hearers.size() == 2, "Wer vor 47 steht, ist gemeint; beide hoeren mit");

        s_Choice = ChooseAddressee(s_World, "Sophie, kommst du mal?", 7.f, 0, -1e9, 100.0);
        Check(s_Choice.m_Key == 202, "Mit Namen angesprochen");

        s_World.m_Npcs[1].m_ViewAngle = 100.f;
        s_Choice = ChooseAddressee(s_World, "Und dann?", 7.f, 202, 95.0, 100.0);
        Check(s_Choice.m_Key == 101, "Gespraechspartner abgewandt (100 Grad) -> wer vorne steht");

        s_World.m_Npcs[1].m_ViewAngle = 40.f;
        s_Choice = ChooseAddressee(s_World, "Und dann?", 7.f, 202, 95.0, 100.0);
        Check(s_Choice.m_Key == 202, "Laufendes Gespraech wird fortgesetzt");

        s_Choice = ChooseAddressee(s_World, "Psst!", 2.f, 0, -1e9, 100.0);
        Check(s_Choice.m_Key == 0 && s_Choice.m_Hearers.empty(), "Gefluestert hoert niemand in 2,5 m Abstand");

        s_World.m_Npcs[0].m_Pacified = true;
        s_Choice = ChooseAddressee(s_World, "Hallo?", 7.f, 0, -1e9, 100.0);
        Check(s_Choice.m_Key != 101, "Bewusstlose antworten nicht");
    }

    void TestReactions() {
        Section("Reaktionen auf Gewalt");
        const std::string s_Victim = ReactionLine(ViolenceKind::Stab, ReactionRole::Victim, false, false, 7, Language::German);
        const std::string s_Witness = ReactionLine(ViolenceKind::Gunshot, ReactionRole::Eyewitness, false, true, 3, Language::German);
        const std::string s_Guard = ReactionLine(ViolenceKind::Gunshot, ReactionRole::Eyewitness, true, false, 3, Language::German);
        const std::string s_Ear = ReactionLine(ViolenceKind::ShotHeard, ReactionRole::Earwitness, false, false, 3, Language::German);
        std::printf("    Opfer (Messer): %s\n    Zeuge (Mord): %s\n    Wache: %s\n    Gehoert: %s\n",
                    s_Victim.c_str(), s_Witness.c_str(), s_Guard.c_str(), s_Ear.c_str());
        Check(!s_Victim.empty() && !s_Witness.empty() && !s_Guard.empty() && !s_Ear.empty(), "Fuer jede Rolle gibt es Saetze");

        const std::string s_Memory = ExperienceText(ViolenceKind::Stab, ReactionRole::Eyewitness, true, "Sophie Martin", Language::German);
        Check(s_Memory.find("Sophie Martin") != std::string::npos && s_Memory.find("erstochen") != std::string::npos, "Erinnerung nennt Opfer und Tat");

        // Trefferzone: Kamera in 1,7 m Hoehe, 10 m entfernt, Opfer bei x=10.
        const Vec3 s_Feet { 10.f, 0.f, 0.f };
        const Vec3 s_Camera { 0.f, 0.f, 1.7f };
        const auto s_Aim = [&](float p_Z, float p_Y) {
            Vec3 s_Dir { 10.f, p_Y, p_Z - 1.7f };
            const float s_L = std::sqrt(s_Dir.x * s_Dir.x + s_Dir.y * s_Dir.y + s_Dir.z * s_Dir.z);
            return Vec3 { s_Dir.x / s_L, s_Dir.y / s_L, s_Dir.z / s_L };
        };
        Check(EstimateHitZone(s_Camera, s_Aim(0.5f, 0.f), s_Feet) == BodyZone::Leg, "Zielen aufs Knie = Bein");
        Check(EstimateHitZone(s_Camera, s_Aim(1.2f, 0.f), s_Feet) == BodyZone::Torso, "Zielen auf den Bauch = Oberkoerper");
        Check(EstimateHitZone(s_Camera, s_Aim(1.3f, 0.35f), s_Feet) == BodyZone::Arm, "Seitlich = Arm");
        Check(EstimateHitZone(s_Camera, s_Aim(1.65f, 0.f), s_Feet) == BodyZone::Head, "Zielen auf den Kopf = Kopf");
        Check(EstimateHitZone(s_Camera, Vec3 { -1.f, 0.f, 0.f }, s_Feet) == BodyZone::Unknown, "Weggedreht = unbekannt");
        std::printf("    Beinschuss: %s\n    Gestochen, am Boden: %s\n",
                    WoundLine(BodyZone::Leg, ViolenceKind::Gunshot, 1, Language::German).c_str(),
                    MoanLine(WoundState::Down, 2, Language::German).c_str());

        const AttitudeShock s_Shock = ShockFor(ViolenceKind::Gunshot, ReactionRole::Eyewitness, false, true);
        Check(s_Shock.m_Fear >= 60 && s_Shock.m_Trust <= -50, "Zeuge eines Mordes hat Todesangst");

        WorldSnapshot s_World = MakeWorld();
        Settings s_Settings;
        DialogueBook s_Book;
        s_Book.Mind(s_World.m_Npcs[0], s_World, Language::German);
        s_Book.AddExperience(101, ExperienceText(ViolenceKind::Gunshot, ReactionRole::Victim, false, "", Language::German), -60, 60, 45);

        PromptContext s_Context;
        s_Context.m_Settings = &s_Settings;
        s_Context.m_World = &s_World;
        s_Context.m_Npc = &s_World.m_Npcs[0];
        s_Context.m_PlayerText = "Alles in Ordnung?";
        const auto s_Messages = s_Book.BuildMessages(s_Context);
        Check(s_Messages.back().m_Content.find("auf DICH geschossen") != std::string::npos, "Erlebnis steht im KI-Prompt");
    }

    void TestSpatial() {
        Section("Raumklang");
        const float s_Listener[3] = { 0.f, 0.f, 0.f };
        const float s_Right[3] = { 1.f, 0.f, 0.f };
        const float s_Near[3] = { 1.f, 0.f, 0.f };
        const float s_FarLeft[3] = { -25.f, 0.f, 0.f };

        const SpatialResult s_A = ComputeSpatial(s_Listener, s_Right, s_Near, 30.f);
        const SpatialResult s_B = ComputeSpatial(s_Listener, s_Right, s_FarLeft, 30.f);
        Check(s_A.m_Volume > 0.95f && s_A.m_Pan > 0.5f, "Nah und rechts = laut, rechts");
        Check(s_B.m_Volume < 0.2f && s_B.m_Pan < -0.5f, "Weit links = leise, links");
    }

    bool TestLlm() {
        Section("Lokale KI (Ollama)");
        LlmClient s_Client;
        Settings s_Settings;

        // Modell zum Vergleichen: set LW_TEST_MODEL=qwen3:1.7b
        char s_ModelOverride[128] = {};

        if (GetEnvironmentVariableA("LW_TEST_MODEL", s_ModelOverride, sizeof(s_ModelOverride)) > 0) {
            s_Settings.m_LlmModel = s_ModelOverride;
        }

        std::printf("    Teste mit Modell: %s\n", s_Settings.m_LlmModel.c_str());
        s_Client.SetConfig(LlmConfig::FromSettings(s_Settings));

        std::string s_Error;
        const bool s_Server = s_Client.EnsureServer(s_Error);
        Check(s_Server, ("Server erreichbar " + s_Error).c_str());

        if (!s_Server) {
            return false;
        }

        std::vector<std::string> s_Models;
        Check(s_Client.QueryModels(s_Models, s_Error), "Modell-Liste abrufen");

        for (const auto& s_Model : s_Models) {
            std::printf("    Modell: %s\n", s_Model.c_str());
        }

        WorldSnapshot s_World = MakeWorld();
        DialogueBook s_Book;
        PromptContext s_Context;
        s_Context.m_Settings = &s_Settings;
        s_Context.m_World = &s_World;
        s_Context.m_Npc = &s_World.m_Npcs[0];
        s_Context.m_Trigger = DialogueTrigger::PlayerSpoke;
        s_Context.m_PlayerText = "Guten Abend. Ich suche die Küche, ich bin heute zum ersten Mal hier.";

        // Beleidigung: soll beleidigt/wuetend reagieren, nicht hoeflich.
        {
            WorldSnapshot s_InsultWorld = MakeWorld();
            DialogueBook s_InsultBook;
            PromptContext s_Insult = s_Context;
            s_Insult.m_World = &s_InsultWorld;
            s_Insult.m_Npc = &s_InsultWorld.m_Npcs[1];
            s_Insult.m_PlayerText = "Du bist die schlechteste Kellnerin, die ich je gesehen habe.";
            LlmJob s_InsultJob;
            s_InsultJob.m_Messages = s_InsultBook.BuildMessages(s_Insult);
            s_InsultJob.m_Schema = ReplySchema();
            const LlmResult s_InsultResult = s_Client.RunNow(s_InsultJob);

            if (s_InsultResult.m_Ok) {
                const NpcReply s_Reply = ParseReply(s_InsultResult.m_Content);
                std::printf("    (%.1f s) Kellnerin (beleidigt): \"%s\" [%s]\n", s_InsultResult.m_Seconds, s_Reply.m_Say.c_str(), s_Reply.m_Emotion.c_str());
            }
        }

        LlmJob s_Job;
        s_Job.m_Messages = s_Book.BuildMessages(s_Context);
        s_Job.m_Schema = ReplySchema();

        for (int s_Round = 0; s_Round < 2; ++s_Round) {
            const LlmResult s_Result = s_Client.RunNow(s_Job);
            Check(s_Result.m_Ok, ("Antwort erhalten " + s_Result.m_Error).c_str());

            if (!s_Result.m_Ok) {
                return false;
            }

            const NpcReply s_Reply = ParseReply(s_Result.m_Content);
            std::printf(
                "    (%.1f s, %d+%d Tokens) Wache: \"%s\" [%s, %s]\n", s_Result.m_Seconds, s_Result.m_PromptTokens,
                s_Result.m_OutputTokens, s_Reply.m_Say.c_str(), s_Reply.m_Emotion.c_str(),
                ActionLabel(s_Reply.m_Action, Language::German).c_str()
            );
            Check(s_Reply.m_Valid && !s_Reply.m_Say.empty(), "Antwort ist gueltig und nicht leer");
        }

        // Gestreamt: der Satz muss deutlich vor dem Ende der Antwort fertig sein.
        {
            const auto s_Start = std::chrono::steady_clock::now();
            double s_EarlyAt = -1.0;
            std::string s_EarlySay;
            const LlmResult s_Streamed = s_Client.RunNow(s_Job, nullptr, [&](const std::string& p_SoFar) {
                std::string s_Emotion;

                if (s_EarlyAt < 0.0 && ExtractEarlyReply(p_SoFar, s_EarlySay, s_Emotion)) {
                    s_EarlyAt = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
                }
            });
            Check(s_Streamed.m_Ok && ParseReply(s_Streamed.m_Content).m_Valid, "Gestreamte Antwort ist gueltig");
            Check(s_EarlyAt > 0.0 && !s_EarlySay.empty(), "Satz kam schon vorab");
            std::printf(
                "    gestreamt: Satz nach %.2f s, fertig nach %.2f s (Prompt %d Tokens in %.2f s): \"%s\"\n", s_EarlyAt,
                s_Streamed.m_Seconds, s_Streamed.m_PromptTokens, s_Streamed.m_PromptSeconds, s_EarlySay.c_str()
            );
        }

        // Konfrontation: Wache zielt, 47 redet
        s_World.m_Ai.m_IllegalWeapon = true;
        s_World.m_Ai.m_Tension = 8;
        s_World.m_Npcs[0].m_Behavior = 82; // StandAndAim
        s_World.m_Npcs[0].m_WeaponOut = true;
        s_Context.m_Gate = GateState::Warning;
        s_Context.m_GateSecondsLeft = 8.f;
        s_Context.m_Volume = VoiceVolume::Normal;
        s_Context.m_PlayerText = "Ganz ruhig! Ich lege die Waffe weg. Ich will keinen Ärger, ich gehe sofort.";
        s_Job.m_Messages = s_Book.BuildMessages(s_Context);

        const LlmResult s_Result = s_Client.RunNow(s_Job);
        Check(s_Result.m_Ok, "Antwort in der Konfrontation");

        if (s_Result.m_Ok) {
            const NpcReply s_Reply = ParseReply(s_Result.m_Content);
            std::printf(
                "    (%.1f s) Wache (zielt): \"%s\" [%s, Aktion: %s]\n", s_Result.m_Seconds, s_Reply.m_Say.c_str(),
                s_Reply.m_Emotion.c_str(), ActionLabel(s_Reply.m_Action, Language::German).c_str()
            );
            Check(s_Reply.m_Action != NpcAction::OpenFire, "Wache schiesst nicht auf jemanden, der aufgibt");
        }

        // Angeschossene Kellnerin wird danach angesprochen.
        {
            WorldSnapshot s_Calm = MakeWorld();
            DialogueBook s_Book2;
            s_Book2.Mind(s_Calm.m_Npcs[1], s_Calm, Language::German);
            s_Book2.AddExperience(202, ExperienceText(ViolenceKind::Gunshot, ReactionRole::Victim, false, "", Language::German), -60, 60, 80);
            s_Calm.m_Npcs[1].m_Health = 35.f;

            PromptContext s_Hurt;
            s_Hurt.m_Settings = &s_Settings;
            s_Hurt.m_World = &s_Calm;
            s_Hurt.m_Npc = &s_Calm.m_Npcs[1];
            s_Hurt.m_PlayerText = "Guten Abend, alles in Ordnung bei Ihnen?";

            LlmJob s_HurtJob;
            s_HurtJob.m_Messages = s_Book2.BuildMessages(s_Hurt);
            s_HurtJob.m_Schema = ReplySchema();
            const LlmResult s_HurtResult = s_Client.RunNow(s_HurtJob);
            Check(s_HurtResult.m_Ok, "Antwort der angeschossenen Kellnerin");

            if (s_HurtResult.m_Ok) {
                const NpcReply s_Reply = ParseReply(s_HurtResult.m_Content);
                std::printf("    (%.1f s) Kellnerin (angeschossen): \"%s\" [%s]\n", s_HurtResult.m_Seconds, s_Reply.m_Say.c_str(), s_Reply.m_Emotion.c_str());
                Check(s_Reply.m_Emotion != "friendly" && s_Reply.m_Emotion != "neutral" && s_Reply.m_Emotion != "amused",
                      "Reagiert nicht gelassen, als waere nichts passiert");
            }
        }

        return true;
    }

    bool SynthesizeSample(const std::string& p_Text, bool p_Female, PcmBuffer& p_Pcm) {
        SapiTts s_Tts;
        std::string s_Error;

        if (!s_Tts.Init(s_Error)) {
            std::printf("    %s\n", s_Error.c_str());
            return false;
        }

        const auto s_Voices = s_Tts.ListVoices();
        const VoiceInfo* s_Voice = SpeechService::PickVoice(s_Voices, "", p_Female, true);
        SpeechStyle s_Style;
        const bool s_Ok = s_Tts.Synthesize(p_Text, s_Voice ? s_Voice->m_TokenId : L"", s_Style, p_Pcm, s_Error);

        if (!s_Ok) {
            std::printf("    %s\n", s_Error.c_str());
        }

        return s_Ok;
    }

    void WriteWav(const std::wstring& p_Path, const PcmBuffer& p_Pcm) {
        FILE* s_File = nullptr;

        if (_wfopen_s(&s_File, p_Path.c_str(), L"wb") != 0 || !s_File) {
            return;
        }

        const uint32_t s_DataBytes = static_cast<uint32_t>(p_Pcm.m_Samples.size() * 2);
        const uint32_t s_Riff = 36 + s_DataBytes;
        const uint16_t s_Format = 1;
        const uint16_t s_Channels = 1;
        const uint32_t s_Rate = p_Pcm.m_SampleRate;
        const uint32_t s_ByteRate = s_Rate * 2;
        const uint16_t s_Align = 2;
        const uint16_t s_Bits = 16;
        const uint32_t s_FmtSize = 16;

        fwrite("RIFF", 1, 4, s_File);
        fwrite(&s_Riff, 4, 1, s_File);
        fwrite("WAVEfmt ", 1, 8, s_File);
        fwrite(&s_FmtSize, 4, 1, s_File);
        fwrite(&s_Format, 2, 1, s_File);
        fwrite(&s_Channels, 2, 1, s_File);
        fwrite(&s_Rate, 4, 1, s_File);
        fwrite(&s_ByteRate, 4, 1, s_File);
        fwrite(&s_Align, 2, 1, s_File);
        fwrite(&s_Bits, 2, 1, s_File);
        fwrite("data", 1, 4, s_File);
        fwrite(&s_DataBytes, 4, 1, s_File);
        fwrite(p_Pcm.m_Samples.data(), 2, p_Pcm.m_Samples.size(), s_File);
        fclose(s_File);
    }

    void TestTts(bool p_Play) {
        Section("Sprachausgabe (Windows-Stimmen)");
        {
            SapiTts s_Tts;
            std::string s_Error;
            Check(s_Tts.Init(s_Error), "SAPI starten");
            const auto s_Voices = s_Tts.ListVoices();
            Check(!s_Voices.empty(), "Stimmen gefunden");

            for (const auto& s_Voice : s_Voices) {
                std::printf(
                    "    %s (%s, %s%s)\n", s_Voice.m_Name.c_str(), s_Voice.m_Language.c_str(),
                    s_Voice.m_Female ? "weiblich" : "maennlich", s_Voice.m_OneCore ? ", OneCore" : ""
                );
            }

            const VoiceInfo* s_Male = SpeechService::PickVoice(s_Voices, "", false, true);
            const VoiceInfo* s_Female = SpeechService::PickVoice(s_Voices, "", true, true);
            Check(s_Male && s_Male->IsGerman(), "Deutsche Stimme fuer Maenner");
            Check(s_Female && s_Female->IsGerman() && s_Female->m_Female, "Deutsche Frauenstimme");
        }

        PcmBuffer s_Pcm;
        const bool s_Ok = SynthesizeSample("Keine Bewegung! Hände hoch, sofort!", false, s_Pcm);
        Check(s_Ok && s_Pcm.DurationSeconds() > 0.8, "Satz synthetisiert");

        if (s_Ok) {
            wchar_t s_Temp[MAX_PATH];
            GetTempPathW(MAX_PATH, s_Temp);
            const std::wstring s_Path = std::wstring(s_Temp) + L"livingworld_tts_test.wav";
            WriteWav(s_Path, s_Pcm);
            std::printf("    %.1f s Audio -> %s\n", s_Pcm.DurationSeconds(), WideToUtf8(s_Path).c_str());

            if (p_Play) {
                AudioOut s_Audio;
                std::string s_Error;

                if (s_Audio.Init(s_Error)) {
                    const uint64_t s_Handle = s_Audio.Play(s_Pcm, 0.4f, -0.5f, 1);
                    Check(s_Handle != 0, "Abspielen gestartet");

                    while (s_Audio.IsPlaying(s_Handle)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }

                    s_Audio.Collect();
                }
            }
        }
    }

    std::vector<float> Resample16k(const PcmBuffer& p_Pcm) {
        std::vector<float> s_Out;
        const double s_Ratio = static_cast<double>(p_Pcm.m_SampleRate) / MicRecorder::c_SampleRate;
        const size_t s_Count = static_cast<size_t>(p_Pcm.m_Samples.size() / s_Ratio);
        s_Out.reserve(s_Count);

        for (size_t i = 0; i < s_Count; ++i) {
            const double s_Pos = i * s_Ratio;
            const size_t s_Index = static_cast<size_t>(s_Pos);
            const double s_Frac = s_Pos - s_Index;
            const float s_A = p_Pcm.m_Samples[std::min(s_Index, p_Pcm.m_Samples.size() - 1)] / 32768.f;
            const float s_B = p_Pcm.m_Samples[std::min(s_Index + 1, p_Pcm.m_Samples.size() - 1)] / 32768.f;
            s_Out.push_back(static_cast<float>(s_A + (s_B - s_A) * s_Frac));
        }

        return s_Out;
    }

    void TestStt(const std::wstring& p_Model) {
        Section("Spracherkennung (whisper.cpp)");
        PcmBuffer s_Pcm;
        const std::string s_Sentence = "Guten Abend, ich bin der neue Kellner. Wo ist die Küche?";

        if (!SynthesizeSample(s_Sentence, false, s_Pcm)) {
            Check(false, "Testsatz erzeugen");
            return;
        }

        const std::vector<float> s_Samples = Resample16k(s_Pcm);
        std::printf("    Lautheit: %.1f dBFS\n", MeasureLoudnessDb(s_Samples));

        SpeechRecognizer s_Recognizer;
        std::string s_Error;
        auto s_Start = std::chrono::steady_clock::now();
        const bool s_Loaded = s_Recognizer.Load(p_Model, s_Error);
        const double s_LoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
        Check(s_Loaded, ("Modell laden " + s_Error).c_str());

        if (!s_Loaded) {
            return;
        }

        std::printf("    Modell geladen in %.1f s\n", s_LoadSeconds);

        std::printf("    Audiolaenge: %.1f s\n", static_cast<double>(s_Samples.size()) / MicRecorder::c_SampleRate);

        for (const int s_Threads : { 4, 6, 8 }) {
            std::string s_Text;
            s_Start = std::chrono::steady_clock::now();
            const bool s_Ok = s_Recognizer.Transcribe(s_Samples, "de", s_Threads, s_Text, s_Error);
            const double s_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
            std::printf("    (%d Threads, %.1f s) erkannt: \"%s\" %s\n", s_Threads, s_Seconds, s_Text.c_str(), s_Ok ? "" : s_Error.c_str());

            const std::string s_Lower = ToLowerUtf8(s_Text);
            Check(s_Ok && s_Lower.find("kellner") != std::string::npos && s_Lower.find("küche") != std::string::npos,
                  "Inhalt stimmt (Kellner, Küche)");
        }

        std::string s_Text;
        std::vector<float> s_Silence(MicRecorder::c_SampleRate * 2, 0.f);
        Check(!s_Recognizer.Transcribe(s_Silence, "de", 4, s_Text, s_Error), "Stille ergibt keinen Text");

        // Leises Rauschen (wie ein offenes Mikrofon ohne Sprache).
        std::vector<float> s_Noise(MicRecorder::c_SampleRate * 2);
        uint32_t s_Seed = 12345;

        for (auto& s_Value : s_Noise) {
            s_Seed = s_Seed * 1664525u + 1013904223u;
            s_Value = (static_cast<float>(s_Seed >> 8) / 16777216.f - 0.5f) * 0.004f;
        }

        const bool s_NoiseText = s_Recognizer.Transcribe(s_Noise, "de", 4, s_Text, s_Error);
        std::printf("    Rauschen -> %s\n", s_NoiseText ? ("\"" + s_Text + "\"").c_str() : s_Error.c_str());
        Check(!s_NoiseText, "Leises Rauschen ergibt keinen Text");
    }
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    bool s_Llm = false;
    bool s_Tts = false;
    bool s_Play = false;
    bool s_Print = false;
    std::wstring s_SttModel;

    for (int i = 1; i < argc; ++i) {
        const std::string s_Arg = argv[i];

        if (s_Arg == "--llm") s_Llm = true;
        else if (s_Arg == "--tts") s_Tts = true;
        else if (s_Arg == "--play") s_Play = true;
        else if (s_Arg == "--print") s_Print = true;
        else if ((s_Arg == "--stt" || s_Arg == "--all") && i + 1 < argc) {
            if (s_Arg == "--all") {
                s_Llm = true;
                s_Tts = true;
            }

            s_SttModel = Utf8ToWide(argv[++i]);
        }
    }

    TestText();
    TestSettings();
    TestReplyParsing();
    TestFireGate();
    TestPrompt(s_Print);
    TestAddressee();
    TestReactions();
    TestConfrontation();
    TestSpatial();

    if (s_Tts || s_Play) TestTts(s_Play);
    if (!s_SttModel.empty()) TestStt(s_SttModel);
    if (s_Llm) TestLlm();

    std::printf("\n%d Pruefungen, %d Fehler\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
