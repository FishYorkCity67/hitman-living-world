#include "Dialogue.h"

#include "Reactions.h"
#include "Text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

using nlohmann::json;

namespace lw {
    namespace {
        constexpr size_t c_MaxLinesPerMind = 40;
        constexpr size_t c_MaxGlobalLog = 300;

        const char* VolumeWords(VoiceVolume p_Volume, Language p_Language) {
            const bool s_De = p_Language == Language::German;

            switch (p_Volume) {
                case VoiceVolume::Whisper: return s_De ? "leise flüsternd" : "in a low whisper";
                case VoiceVolume::Shout: return s_De ? "laut rufend" : "loudly shouting";
                default: return s_De ? "in normaler Lautstärke" : "at normal volume";
            }
        }

        std::string Clamp100(int p_Value) {
            return std::to_string(std::clamp(p_Value, 0, 100));
        }

        std::string RoleDescription(const NpcSnapshot& p_Npc, Language p_Language) {
            const bool s_De = p_Language == Language::German;
            std::string s_Role = p_Npc.m_Outfit.empty() ? (s_De ? "Person vor Ort" : "person on site") : p_Npc.m_Outfit;

            if (p_Npc.m_Guard) {
                switch (p_Npc.m_Rank) {
                    case 1: s_Role += s_De ? " (Elite-Sicherheitskraft, bewaffnet)" : " (elite security, armed)"; break;
                    case 2: s_Role += s_De ? " (Polizist/in, bewaffnet)" : " (police officer, armed)"; break;
                    case 3: s_Role += s_De ? " (Schläger/Handlanger, bewaffnet)" : " (thug/henchman, armed)"; break;
                    default: s_Role += s_De ? " (Sicherheitspersonal, bewaffnet)" : " (security staff, armed)"; break;
                }
            }
            else {
                s_Role += s_De ? " (Zivilist, unbewaffnet)" : " (civilian, unarmed)";
            }

            if (p_Npc.m_Authority) {
                s_Role += s_De ? ", hast Weisungsbefugnis" : ", you have authority here";
            }

            return s_Role;
        }

        void Push(std::vector<ChatMessage>& p_Messages, const std::string& p_Role, const std::string& p_Content) {
            // Manche Server verlangen abwechselnde Rollen – gleiche Rollen zusammenfassen.
            if (!p_Messages.empty() && p_Messages.back().m_Role == p_Role && p_Role != "system") {
                p_Messages.back().m_Content += "\n" + p_Content;
                return;
            }

            p_Messages.push_back({ p_Role, p_Content });
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Wen spricht 47 an?
    // -----------------------------------------------------------------------------------------------------------------

    AddresseeChoice ChooseAddressee(
        const WorldSnapshot& p_World, const std::string& p_Text, float p_Radius, uint64_t p_LastAddressee,
        double p_LastAddressTime, double p_Now
    ) {
        AddresseeChoice s_Choice;
        std::vector<const NpcSnapshot*> s_Hearers;

        for (const auto& s_Npc : p_World.m_Npcs) {
            if (s_Npc.CanTalk() && s_Npc.m_Distance <= p_Radius) {
                s_Hearers.push_back(&s_Npc);
                s_Choice.m_Hearers.push_back(s_Npc.m_Key);
            }
        }

        if (s_Hearers.empty()) {
            return s_Choice;
        }

        // 1) Mit Namen oder Beruf angesprochen ("Hey Sophie", "Herr Wachmann" ...).
        const NpcSnapshot* s_Best = nullptr;

        for (const NpcSnapshot* s_Npc : s_Hearers) {
            std::vector<std::string> s_Tokens;

            if (LooksLikePersonName(s_Npc->m_Name)) {
                for (const auto& s_Word : SplitWords(s_Npc->m_Name)) {
                    s_Tokens.push_back(s_Word);
                }
            }

            for (const auto& s_Token : s_Tokens) {
                if (ContainsWord(p_Text, s_Token) && (!s_Best || s_Npc->m_Distance < s_Best->m_Distance)) {
                    s_Best = s_Npc;
                }
            }
        }

        if (s_Best) {
            s_Choice.m_Key = s_Best->m_Key;
            return s_Choice;
        }

        // 2) Laufendes Gespraech fortsetzen, solange der Partner noch grob vor 47 ist.
        if (p_LastAddressee != 0 && p_Now - p_LastAddressTime < 25.0) {
            for (const NpcSnapshot* s_Npc : s_Hearers) {
                if (s_Npc->m_Key == p_LastAddressee && (s_Npc->m_ViewAngle < 80.f || s_Npc->m_Distance < 2.f)) {
                    s_Choice.m_Key = s_Npc->m_Key;
                    return s_Choice;
                }
            }
        }

        // 3) Wer am ehesten gemeint ist: nah und in Blickrichtung.
        float s_BestScore = 1e9f;

        for (const NpcSnapshot* s_Npc : s_Hearers) {
            const bool s_InFront = s_Npc->m_ViewAngle < 60.f;

            if (!s_InFront && s_Npc->m_Distance > 1.8f) {
                continue;
            }

            const float s_Score = s_Npc->m_Distance + s_Npc->m_ViewAngle * 0.06f;

            if (s_Score < s_BestScore) {
                s_BestScore = s_Score;
                s_Best = s_Npc;
            }
        }

        // 4) Niemand vor 47: der Naechste, wenn er wirklich nah ist.
        if (!s_Best && s_Hearers.front()->m_Distance < 4.f) {
            s_Best = s_Hearers.front();
        }

        if (s_Best) {
            s_Choice.m_Key = s_Best->m_Key;
        }

        return s_Choice;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Antwortformat
    // -----------------------------------------------------------------------------------------------------------------

    // Reihenfolge zaehlt: erst Gefuehl, dann der Satz – sobald der fertig ist, wird er schon gesprochen,
    // waehrend das Modell noch die Zahlen dahinter schreibt.
    nlohmann::ordered_json ReplySchema() {
        return nlohmann::ordered_json {
            { "type", "object" },
            { "properties", {
                { "emotion", { { "type", "string" }, { "enum", {
                    "neutral", "friendly", "suspicious", "nervous", "scared", "angry", "panicked", "amused", "annoyed"
                } } } },
                { "say", { { "type", "string" } } },
                { "action", { { "type", "string" }, { "enum", { "none", "hold_fire", "open_fire", "stand_down" } } } },
                { "trust", { { "type", "integer" } } },
                { "suspicion", { { "type", "integer" } } },
                { "fear", { { "type", "integer" } } },
            } },
            { "required", { "emotion", "say", "action", "trust", "suspicion", "fear" } },
        };
    }

    namespace {
        // Liest einen JSON-String ab p_Pos (zeigt auf das oeffnende "). false, solange er noch nicht komplett ist.
        bool ReadJsonString(const std::string& p_Text, size_t& p_Pos, std::string& p_Out) {
            if (p_Pos >= p_Text.size() || p_Text[p_Pos] != '"') {
                return false;
            }

            for (size_t i = p_Pos + 1; i < p_Text.size(); ++i) {
                if (p_Text[i] == '\\') {
                    ++i;
                    continue;
                }

                if (p_Text[i] == '"') {
                    try {
                        p_Out = json::parse(p_Text.substr(p_Pos, i - p_Pos + 1)).get<std::string>();
                    }
                    catch (...) {
                        return false;
                    }

                    p_Pos = i + 1;
                    return true;
                }
            }

            return false;
        }
    }

    bool ExtractEarlyReply(const std::string& p_Partial, std::string& p_Say, std::string& p_Emotion) {
        size_t i = p_Partial.find('{');

        if (i == std::string::npos) {
            return false;
        }

        ++i;
        bool s_HaveSay = false;
        bool s_HaveEmotion = false;
        const auto s_Skip = [&]() {
            while (i < p_Partial.size() && (std::isspace(static_cast<unsigned char>(p_Partial[i])) || p_Partial[i] == ',')) {
                ++i;
            }
        };

        while (!(s_HaveSay && s_HaveEmotion)) {
            s_Skip();
            std::string s_Key;

            if (i >= p_Partial.size() || p_Partial[i] == '}' || !ReadJsonString(p_Partial, i, s_Key)) {
                break;
            }

            s_Skip();

            if (i >= p_Partial.size() || p_Partial[i] != ':') {
                break;
            }

            ++i;
            s_Skip();

            if (i >= p_Partial.size()) {
                break;
            }

            if (p_Partial[i] == '"') {
                std::string s_Value;

                if (!ReadJsonString(p_Partial, i, s_Value)) {
                    break;
                }

                if (s_Key == "say") {
                    p_Say = s_Value;
                    s_HaveSay = true;
                }
                else if (s_Key == "emotion") {
                    p_Emotion = s_Value;
                    s_HaveEmotion = true;
                }
            }
            else {
                // Zahl o. Ae.: erst weiter, wenn sie abgeschlossen ist.
                const size_t s_End = p_Partial.find_first_of(",}", i);

                if (s_End == std::string::npos) {
                    break;
                }

                i = s_End;
            }
        }

        return s_HaveSay && s_HaveEmotion;
    }

    NpcAction ParseAction(const std::string& p_Text) {
        const std::string s_Lower = ToLowerAscii(Trim(p_Text));

        if (s_Lower == "hold_fire" || s_Lower == "hold" || s_Lower == "negotiate") return NpcAction::HoldFire;
        if (s_Lower == "open_fire" || s_Lower == "shoot" || s_Lower == "attack" || s_Lower == "fire") return NpcAction::OpenFire;
        if (s_Lower == "stand_down" || s_Lower == "let_go" || s_Lower == "calm_down" || s_Lower == "release") return NpcAction::StandDown;
        return NpcAction::None;
    }

    std::string StripAssistantPhrases(const std::string& p_Text) {
        // Kleine Modelle fallen gern in den Chatbot-Ton – solche Saetze fliegen raus.
        static const char* const c_Phrases[] = {
            "wie kann ich ihnen helfen", "wie kann ich dir helfen", "kann ich ihnen helfen", "kann ich dir helfen",
            "ich helfe gern", "ich helfe ihnen gern", "ich helfe dir gern", "gibt es sonst noch etwas", "noch etwas, womit",
            "schönen tag noch", "einen schönen tag", "how can i help", "can i help you", "happy to help", "anything else",
            "have a nice day", "als ki", "as an ai",
        };

        // In Saetze zerlegen (Satzzeichen bleiben am Satz).
        std::vector<std::string> s_Sentences;
        std::string s_Current;

        for (const char c : p_Text) {
            s_Current.push_back(c);

            if (c == '.' || c == '!' || c == '?') {
                s_Sentences.push_back(s_Current);
                s_Current.clear();
            }
        }

        if (!Trim(s_Current).empty()) {
            s_Sentences.push_back(s_Current);
        }

        std::string s_Result;

        for (const auto& s_Sentence : s_Sentences) {
            const std::string s_Lower = ToLowerUtf8(s_Sentence);
            bool s_Bad = false;

            for (const char* s_Phrase : c_Phrases) {
                s_Bad |= s_Lower.find(s_Phrase) != std::string::npos;
            }

            if (!s_Bad) {
                s_Result += s_Sentence;
            }
        }

        return Trim(s_Result);
    }

    NpcReply ParseReply(const std::string& p_Content) {
        NpcReply s_Reply = ParseReplyRaw(p_Content);

        if (s_Reply.m_Valid && !s_Reply.m_Say.empty()) {
            const std::string s_Clean = StripAssistantPhrases(s_Reply.m_Say);

            // Bestand die Antwort NUR aus Floskeln, lieber ein knappes, menschliches "Hm?" als gar nichts.
            s_Reply.m_Say = s_Clean.empty() ? "Hm?" : s_Clean;
        }

        return s_Reply;
    }

    NpcReply ParseReplyRaw(const std::string& p_Content) {
        NpcReply s_Reply;
        const std::string s_Text = Trim(p_Content);

        const auto s_TryJson = [&](const std::string& p_Json) {
            try {
                const json s_Json = json::parse(p_Json);

                if (!s_Json.is_object()) {
                    return false;
                }

                const auto s_Int = [&](const char* p_Key) {
                    if (!s_Json.contains(p_Key)) return 0;
                    const auto& s_Value = s_Json[p_Key];
                    if (s_Value.is_number()) return std::clamp(static_cast<int>(s_Value.get<double>()), -30, 30);
                    if (s_Value.is_string()) {
                        try { return std::clamp(std::stoi(s_Value.get<std::string>()), -30, 30); } catch (...) {}
                    }
                    return 0;
                };

                s_Reply.m_Say = s_Json.contains("say") && s_Json["say"].is_string() ? s_Json["say"].get<std::string>() : "";
                s_Reply.m_Emotion = s_Json.contains("emotion") && s_Json["emotion"].is_string()
                    ? ToLowerAscii(s_Json["emotion"].get<std::string>())
                    : "neutral";
                s_Reply.m_Action = s_Json.contains("action") && s_Json["action"].is_string()
                    ? ParseAction(s_Json["action"].get<std::string>())
                    : NpcAction::None;
                s_Reply.m_Trust = s_Int("trust");
                s_Reply.m_Suspicion = s_Int("suspicion");
                s_Reply.m_Fear = s_Int("fear");
                s_Reply.m_Say = CleanSpokenText(s_Reply.m_Say);
                s_Reply.m_Valid = true;
                return true;
            }
            catch (...) {
                return false;
            }
        };

        if (s_TryJson(s_Text)) {
            return s_Reply;
        }

        // Manche Modelle setzen Text oder ```json-Bloecke drumherum: das erste {...} herausschneiden.
        const size_t s_Open = s_Text.find('{');
        const size_t s_Close = s_Text.rfind('}');

        if (s_Open != std::string::npos && s_Close != std::string::npos && s_Close > s_Open &&
            s_TryJson(s_Text.substr(s_Open, s_Close - s_Open + 1))) {
            return s_Reply;
        }

        // Notfalls den Text selbst als gesprochenen Satz nehmen.
        s_Reply.m_Say = CleanSpokenText(s_Text);
        s_Reply.m_Valid = !s_Reply.m_Say.empty();
        return s_Reply;
    }

    std::string ActionLabel(NpcAction p_Action, Language p_Language) {
        const bool s_De = p_Language == Language::German;

        switch (p_Action) {
            case NpcAction::HoldFire: return s_De ? "verhandelt weiter" : "keeps negotiating";
            case NpcAction::OpenFire: return s_De ? "eröffnet das Feuer" : "opens fire";
            case NpcAction::StandDown: return s_De ? "lässt dich gehen" : "stands down";
            default: return "";
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Prompt-Bausteine
    // -----------------------------------------------------------------------------------------------------------------

    std::string SystemPrompt(Language p_Language) {
        if (p_Language == Language::German) {
            return
                "Du bist ein echter Mensch an deinem Arbeitsplatz oder auf einer Veranstaltung – keine Spielfigur und KEIN Assistent. "
                "Ein Fremder spricht dich an. Du weißt nicht, wer er ist – du siehst nur einen großen Mann mit Glatze, "
                "seine Kleidung und was er tut.\n\n"
                "Regeln:\n"
                "- Hör GENAU zu und reagiere auf das, was er WÖRTLICH gesagt hat: beantworte seine Frage, geh auf seine Bitte, "
                "Lüge, Beleidigung, Drohung oder seinen Witz ein. Keine allgemeinen Floskeln.\n"
                "- Du bist kein Kundenservice. Sag NIE Dinge wie \"Wie kann ich helfen?\", \"Ich helfe gern weiter\", "
                "\"Gibt es sonst noch etwas?\" oder \"Schönen Tag noch\". Du hast eigene Sorgen, Aufgaben, Launen und Meinungen.\n"
                "- Sprich wie ein echter Mensch: umgangssprachlich, manchmal unhöflich, ungeduldig, sarkastisch oder unsicher – passend zu deiner Persönlichkeit. "
                "Wer beleidigt wird, ist beleidigt oder wird wütend. Befehle von Fremden befolgst du nur, wenn sie Autorität haben oder dir Angst machen.\n"
                "- Antworte kurz: 1 bis 2 Sätze, höchstens 25 Wörter. Keine Regieanweisungen, keine Sternchen, keine Emojis.\n"
                "- Sprich Deutsch.\n"
                "Beispiel für den Ton (NIE wörtlich übernehmen, nur den Stil):\n"
                "  Fremder: \"Schönes Wetter heute.\" -> \"Kann sein. Ich steh seit sechs Stunden hier drin und hab nix davon.\"\n"
                "- Du kennst nur, was unter [Lage] steht, dein Gedächtnis und deinen Alltag an diesem Ort. Erfinde keine Spielmechaniken.\n"
                "- Reagiere glaubwürdig: Fremde in Sperrbereichen fallen auf, sichtbare Waffen machen Angst, Drohungen machen Angst oder wütend, "
                "Höflichkeit und gute Ausreden können überzeugen, Bestechung kann klappen – je nach Charakter.\n"
                "- Gewalt ist das letzte Mittel. Als bewaffnete Wache warnst und verhandelst du zuerst (Waffe weg, Hände hoch, Bereich verlassen). "
                "Du schießt nur, wenn die Person angreift, mit einer Waffe droht und nicht aufgibt oder dich offensichtlich verarscht.\n"
                "- Als Zivilist hast du keine Waffe: Du bist höflich, genervt, neugierig oder ängstlich – je nach Lage.\n"
                "- Wurdest du angeschossen, gestochen oder geschlagen oder hast du so etwas gesehen, reagierst du wie ein echter Mensch: "
                "Schmerz, Schock, Panik, Flehen oder Wut – niemals entspannt oder freundlich, als wäre nichts passiert.\n"
                "- Über wichtige Personen redest du nur, wenn du der Person vertraust, Angst vor ihr hast oder sie dich wirklich überzeugt.\n\n"
                "Antworte AUSSCHLIESSLICH mit JSON:\n"
                "{\"say\": \"gesprochener Text\", \"emotion\": \"neutral|friendly|suspicious|nervous|scared|angry|panicked|amused|annoyed\", "
                "\"action\": \"none|hold_fire|open_fire|stand_down\", \"trust\": -20..20, \"suspicion\": -20..20, \"fear\": -20..20}\n"
                "- action nur, wenn du bewaffnet bist und gerade eine Konfrontation läuft: hold_fire = weiter verhandeln, "
                "stand_down = Person gehen lassen / dich beruhigen, open_fire = Verhandlung gescheitert, du schießt. Sonst \"none\".\n"
                "- trust/suspicion/fear: wie sich deine Haltung zur Person durch das Gesagte ändert (0 = unverändert).\n"
                "- Wenn du nichts sagen willst (z. B. bei mitgehörten Gesprächen, die dich nicht betreffen): \"say\": \"\".";
        }

        return
            "You are a real person at your workplace or at an event – not a game character and NOT an assistant. "
            "A stranger is talking to you. You don't know who he is – you only see a tall bald man, his clothes and what he does.\n\n"
            "Rules:\n"
            "- Listen CLOSELY and react to what he LITERALLY said: answer his question, respond to his request, lie, insult, threat or joke. No generic phrases.\n"
            "- You are not customer service. NEVER say things like \"How can I help?\", \"Happy to help\", \"Anything else?\" or \"Have a nice day\". "
            "You have your own worries, tasks, moods and opinions.\n"
            "- Talk like a real person: casual, sometimes rude, impatient, sarcastic or unsure – fitting your personality. "
            "If insulted, you are offended or angry. You only obey strangers if they have authority or scare you.\n"
            "- Keep it short: 1 to 2 sentences, at most 25 words. No stage directions, no asterisks, no emojis.\n"
            "- Speak English.\n"
            "- You only know what is listed under [Situation], your memory and your everyday life here. Do not invent game mechanics.\n"
            "- React believably: strangers in restricted areas stand out, visible weapons are frightening, threats make you scared or angry, "
            "politeness and good excuses can convince you, bribes may work – depending on your character.\n"
            "- Violence is the last resort. As an armed guard you warn and negotiate first (drop the weapon, hands up, leave the area). "
            "You only shoot if the person attacks, threatens you with a weapon and does not give up, or is obviously messing with you.\n"
            "- As a civilian you are unarmed: polite, annoyed, curious or scared – depending on the situation.\n"
            "- If you were shot, stabbed or hit, or saw something like that, react like a real person: "
            "pain, shock, panic, pleading or anger – never relaxed or friendly as if nothing happened.\n"
            "- You only talk about important people if you trust the person, are afraid of them, or they truly convince you.\n\n"
            "Reply ONLY with JSON:\n"
            "{\"say\": \"spoken text\", \"emotion\": \"neutral|friendly|suspicious|nervous|scared|angry|panicked|amused|annoyed\", "
            "\"action\": \"none|hold_fire|open_fire|stand_down\", \"trust\": -20..20, \"suspicion\": -20..20, \"fear\": -20..20}\n"
            "- action only if you are armed and a confrontation is happening: hold_fire = keep negotiating, "
            "stand_down = let the person go / calm down, open_fire = negotiation failed, you shoot. Otherwise \"none\".\n"
            "- trust/suspicion/fear: how your attitude towards the person changes (0 = unchanged).\n"
            "- If you do not want to say anything (e.g. overheard talk that does not concern you): \"say\": \"\".";
    }

    std::string DisplayName(const NpcSnapshot& p_Npc) {
        if (LooksLikePersonName(p_Npc.m_Name)) {
            return p_Npc.m_Name;
        }

        if (!p_Npc.m_Outfit.empty()) {
            return p_Npc.m_Outfit;
        }

        return p_Npc.m_Name.empty() ? "NPC" : p_Npc.m_Name;
    }

    std::string DescribeAppearance(const NpcSnapshot& p_Npc, const WorldSnapshot& p_World, Language p_Language) {
        const bool s_De = p_Language == Language::German;
        const PlayerSnapshot& s_Player = p_World.m_Player;
        std::string s_Text;

        if (s_Player.m_InSuit || s_Player.m_Outfit.empty()) {
            s_Text = s_De ? "trägt einen eleganten dunklen Anzug – kein Angestellter, eher ein Gast oder Geschäftsmann"
                          : "wears an elegant dark suit – not staff, more like a guest or businessman";
        }
        else if (!p_Npc.m_Outfit.empty() && ToLowerAscii(s_Player.m_Outfit) == ToLowerAscii(p_Npc.m_Outfit)) {
            s_Text = s_De ? "trägt dieselbe Kleidung wie du (" + s_Player.m_Outfit + ") – also angeblich ein Kollege"
                          : "wears the same outfit as you (" + s_Player.m_Outfit + ") – supposedly a colleague";

            if (p_Npc.m_Enforcer) {
                s_Text += s_De ? ". Du kennst alle Kollegen persönlich – DIESEN Mann hast du hier noch nie gesehen"
                               : ". You know all your colleagues personally – you have NEVER seen THIS man here";
            }
        }
        else {
            s_Text = (s_De ? "trägt die Kleidung: " : "wears: ") + s_Player.m_Outfit;

            if (s_Player.m_OutfitIsGuard) {
                s_Text += s_De ? " (Sicherheitspersonal)" : " (security staff)";
            }

            if (p_Npc.m_Enforcer) {
                s_Text += s_De ? ". Du kennst die Leute in dieser Kleidung – ihn kennst du nicht"
                               : ". You know the people in that outfit – you don't know him";
            }
        }

        return s_Text;
    }

    std::string PersonalityFor(const NpcSnapshot& p_Npc, Language p_Language) {
        static const char* s_GuardDe[] = {
            "pflichtbewusst und korrekt, hält sich an Vorschriften",
            "gelangweilt vom Dienst, will nur keinen Ärger",
            "nervös und schreckhaft, aber bemüht, souverän zu wirken",
            "misstrauisch und kurz angebunden",
            "selbstbewusst, etwas großspurig",
            "erfahren und ruhig, lässt sich nicht so leicht aus der Fassung bringen",
            "müde nach einer langen Schicht, ein bisschen gereizt",
            "freundlich, plaudert gern, wenn es ruhig ist",
        };
        static const char* s_CivDe[] = {
            "freundlich und gesprächig",
            "gestresst und in Eile",
            "neugierig und tratscht gerne",
            "arrogant und herablassend gegenüber Fremden",
            "schüchtern und unsicher",
            "humorvoll, macht gern einen Witz",
            "genervt von der Arbeit und dem Chef",
            "höflich, aber distanziert",
            "abergläubisch und leicht zu beeindrucken",
            "zynisch und sarkastisch",
        };
        static const char* s_GuardEn[] = {
            "dutiful and by-the-book",
            "bored with the job, just wants no trouble",
            "nervous and jumpy but trying to look in control",
            "suspicious and curt",
            "confident, a bit cocky",
            "experienced and calm, hard to rattle",
            "tired after a long shift, a little irritable",
            "friendly, likes to chat when it's quiet",
        };
        static const char* s_CivEn[] = {
            "friendly and chatty",
            "stressed and in a hurry",
            "curious and loves gossip",
            "arrogant and condescending to strangers",
            "shy and insecure",
            "humorous, likes to crack a joke",
            "annoyed with work and the boss",
            "polite but distant",
            "superstitious and easily impressed",
            "cynical and sarcastic",
        };

        const uint32_t s_Hash = StableHash(p_Npc.m_Name + "|" + p_Npc.m_Outfit + "|" + std::to_string(p_Npc.m_Key % 97));
        const bool s_De = p_Language == Language::German;

        if (p_Npc.m_Guard) {
            return s_De ? s_GuardDe[s_Hash % std::size(s_GuardDe)] : s_GuardEn[s_Hash % std::size(s_GuardEn)];
        }

        return s_De ? s_CivDe[s_Hash % std::size(s_CivDe)] : s_CivEn[s_Hash % std::size(s_CivEn)];
    }

    std::string WarningLine(uint64_t p_Seed, bool p_Last, Language p_Language) {
        static const char* s_FirstDe[] = {
            "Keine Bewegung!", "Stehen bleiben! Hände hoch!", "Waffe runter – sofort!",
            "Ganz ruhig... keine falsche Bewegung!", "Halt! Noch ein Schritt und ich schieße!",
            "Hände dahin, wo ich sie sehen kann!", "Hey! Stopp! Keine Dummheiten!",
        };
        static const char* s_LastDe[] = {
            "Letzte Warnung!", "Ich sag's nicht noch mal!", "Jetzt! Oder ich schieße!",
            "Ich zähl bis drei!", "Das war's – letzte Chance!",
        };
        static const char* s_FirstEn[] = {
            "Don't move!", "Freeze! Hands up!", "Drop the weapon – now!", "Easy... no sudden moves!",
            "Stop! One more step and I shoot!", "Hands where I can see them!", "Hey! Stop! Don't do anything stupid!",
        };
        static const char* s_LastEn[] = {
            "Last warning!", "I won't say it again!", "Now! Or I'll shoot!", "I'm counting to three!", "That's it – last chance!",
        };

        const auto s_Index = static_cast<size_t>((p_Seed * 2654435761ull) >> 7);

        if (p_Language == Language::German) {
            return p_Last ? s_LastDe[s_Index % std::size(s_LastDe)] : s_FirstDe[s_Index % std::size(s_FirstDe)];
        }

        return p_Last ? s_LastEn[s_Index % std::size(s_LastEn)] : s_FirstEn[s_Index % std::size(s_FirstEn)];
    }

    void EmotionToStyle(const std::string& p_Emotion, int& p_Rate, int& p_Pitch, int& p_Volume) {
        if (p_Emotion == "angry") {
            p_Rate += 1;
            p_Pitch -= 1;
            p_Volume = 100;
        }
        else if (p_Emotion == "panicked") {
            p_Rate += 3;
            p_Pitch += 3;
            p_Volume = 100;
        }
        else if (p_Emotion == "scared" || p_Emotion == "nervous") {
            p_Rate += 2;
            p_Pitch += 2;
            p_Volume = 85;
        }
        else if (p_Emotion == "suspicious") {
            p_Rate -= 1;
            p_Volume = 90;
        }
        else if (p_Emotion == "friendly" || p_Emotion == "amused") {
            p_Pitch += 1;
            p_Volume = 95;
        }
        else if (p_Emotion == "annoyed") {
            p_Rate += 1;
            p_Volume = 95;
        }
        else {
            p_Volume = 92;
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // DialogueBook
    // -----------------------------------------------------------------------------------------------------------------

    NpcMind& DialogueBook::Mind(const NpcSnapshot& p_Npc, const WorldSnapshot& p_World, Language p_Language) {
        NpcMind& s_Mind = m_Minds[p_Npc.m_Key];

        if (!s_Mind.m_Initialized) {
            s_Mind.m_Initialized = true;
            s_Mind.m_Key = p_Npc.m_Key;
            s_Mind.m_DisplayName = DisplayName(p_Npc);
            s_Mind.m_Personality = PersonalityFor(p_Npc, p_Language);

            // Stimme: pro NPC leicht anders, damit nicht alle gleich klingen.
            const uint32_t s_Hash = StableHash(p_Npc.m_Name + p_Npc.m_Outfit + std::to_string(p_Npc.m_Key));
            // Maenner deutlich tiefer (es gibt meist nur eine deutsche Maennerstimme), Frauen eher hoeher.
            s_Mind.m_VoicePitch = p_Npc.m_Female ? static_cast<int>(s_Hash % 5) : static_cast<int>(s_Hash % 7) - 6;
            s_Mind.m_VoiceRate = static_cast<int>((s_Hash >> 8) % 3) - 1;

            // Erste Einschaetzung von 47 je nach Lage.
            const AiSnapshot& s_Ai = p_World.m_Ai;
            s_Mind.m_Trust = 40;
            s_Mind.m_Suspicion = 10;
            s_Mind.m_Fear = 0;

            if (!p_World.m_Player.m_InSuit && ToLowerAscii(p_World.m_Player.m_Outfit) == ToLowerAscii(p_Npc.m_Outfit)) {
                s_Mind.m_Trust += 15;
            }

            if (p_Npc.m_Enforcer) {
                s_Mind.m_Suspicion += 30;
            }

            if (s_Ai.m_Trespassing) {
                s_Mind.m_Suspicion += 25;
            }

            if (s_Ai.m_IllegalWeapon) {
                s_Mind.m_Suspicion += 30;
                s_Mind.m_Fear += p_Npc.m_Guard ? 15 : 45;
            }

            if (s_Ai.m_DisguiseBroken) {
                s_Mind.m_Suspicion += 30;
            }

            if (s_Ai.m_Tension >= 4) {
                s_Mind.m_Suspicion += 10;
                s_Mind.m_Fear += 10;
            }

            s_Mind.m_Trust = std::clamp(s_Mind.m_Trust, 0, 100);
            s_Mind.m_Suspicion = std::clamp(s_Mind.m_Suspicion, 0, 100);
            s_Mind.m_Fear = std::clamp(s_Mind.m_Fear, 0, 100);
        }

        return s_Mind;
    }

    NpcMind* DialogueBook::Find(uint64_t p_Key) {
        const auto s_It = m_Minds.find(p_Key);
        return s_It == m_Minds.end() ? nullptr : &s_It->second;
    }

    void DialogueBook::Clear() {
        m_Minds.clear();
        m_Log.clear();
    }

    void DialogueBook::RecordPlayerLine(
        const std::string& p_Text, VoiceVolume p_Volume, uint64_t p_Addressee, const std::string& p_AddresseeName,
        const std::vector<uint64_t>& p_Hearers, double p_Time
    ) {
        for (const uint64_t s_Key : p_Hearers) {
            auto s_It = m_Minds.find(s_Key);

            if (s_It == m_Minds.end()) {
                continue;
            }

            DialogueLine s_Line;
            s_Line.m_Speaker = "47";
            s_Line.m_Text = p_Text;
            s_Line.m_FromPlayer = true;
            s_Line.m_Overheard = s_Key != p_Addressee;
            s_Line.m_AddressedTo = p_AddresseeName;
            s_Line.m_Volume = p_Volume;
            s_Line.m_Time = p_Time;

            auto& s_Lines = s_It->second.m_Lines;
            s_Lines.push_back(std::move(s_Line));

            while (s_Lines.size() > c_MaxLinesPerMind) {
                s_Lines.pop_front();
            }
        }
    }

    void DialogueBook::RecordNpcLine(
        uint64_t p_Speaker, const std::string& p_SpeakerName, const NpcReply& p_Reply,
        const std::vector<uint64_t>& p_Hearers, double p_Time
    ) {
        if (p_Reply.m_Say.empty()) {
            return;
        }

        const auto s_Add = [&](uint64_t p_Key, bool p_Own) {
            auto s_It = m_Minds.find(p_Key);

            if (s_It == m_Minds.end()) {
                return;
            }

            DialogueLine s_Line;
            s_Line.m_Speaker = p_SpeakerName;
            s_Line.m_Text = p_Reply.m_Say;
            s_Line.m_Own = p_Own;
            s_Line.m_Overheard = !p_Own;
            s_Line.m_Emotion = p_Reply.m_Emotion;
            s_Line.m_Time = p_Time;

            auto& s_Lines = s_It->second.m_Lines;
            s_Lines.push_back(std::move(s_Line));

            while (s_Lines.size() > c_MaxLinesPerMind) {
                s_Lines.pop_front();
            }
        };

        s_Add(p_Speaker, true);

        for (const uint64_t s_Key : p_Hearers) {
            if (s_Key != p_Speaker) {
                s_Add(s_Key, false);
            }
        }

        if (auto* s_Mind = Find(p_Speaker)) {
            s_Mind->m_LastSpoke = p_Time;
            ++s_Mind->m_Exchanges;
        }
    }

    void DialogueBook::ApplyAttitude(uint64_t p_Key, const NpcReply& p_Reply) {
        if (auto* s_Mind = Find(p_Key)) {
            s_Mind->m_Trust = std::clamp(s_Mind->m_Trust + p_Reply.m_Trust, 0, 100);
            s_Mind->m_Suspicion = std::clamp(s_Mind->m_Suspicion + p_Reply.m_Suspicion, 0, 100);
            s_Mind->m_Fear = std::clamp(s_Mind->m_Fear + p_Reply.m_Fear, 0, 100);
        }
    }

    void DialogueBook::AddExperience(uint64_t p_Key, const std::string& p_Text, int p_Trust, int p_Suspicion, int p_Fear) {
        NpcMind* s_Mind = Find(p_Key);

        if (!s_Mind) {
            return;
        }

        if (s_Mind->m_Experiences.empty() || s_Mind->m_Experiences.back() != p_Text) {
            s_Mind->m_Experiences.push_back(p_Text);

            while (s_Mind->m_Experiences.size() > 6) {
                s_Mind->m_Experiences.pop_front();
            }
        }

        s_Mind->m_Trust = std::clamp(s_Mind->m_Trust + p_Trust, 0, 100);
        s_Mind->m_Suspicion = std::clamp(s_Mind->m_Suspicion + p_Suspicion, 0, 100);
        s_Mind->m_Fear = std::clamp(s_Mind->m_Fear + p_Fear, 0, 100);
    }

    std::vector<ChatMessage> DialogueBook::BuildMessages(const PromptContext& p_Context) {
        const Settings& s_Settings = *p_Context.m_Settings;
        const WorldSnapshot& s_World = *p_Context.m_World;
        const NpcSnapshot& s_Npc = *p_Context.m_Npc;
        const Language s_Lang = s_Settings.m_Language;
        const bool s_De = s_Lang == Language::German;

        NpcMind& s_Mind = Mind(s_Npc, s_World, s_Lang);
        std::vector<ChatMessage> s_Messages;

        // 1) Feste Regeln – fuer alle NPCs identisch, damit der Server sie zwischenspeichern kann.
        Push(s_Messages, "system", SystemPrompt(s_Lang));

        // 2) Wer bin ich?
        {
            std::string s_Persona = s_De ? "[Deine Rolle]\n" : "[Your role]\n";

            // Namenlose NPCs bekommen fest einen Vornamen – eine Anweisung wie "denk dir einen aus" plappern kleine
            // Modelle sonst einfach nach.
            if (LooksLikePersonName(s_Npc.m_Name)) {
                s_Persona += "Name: " + s_Npc.m_Name + "\n";
            }
            else {
                static const char* const c_Male[] = { "Marco", "Thomas", "Daniel", "Luca", "Karim", "Kenji", "Pierre", "Jonas", "Viktor", "Samuel" };
                static const char* const c_Female[] = { "Anna", "Sophie", "Elena", "Mia", "Yuki", "Claire", "Sara", "Lena", "Nadia", "Julia" };
                const uint32_t s_NameHash = StableHash(std::to_string(s_Npc.m_Key) + s_Npc.m_Outfit);
                s_Persona += std::string("Name: ") + (s_Npc.m_Female ? c_Female[s_NameHash % 10] : c_Male[s_NameHash % 10]) + "\n";
            }

            s_Persona += (s_De ? "Rolle: " : "Role: ") + RoleDescription(s_Npc, s_Lang) + "\n";
            s_Persona += (s_De ? "Geschlecht: " : "Gender: ") +
                std::string(s_Npc.m_Female ? (s_De ? "weiblich" : "female") : (s_De ? "männlich" : "male")) + "\n";
            s_Persona += (s_De ? "Persönlichkeit: " : "Personality: ") + s_Mind.m_Personality + "\n";
            s_Persona += (s_De ? "Ort: " : "Place: ") + s_World.m_LocationName + "\n";

            if (s_Npc.m_Target) {
                s_Persona += s_De ? "Du bist eine wichtige, einflussreiche Person hier.\n"
                                  : "You are an important, influential person here.\n";
            }

            Push(s_Messages, "system", s_Persona);
        }

        // 3) Gedaechtnis: was dieser NPC gesagt und gehoert hat.
        {
            const size_t s_MaxLines = static_cast<size_t>(std::max(0, s_Settings.m_HistoryTurns)) * 2;
            const size_t s_Start = s_Mind.m_Lines.size() > s_MaxLines ? s_Mind.m_Lines.size() - s_MaxLines : 0;

            for (size_t i = s_Start; i < s_Mind.m_Lines.size(); ++i) {
                const DialogueLine& s_Line = s_Mind.m_Lines[i];

                if (s_Line.m_Own) {
                    Push(s_Messages, "assistant", nlohmann::ordered_json {
                        { "emotion", s_Line.m_Emotion.empty() ? "neutral" : s_Line.m_Emotion }, { "say", s_Line.m_Text },
                        { "action", "none" }, { "trust", 0 }, { "suspicion", 0 }, { "fear", 0 }
                    }.dump());
                    continue;
                }

                std::string s_Prefix;

                if (s_Line.m_FromPlayer && !s_Line.m_Overheard) {
                    s_Prefix = s_De ? "[Die Person sagt " : "[The person says ";
                    s_Prefix += VolumeWords(s_Line.m_Volume, s_Lang);
                    s_Prefix += "]: ";
                }
                else if (s_Line.m_FromPlayer) {
                    s_Prefix = s_De ? "[Mitgehört – die Person sagt zu " : "[Overheard – the person says to ";
                    s_Prefix += s_Line.m_AddressedTo + "]: ";
                }
                else {
                    s_Prefix = "[" + s_Line.m_Speaker + (s_De ? " sagt]: " : " says]: ");
                }

                Push(s_Messages, "user", s_Prefix + "\"" + s_Line.m_Text + "\"");
            }
        }

        // 4) Aktuelle Lage + Anlass.
        std::string s_Situation = s_De ? "[Lage]\n" : "[Situation]\n";
        s_Situation += (s_De ? "Stimmung hier: " : "Mood here: ") + DescribeTension(s_World.m_Ai.m_Tension, s_Lang) + ".\n";
        s_Situation += (s_De ? "Du " : "You ") + DescribeBehavior(s_Npc.m_Behavior, s_Lang) + ".";

        if (s_Npc.m_WeaponOut) {
            s_Situation += s_De ? " Deine Waffe ist gezogen." : " Your weapon is drawn.";
        }

        if (s_Npc.m_Health < 60.f) {
            s_Situation += s_De ? " Du bist verletzt." : " You are hurt.";
        }

        s_Situation += "\n";

        char s_Distance[32];
        snprintf(s_Distance, sizeof(s_Distance), "%.0f", s_Npc.m_Distance);

        s_Situation += (s_De ? "Die Person vor dir " : "The person in front of you ") +
            DescribeAppearance(s_Npc, s_World, s_Lang) + ". ";

        if (s_World.m_Ai.m_IllegalWeapon) {
            s_Situation += s_De ? "Sie hält sichtbar eine Waffe in der Hand. " : "They are visibly holding a weapon. ";
        }

        if (s_World.m_Ai.m_Trespassing) {
            s_Situation += s_De ? "Sie ist in einem Bereich, in dem sie nichts zu suchen hat. "
                                : "They are in an area where they are not allowed. ";
        }

        if (s_World.m_Ai.m_DisguiseBroken) {
            s_Situation += s_De ? "Man sucht nach einem Eindringling, auf den die Beschreibung passt. "
                                : "People are looking for an intruder matching their description. ";
        }
        else if (s_World.m_Ai.m_DisguiseSuspicious) {
            s_Situation += s_De ? "Irgendwas an ihr kommt dir komisch vor. " : "Something about them seems off. ";
        }

        s_Situation += (s_De ? "Abstand: " : "Distance: ") + std::string(s_Distance) + " m.\n";

        // Ruf: getoetete Bewaffnete machen allen Angst.
        if (const std::string s_Reputation = ReputationText(p_Context.m_ArmedKills, p_Context.m_CivilianKills,
                                                            s_Npc.IsArmed() && !p_Context.m_Disarmed, s_Lang);
            !s_Reputation.empty()) {
            s_Situation += (s_De ? "WAS MAN ÜBER IHN WEISS: " : "WHAT PEOPLE KNOW ABOUT HIM: ") + s_Reputation + "\n";
        }

        if (p_Context.m_Disarmed) {
            s_Situation += s_De
                ? "DU WURDEST GERADE ENTWAFFNET: Er hat dir die Waffe aus der Hand geschossen/geschlagen. Du bist wehrlos, hast Todesangst "
                  "und bettelst um dein Leben. Kein Widerstand, keine Drohungen.\n"
                : "YOU WERE JUST DISARMED: he shot/knocked the weapon out of your hand. You are defenceless, terrified "
                  "and begging for your life. No resistance, no threats.\n";
        }

        if (p_Context.m_Threatened) {
            const bool s_CanResist = s_Npc.IsArmed() && !p_Context.m_Disarmed && p_Context.m_ArmedKills < 3;
            s_Situation += s_De
                ? (s_CanResist
                    ? "ER BEDROHT DICH GERADE MIT DEM TOD. Du bist bewaffnet, aber er macht dir Angst – du versuchst die Lage zu beruhigen und ihn zum Aufgeben zu bringen.\n"
                    : "ER BEDROHT DICH GERADE MIT DEM TOD und ist bewaffnet. Du bist in Panik: du flehst, bettelst um dein Leben, "
                      "versprichst alles, gehorchst sofort und stotterst vor Angst. Emotion: panicked.\n")
                : (s_CanResist
                    ? "HE IS THREATENING TO KILL YOU. You are armed, but he scares you – you try to de-escalate and make him give up.\n"
                    : "HE IS THREATENING TO KILL YOU and he is armed. You panic: you plead, beg for your life, promise anything, "
                      "obey immediately and stammer with fear. Emotion: panicked.\n");
        }

        if (!s_Mind.m_Condition.empty()) {
            s_Situation += (s_De ? "DEIN KÖRPERLICHER ZUSTAND: " : "YOUR PHYSICAL CONDITION: ") + s_Mind.m_Condition + "\n";
        }

        if (!s_Mind.m_Experiences.empty()) {
            s_Situation += s_De ? "Was du mit dieser Person erlebt hast (reagiere entsprechend – Schmerz, Schock, Angst oder Wut):\n"
                                : "What you experienced with this person (react accordingly – pain, shock, fear or anger):\n";

            for (const auto& s_Experience : s_Mind.m_Experiences) {
                s_Situation += "- " + s_Experience + "\n";
            }
        }

        if (s_World.m_Ai.m_BodyFound) {
            s_Situation += s_De ? "Vorhin wurde eine Leiche oder eine bewusstlose Person gefunden.\n"
                                : "A body or an unconscious person was found earlier.\n";
        }

        if (p_Context.m_Gate == GateState::Warning) {
            char s_Seconds[32];
            snprintf(s_Seconds, sizeof(s_Seconds), "%.0f", std::max(1.f, p_Context.m_GateSecondsLeft));
            s_Situation += s_De
                ? "KONFRONTATION: Du zielst gerade mit deiner Waffe auf die Person und hast sie gewarnt. Du hast noch nicht geschossen. "
                  "Deine Geduld reicht noch etwa " + std::string(s_Seconds) + " Sekunden. Entscheide mit action.\n"
                : "CONFRONTATION: You are aiming your weapon at the person and have warned them. You have not fired yet. "
                  "Your patience lasts about " + std::string(s_Seconds) + " more seconds. Decide with action.\n";
        }
        else if (p_Context.m_Gate == GateState::Firing) {
            s_Situation += s_De ? "KONFRONTATION: Es fallen Schüsse – du kämpfst gerade mit dieser Person.\n"
                                : "CONFRONTATION: Shots are being fired – you are fighting this person.\n";
        }
        else if (p_Context.m_Gate == GateState::Persuaded) {
            s_Situation += s_De ? "Du hast vorhin beschlossen, die Person gehen zu lassen.\n"
                                : "You decided earlier to let the person go.\n";
        }

        if (s_Settings.m_NpcsKnowTargets && !s_Npc.m_Target) {
            std::string s_Known;

            for (const auto& s_Target : s_World.m_Targets) {
                if (!s_Target.m_Alive || s_Target.m_Key == s_Npc.m_Key) {
                    continue;
                }

                char s_TargetDistance[32];
                snprintf(s_TargetDistance, sizeof(s_TargetDistance), "%.0f", Distance(s_Npc.m_Pos, s_Target.m_Pos));
                s_Known += "- " + s_Target.m_Name + (s_De ? ": gerade etwa " : ": currently about ") + s_TargetDistance +
                    " m " + DescribeDirection(s_Npc.m_Pos, s_Target.m_Pos, s_Lang) + "\n";
            }

            if (!s_Known.empty()) {
                s_Situation += s_De ? "Was du über wichtige Personen hier weißt (nicht jedem erzählen!):\n"
                                    : "What you know about important people here (don't tell just anyone!):\n";
                s_Situation += s_Known;
            }
        }

        s_Situation += (s_De ? "Deine Haltung zur Person: Vertrauen " : "Your attitude towards the person: trust ") +
            Clamp100(s_Mind.m_Trust) + (s_De ? "/100, Misstrauen " : "/100, suspicion ") + Clamp100(s_Mind.m_Suspicion) +
            (s_De ? "/100, Angst " : "/100, fear ") + Clamp100(s_Mind.m_Fear) + "/100.\n\n";

        switch (p_Context.m_Trigger) {
            case DialogueTrigger::PlayerSpoke:
                s_Situation += (s_De ? "[Die Person sagt " : "[The person says ") +
                    std::string(VolumeWords(p_Context.m_Volume, s_Lang)) + (s_De ? " zu dir]: \"" : " to you]: \"") +
                    p_Context.m_PlayerText + "\"";
                break;

            case DialogueTrigger::Overheard:
                s_Situation += (s_De ? "[Du hast mitgehört, wie die Person " : "[You overheard the person say ") +
                    std::string(VolumeWords(p_Context.m_Volume, s_Lang)) + (s_De ? " zu " : " to ") + p_Context.m_AddressedTo +
                    (s_De ? " sagt]: \"" : "]: \"") + p_Context.m_PlayerText + "\"\n" +
                    (s_De ? "Reagiere nur kurz, wenn es dich betrifft oder beunruhigt – sonst \"say\": \"\"."
                          : "Only react briefly if it concerns or worries you – otherwise \"say\": \"\".");
                break;

            case DialogueTrigger::Initiate:
                s_Situation += s_De
                    ? "[Die Person steht schon eine Weile direkt neben dir und beobachtet dich.] Sprich sie von dir aus an – kurz und passend zu Rolle und Lage."
                    : "[The person has been standing right next to you for a while, watching you.] Address them yourself – briefly, fitting your role and the situation.";
                break;
        }

        Push(s_Messages, "user", s_Situation);
        return s_Messages;
    }

    std::vector<DialogueBook::LogEntry> DialogueBook::GlobalLog() const {
        std::scoped_lock s_Lock(m_Mutex);
        return { m_Log.begin(), m_Log.end() };
    }

    void DialogueBook::AddToGlobalLog(const LogEntry& p_Entry) {
        std::scoped_lock s_Lock(m_Mutex);
        m_Log.push_back(p_Entry);

        while (m_Log.size() > c_MaxGlobalLog) {
            m_Log.pop_front();
        }
    }
}
