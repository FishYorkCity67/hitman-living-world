// Render-Thread: Chatfeld, Untertitel, Sprechblasen, Hinweise und das Einstellungsfenster.

#include "LivingWorld.h"

#include "Log.h"
#include "Text.h"

#include <IconsMaterialDesign.h>

#include <Glacier/ZMath.h>

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace {
    constexpr ImVec4 c_Panel { 0.055f, 0.063f, 0.078f, 0.93f };
    constexpr ImVec4 c_PanelSoft { 0.055f, 0.063f, 0.078f, 0.72f };
    constexpr ImVec4 c_Border { 0.20f, 0.22f, 0.26f, 1.f };
    constexpr ImVec4 c_Text { 0.93f, 0.94f, 0.95f, 1.f };
    constexpr ImVec4 c_Muted { 0.58f, 0.61f, 0.66f, 1.f };
    constexpr ImVec4 c_Accent { 0.20f, 0.76f, 0.70f, 1.f };   // Living-World-Tuerkis
    constexpr ImVec4 c_Danger { 0.93f, 0.27f, 0.30f, 1.f };
    constexpr ImVec4 c_Warn { 0.96f, 0.70f, 0.25f, 1.f };
    constexpr ImVec4 c_Good { 0.30f, 0.80f, 0.50f, 1.f };
    constexpr ImVec4 c_PlayerName { 0.80f, 0.82f, 0.86f, 1.f };

    ImU32 Col(const ImVec4& p_Color, float p_Alpha = 1.f) {
        return ImGui::ColorConvertFloat4ToU32({ p_Color.x, p_Color.y, p_Color.z, p_Color.w * p_Alpha });
    }

    ImFont* FontOr(ImFont* p_Font) {
        return p_Font ? p_Font : ImGui::GetFont();
    }

    ImVec4 EmotionColor(const std::string& p_Emotion) {
        if (p_Emotion == "angry") return { 0.95f, 0.35f, 0.32f, 1.f };
        if (p_Emotion == "panicked" || p_Emotion == "scared") return { 0.98f, 0.62f, 0.25f, 1.f };
        if (p_Emotion == "nervous") return { 0.95f, 0.80f, 0.35f, 1.f };
        if (p_Emotion == "suspicious" || p_Emotion == "annoyed") return { 0.90f, 0.70f, 0.45f, 1.f };
        if (p_Emotion == "friendly" || p_Emotion == "amused") return { 0.45f, 0.85f, 0.55f, 1.f };
        return { 0.55f, 0.80f, 0.95f, 1.f };
    }

    const char* VolumeLabel(lw::VoiceVolume p_Volume, bool p_German) {
        switch (p_Volume) {
            case lw::VoiceVolume::Whisper: return p_German ? "Flüstern" : "Whisper";
            case lw::VoiceVolume::Shout: return p_German ? "Rufen" : "Shout";
            default: return "Normal";
        }
    }

    // Text mit Schatten, damit er auf hellem Hintergrund lesbar bleibt.
    void ShadowText(ImDrawList* p_List, ImFont* p_Font, float p_Size, ImVec2 p_Pos, ImU32 p_Color, const char* p_Text, float p_Wrap = 0.f, float p_Alpha = 1.f) {
        p_List->AddText(p_Font, p_Size, { p_Pos.x + 1.f, p_Pos.y + 1.f }, IM_COL32(0, 0, 0, static_cast<int>(200 * p_Alpha)), p_Text, nullptr, p_Wrap);
        p_List->AddText(p_Font, p_Size, p_Pos, p_Color, p_Text, nullptr, p_Wrap);
    }

    // Kleiner Tooltip-Helfer fuer Erklaerungen in den Einstellungen.
    void Help(const char* p_Text) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
            ImGui::TextUnformatted(p_Text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    bool InputString(const char* p_Label, std::string& p_Value, ImGuiInputTextFlags p_Flags = 0) {
        char s_Buffer[512];
        strncpy_s(s_Buffer, p_Value.c_str(), _TRUNCATE);

        if (ImGui::InputText(p_Label, s_Buffer, sizeof(s_Buffer), p_Flags)) {
            p_Value = s_Buffer;
            return true;
        }

        return false;
    }

    std::string FormatMb(uint64_t p_Bytes) {
        char s_Buffer[32];
        snprintf(s_Buffer, sizeof(s_Buffer), "%.0f MB", static_cast<double>(p_Bytes) / (1024.0 * 1024.0));
        return s_Buffer;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstieg
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::OnDrawMenu() {
    if (ImGui::Button(ICON_MD_RECORD_VOICE_OVER " LIVING WORLD")) {
        m_ShowSettings = !m_ShowSettings;
        m_FocusRequestedBySettings = false; // das SDK-Menue hat den Fokus bereits
    }
}

void LivingWorld::OnDrawUI(bool p_HasFocus) {
    m_UiHasFocus = p_HasFocus;

    HandleInput(CurrentSettings(), 0.0);

    if (m_RequestToggleSettings.exchange(false)) {
        m_ShowSettings = !m_ShowSettings;
        lw::log::Info("Einstellungsfenster {} (Taste).", m_ShowSettings ? "geoeffnet" : "geschlossen");

        if (m_ShowSettings && !p_HasFocus) {
            SDK()->RequestUIFocus();
            m_FocusRequestedBySettings = true;
        }
        else if (!m_ShowSettings && m_FocusRequestedBySettings && !m_ChatOpen) {
            SDK()->ReleaseUIFocus();
            m_FocusRequestedBySettings = false;
        }
    }

    if (m_RequestOpenChat.exchange(false)) {
        OpenChat();
    }

    // Jemand anderes hat den Fokus weggenommen (z. B. Konsole per ^): Chat schliessen.
    if (m_ChatOpen && !p_HasFocus && ImGui::GetTime() - m_ChatOpenedAt > 0.4) {
        m_ChatOpen = false;
    }

    const lw::Settings s_Settings = CurrentSettings();

    if (s_Settings.m_Enabled) {
        if (s_Settings.m_DebugOverlay) {
            DrawDebugOverlay();
        }

        if (s_Settings.m_ShowBubbles) {
            DrawBubbles();
        }

        if (s_Settings.m_ShowSubtitles) {
            DrawSubtitles();
        }

        DrawStatusHud();

        if (m_ChatOpen) {
            DrawChatBox();
        }
    }
    else if (m_ChatOpen) {
        CloseChat();
    }

    if (m_ShowSettings) {
        DrawSettingsWindow();
    }
    else if (m_FocusRequestedBySettings && !m_ChatOpen) {
        SDK()->ReleaseUIFocus();
        m_FocusRequestedBySettings = false;
    }
}

void LivingWorld::OpenChat() {
    if (m_ChatOpen) {
        return;
    }

    m_ChatBuffer[0] = '\0';
    m_ChatOpenedAt = ImGui::GetTime();
    m_ChatOpen = true;

    // Wichtig: Beim letzten Schliessen per Enter/Esc hat ImGui das Loslassen der Taste nicht mehr mitbekommen
    // (der Fokus ging sofort ans Spiel zurueck). Ohne Zuruecksetzen gilt Enter als "noch gedrueckt" und
    // schickt das leere Feld sofort ab -> Chat schliesst sich direkt wieder.
    ImGui::GetIO().ClearEventsQueue();
    ImGui::GetIO().ClearInputKeys();

    SDK()->RequestUIFocus();
    lw::log::Info("Chat geoeffnet (Taste).");
}

void LivingWorld::CloseChat() {
    if (!m_ChatOpen) {
        return;
    }

    m_ChatOpen = false;
    m_ChatBuffer[0] = '\0';

    // Auch beim Schliessen alle Tasten loslassen, damit nichts im SDK-Menue "haengen" bleibt.
    ImGui::GetIO().ClearInputKeys();

    if (!(m_ShowSettings && m_FocusRequestedBySettings)) {
        SDK()->ReleaseUIFocus();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Chatfeld (T)
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DrawChatBox() {
    const lw::Settings s_Settings = CurrentSettings();
    const bool s_De = s_Settings.m_Language == lw::Language::German;
    const ImGuiIO& s_Io = ImGui::GetIO();
    const float s_Scale = s_Settings.m_UiScale;

    // Das 't' vom Oeffnen nicht ins Feld uebernehmen.
    if (ImGui::GetTime() - m_ChatOpenedAt < 0.15) {
        ImGui::GetIO().InputQueueCharacters.resize(0);
        m_ChatBuffer[0] = '\0';
    }

    lw::WorldSnapshot s_World;
    {
        std::scoped_lock s_Lock(m_WorldMutex);
        s_World = m_World;
    }

    const float s_Width = std::min(720.f * s_Scale, s_Io.DisplaySize.x * 0.7f);
    ImGui::SetNextWindowPos({ s_Io.DisplaySize.x * 0.5f, s_Io.DisplaySize.y - 70.f * s_Scale }, ImGuiCond_Always, { 0.5f, 1.f });
    ImGui::SetNextWindowSize({ s_Width, 0.f });
    ImGui::SetNextWindowBgAlpha(0.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 14.f, 12.f });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c_Panel);
    ImGui::PushStyleColor(ImGuiCol_Border, c_Border);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.11f, 0.12f, 0.15f, 1.f });

    constexpr ImGuiWindowFlags s_Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##LivingWorldChat", nullptr, s_Flags)) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(), { ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y },
            Col(c_Panel), 10.f
        );

        // Wer wuerde mich hoeren?
        const float s_Radius = RadiusFor(m_ChatVolume, s_Settings);
        const lw::AddresseeChoice s_Choice = lw::ChooseAddressee(
            s_World, m_ChatBuffer, s_Radius, m_LastAddressee, m_LastAddressTime, s_World.m_GameTime
        );

        ImGui::PushFont(SDK()->GetImGuiBoldFont(), 0.f);
        ImGui::TextColored(c_Accent, ICON_MD_RECORD_VOICE_OVER);
        ImGui::PopFont();
        ImGui::SameLine();

        if (const auto* s_Npc = s_Choice.m_Key ? s_World.FindNpc(s_Choice.m_Key) : nullptr) {
            ImGui::TextColored(c_Text, "%s %s", s_De ? "An:" : "To:", lw::DisplayName(*s_Npc).c_str());
            ImGui::SameLine();
            ImGui::TextColored(c_Muted, "· %.0f m", s_Npc->m_Distance);

            if (s_Choice.m_Hearers.size() > 1) {
                ImGui::SameLine();
                ImGui::TextColored(c_Muted, s_De ? "· %d hören mit" : "· %d overhear", static_cast<int>(s_Choice.m_Hearers.size() - 1));
            }
        }
        else if (!s_Choice.m_Hearers.empty()) {
            ImGui::TextColored(c_Warn, s_De ? "Sieh die Person an, die du meinst" : "Look at the person you mean");
        }
        else {
            ImGui::TextColored(c_Muted, s_De ? "Niemand in Hörweite (%.0f m)" : "Nobody within earshot (%.0f m)", s_Radius);
        }

        // Lautstaerke-Auswahl.
        const float s_ButtonsWidth = 3.f * 92.f * s_Scale;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.f, ImGui::GetContentRegionMax().x - s_ButtonsWidth));

        for (int i = 0; i < 3; ++i) {
            const auto s_Volume = static_cast<lw::VoiceVolume>(i);
            const bool s_Active = m_ChatVolume == s_Volume;

            if (i > 0) {
                ImGui::SameLine(0.f, 4.f);
            }

            ImGui::PushStyleColor(ImGuiCol_Button, s_Active ? ImVec4(c_Accent.x, c_Accent.y, c_Accent.z, 0.85f) : ImVec4(0.16f, 0.17f, 0.21f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Text, s_Active ? ImVec4(0.02f, 0.05f, 0.05f, 1.f) : c_Text);

            if (ImGui::Button(VolumeLabel(s_Volume, s_De), { 88.f * s_Scale, 0.f })) {
                m_ChatVolume = s_Volume;
            }

            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();

        if (ImGui::GetTime() - m_ChatOpenedAt < 0.5) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(-FLT_MIN);

        const auto s_Callback = [](ImGuiInputTextCallbackData* p_Data) -> int {
            if (p_Data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
                auto* s_Volume = static_cast<lw::VoiceVolume*>(p_Data->UserData);
                *s_Volume = static_cast<lw::VoiceVolume>((static_cast<int>(*s_Volume) + 1) % 3);
            }

            return 0;
        };

        const bool s_Submitted = ImGui::InputTextWithHint(
            "##LivingWorldChatInput",
            s_De ? "Was sagst du? (Enter = sagen)" : "What do you say? (Enter = say)",
            m_ChatBuffer, sizeof(m_ChatBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion,
            s_Callback, &m_ChatVolume
        );

        ImGui::PushStyleColor(ImGuiCol_Text, c_Muted);
        ImGui::TextUnformatted(
            s_De ? "Enter: sagen   ·   Esc: abbrechen   ·   Tab: Lautstärke   ·   Name nennen = gezielt ansprechen"
                 : "Enter: say   ·   Esc: cancel   ·   Tab: volume   ·   say a name to address someone"
        );
        ImGui::PopStyleColor();

        const bool s_JustOpened = ImGui::GetTime() - m_ChatOpenedAt < 0.3;

        if (s_JustOpened) {
            // Direkt nach dem Oeffnen keine Enter/Esc-Reste auswerten.
        }
        else if (s_Submitted) {
            const std::string s_Text = lw::Trim(m_ChatBuffer);

            if (!s_Text.empty()) {
                std::scoped_lock s_Lock(m_PendingMutex);
                m_PendingUtterances.push_back({ s_Text, m_ChatVolume, false });
            }

            CloseChat();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CloseChat();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------------------------------------------------
// Untertitel
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DrawSubtitles() {
    const lw::Settings s_Settings = CurrentSettings();
    const ImGuiIO& s_Io = ImGui::GetIO();
    const double s_Now = RealTime();
    const float s_Scale = s_Settings.m_UiScale;

    std::vector<Subtitle> s_Visible;

    {
        std::scoped_lock s_Lock(m_UiMutex);

        while (!m_Subtitles.empty() && s_Now - m_Subtitles.front().m_Created > m_Subtitles.front().m_Duration) {
            m_Subtitles.pop_front();
        }

        for (const auto& s_Subtitle : m_Subtitles) {
            if (s_Now - s_Subtitle.m_Created <= s_Subtitle.m_Duration) {
                s_Visible.push_back(s_Subtitle);
            }
        }
    }

    if (s_Visible.empty()) {
        return;
    }

    while (s_Visible.size() > 4) {
        s_Visible.erase(s_Visible.begin());
    }

    ImDrawList* s_List = ImGui::GetForegroundDrawList();
    ImFont* s_Font = FontOr(SDK()->GetImGuiMediumFont());
    ImFont* s_Bold = FontOr(SDK()->GetImGuiBoldFont());
    const float s_Size = 19.f * s_Scale;
    const float s_MaxWidth = std::min(900.f * s_Scale, s_Io.DisplaySize.x * 0.75f);
    const float s_BottomMargin = (m_ChatOpen ? 190.f : 110.f) * s_Scale;

    float s_Y = s_Io.DisplaySize.y - s_BottomMargin;

    // Von unten nach oben stapeln (neueste unten).
    for (auto s_It = s_Visible.rbegin(); s_It != s_Visible.rend(); ++s_It) {
        const Subtitle& s_Subtitle = *s_It;
        const double s_Age = s_Now - s_Subtitle.m_Created;
        const float s_Alpha = static_cast<float>(std::clamp(std::min(s_Age / 0.25, (s_Subtitle.m_Duration - s_Age) / 0.6), 0.0, 1.0));

        const std::string s_Name = s_Subtitle.m_Speaker + ": ";
        const ImVec2 s_NameSize = s_Bold->CalcTextSizeA(s_Size, FLT_MAX, 0.f, s_Name.c_str());
        const float s_TextWrap = s_MaxWidth - s_NameSize.x;
        const ImVec2 s_TextSize = s_Font->CalcTextSizeA(s_Size, FLT_MAX, s_TextWrap, s_Subtitle.m_Text.c_str());
        const float s_LineWidth = s_NameSize.x + s_TextSize.x;
        const float s_LineHeight = std::max(s_NameSize.y, s_TextSize.y);

        s_Y -= s_LineHeight + 10.f * s_Scale;
        const float s_X = (s_Io.DisplaySize.x - s_LineWidth) * 0.5f;

        s_List->AddRectFilled(
            { s_X - 12.f, s_Y - 5.f }, { s_X + s_LineWidth + 12.f, s_Y + s_LineHeight + 5.f },
            Col(c_PanelSoft, s_Alpha), 7.f
        );

        const ImVec4 s_NameColor = s_Subtitle.m_FromPlayer ? c_PlayerName : EmotionColor(s_Subtitle.m_Emotion);
        ShadowText(s_List, s_Bold, s_Size, { s_X, s_Y }, Col(s_NameColor, s_Alpha), s_Name.c_str(), 0.f, s_Alpha);

        ImVec4 s_TextColor = c_Text;

        if (s_Subtitle.m_FromPlayer && s_Subtitle.m_Emotion == "whisper") {
            s_TextColor = c_Muted;
        }

        ShadowText(s_List, s_Font, s_Size, { s_X + s_NameSize.x, s_Y }, Col(s_TextColor, s_Alpha), s_Subtitle.m_Text.c_str(), s_TextWrap, s_Alpha);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Sprechblasen ueber den Koepfen
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DrawBubbles() {
    const lw::Settings s_Settings = CurrentSettings();
    const double s_Now = RealTime();
    const float s_Scale = s_Settings.m_UiScale;

    struct Bubble {
        uint64_t m_Key;
        std::string m_Text;
        bool m_Thinking;
        std::string m_Emotion;
    };

    std::vector<Bubble> s_Bubbles;

    {
        std::scoped_lock s_Lock(m_UiMutex);

        for (const auto& [s_Key, s_Since] : m_Thinking) {
            s_Bubbles.push_back({ s_Key, "", true, "" });
        }

        for (const auto& s_Subtitle : m_Subtitles) {
            if (s_Subtitle.m_NpcKey != 0 && s_Now - s_Subtitle.m_Created < s_Subtitle.m_Duration) {
                std::erase_if(s_Bubbles, [&](const Bubble& p_Bubble) { return p_Bubble.m_Key == s_Subtitle.m_NpcKey; });
                s_Bubbles.push_back({ s_Subtitle.m_NpcKey, s_Subtitle.m_Text, false, s_Subtitle.m_Emotion });
            }
        }
    }

    if (s_Bubbles.empty()) {
        return;
    }

    std::vector<std::pair<uint64_t, lw::Vec3>> s_Heads;
    std::vector<float> s_Distances;

    {
        std::scoped_lock s_Lock(m_WorldMutex);

        for (const auto& s_Bubble : s_Bubbles) {
            if (const auto* s_Npc = m_World.FindNpc(s_Bubble.m_Key)) {
                s_Heads.push_back({ s_Bubble.m_Key, s_Npc->m_Head });
                s_Distances.push_back(s_Npc->m_Distance);
            }
        }
    }

    ImDrawList* s_List = ImGui::GetForegroundDrawList();
    ImFont* s_Font = FontOr(SDK()->GetImGuiRegularFont());

    for (size_t i = 0; i < s_Heads.size(); ++i) {
        const auto& [s_Key, s_Head] = s_Heads[i];

        if (s_Distances[i] > 30.f) {
            continue;
        }

        SVector2 s_Screen;

        if (!SDK()->WorldToScreen(SVector3(s_Head.x, s_Head.y, s_Head.z + 0.35f), s_Screen)) {
            continue;
        }

        const auto s_It = std::find_if(s_Bubbles.begin(), s_Bubbles.end(), [&](const Bubble& p_Bubble) { return p_Bubble.m_Key == s_Key; });

        if (s_It == s_Bubbles.end()) {
            continue;
        }

        // Weiter weg = kleiner, aber lesbar.
        const float s_DistanceScale = std::clamp(1.25f - s_Distances[i] * 0.03f, 0.7f, 1.15f);
        const float s_Size = 16.f * s_Scale * s_DistanceScale;

        std::string s_Text = s_It->m_Thinking
            ? std::string(static_cast<size_t>(1 + static_cast<int>(s_Now * 3.0) % 3), '.')
            : lw::TruncateUtf8(s_It->m_Text, 120);

        if (!s_It->m_Thinking && s_Text.size() < s_It->m_Text.size()) {
            s_Text += "...";
        }

        const float s_Wrap = 260.f * s_Scale;
        const ImVec2 s_TextSize = s_Font->CalcTextSizeA(s_Size, FLT_MAX, s_Wrap, s_Text.c_str());
        const ImVec2 s_Min { s_Screen.x - s_TextSize.x * 0.5f - 10.f, s_Screen.y - s_TextSize.y - 18.f };
        const ImVec2 s_Max { s_Screen.x + s_TextSize.x * 0.5f + 10.f, s_Screen.y - 8.f };
        const ImVec4 s_Edge = s_It->m_Thinking ? c_Muted : EmotionColor(s_It->m_Emotion);

        s_List->AddRectFilled(s_Min, s_Max, Col(c_Panel, 0.9f), 8.f);
        s_List->AddRect(s_Min, s_Max, Col(s_Edge, 0.85f), 8.f, 0, 1.5f);
        s_List->AddTriangleFilled(
            { s_Screen.x - 7.f, s_Max.y }, { s_Screen.x + 7.f, s_Max.y }, { s_Screen.x, s_Max.y + 8.f }, Col(c_Panel, 0.9f)
        );
        s_List->AddText(s_Font, s_Size, { s_Min.x + 10.f, s_Min.y + 5.f }, Col(c_Text), s_Text.c_str(), nullptr, s_Wrap);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Hinweise oben in der Mitte (Aufnahme, Konfrontation, Status)
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DrawStatusHud() {
    const lw::Settings s_Settings = CurrentSettings();
    const bool s_De = s_Settings.m_Language == lw::Language::German;
    const ImGuiIO& s_Io = ImGui::GetIO();
    const float s_Scale = s_Settings.m_UiScale;
    ImDrawList* s_List = ImGui::GetForegroundDrawList();
    ImFont* s_Font = FontOr(SDK()->GetImGuiMediumFont());
    const float s_Size = 17.f * s_Scale;

    std::vector<std::pair<std::string, ImVec4>> s_Lines;

    if (m_Recording) {
        char s_Buffer[96];
        snprintf(s_Buffer, sizeof(s_Buffer), ICON_MD_MIC " %s  %.1f s", s_De ? "Aufnahme" : "Recording", m_Mic.Seconds());
        s_Lines.push_back({ s_Buffer, c_Danger });
    }
    else if (m_Stt.IsBusy()) {
        s_Lines.push_back({ std::string(ICON_MD_HEARING " ") + (s_De ? "Verstehe ..." : "Listening ..."), c_Accent });
    }

    // Konfrontationen.
    {
        lw::WorldSnapshot s_World;
        {
            std::scoped_lock s_Lock(m_WorldMutex);
            s_World = m_World;
        }

        const double s_GameNow = s_World.m_GameTime;

        for (const uint64_t s_Key : m_Gate.ActiveStandoffs(s_GameNow)) {
            if (const auto* s_Npc = s_World.FindNpc(s_Key)) {
                char s_Buffer[160];
                snprintf(
                    s_Buffer, sizeof(s_Buffer), ICON_MD_WARNING " %s %s  ·  %.0f s",
                    lw::DisplayName(*s_Npc).c_str(), s_De ? "zielt auf dich" : "is aiming at you",
                    m_Gate.SecondsLeft(s_Key, s_GameNow)
                );
                s_Lines.push_back({ s_Buffer, c_Warn });
            }

            if (s_Lines.size() > 4) {
                break;
            }
        }
    }

    {
        std::scoped_lock s_Lock(m_UiMutex);

        if (!m_StatusText.empty() && RealTime() < m_StatusUntil) {
            s_Lines.push_back({ m_StatusText, c_Text });
        }
    }

    float s_Y = 70.f * s_Scale;

    for (const auto& [s_Text, s_Color] : s_Lines) {
        const ImVec2 s_TextSize = s_Font->CalcTextSizeA(s_Size, FLT_MAX, 700.f * s_Scale, s_Text.c_str());
        const float s_X = (s_Io.DisplaySize.x - s_TextSize.x) * 0.5f;
        s_List->AddRectFilled({ s_X - 14.f, s_Y - 6.f }, { s_X + s_TextSize.x + 14.f, s_Y + s_TextSize.y + 6.f }, Col(c_Panel, 0.85f), 14.f);
        s_List->AddRect({ s_X - 14.f, s_Y - 6.f }, { s_X + s_TextSize.x + 14.f, s_Y + s_TextSize.y + 6.f }, Col(s_Color, 0.6f), 14.f);
        ShadowText(s_List, s_Font, s_Size, { s_X, s_Y }, Col(s_Color), s_Text.c_str(), 700.f * s_Scale);

        // Pegelanzeige unter der Aufnahme.
        if (m_Recording && s_Color.x == c_Danger.x) {
            const float s_Level = m_Mic.Level();
            const float s_BarWidth = s_TextSize.x + 28.f;
            s_List->AddRectFilled({ s_X - 14.f, s_Y + s_TextSize.y + 9.f }, { s_X - 14.f + s_BarWidth, s_Y + s_TextSize.y + 12.f }, Col(c_Border));
            s_List->AddRectFilled({ s_X - 14.f, s_Y + s_TextSize.y + 9.f }, { s_X - 14.f + s_BarWidth * s_Level, s_Y + s_TextSize.y + 12.f }, Col(c_Danger));
        }

        s_Y += s_TextSize.y + 20.f * s_Scale;
    }

    // Dezenter Hinweis, wenn jemand ansprechbar ist.
    if (s_Settings.m_ShowHints && s_Lines.empty() && !m_ChatOpen && !m_UiHasFocus) {
        std::string s_Name;

        {
            std::scoped_lock s_Lock(m_WorldMutex);

            for (const auto& s_Npc : m_World.m_Npcs) {
                if (s_Npc.m_Distance > 3.f) {
                    break;
                }

                if (s_Npc.CanTalk() && s_Npc.m_ViewAngle < 35.f) {
                    s_Name = lw::DisplayName(s_Npc);
                    break;
                }
            }
        }

        if (!s_Name.empty()) {
            const std::string s_Hint = "[" + lw::KeyName(s_Settings.m_ChatKey) + "] " + (s_De ? "Ansprechen: " : "Talk to: ") + s_Name +
                (s_Settings.m_SttEnabled ? "   [" + lw::KeyName(s_Settings.m_PushToTalkKey) + "] " + (s_De ? "Sprechen" : "Speak") : "");
            const ImVec2 s_TextSize = s_Font->CalcTextSizeA(15.f * s_Scale, FLT_MAX, 0.f, s_Hint.c_str());
            const ImVec2 s_Pos { (s_Io.DisplaySize.x - s_TextSize.x) * 0.5f, s_Io.DisplaySize.y * 0.62f };
            s_List->AddRectFilled({ s_Pos.x - 10.f, s_Pos.y - 4.f }, { s_Pos.x + s_TextSize.x + 10.f, s_Pos.y + s_TextSize.y + 4.f }, Col(c_Panel, 0.55f), 10.f);
            ShadowText(s_List, s_Font, 15.f * s_Scale, s_Pos, Col(c_Text, 0.85f), s_Hint.c_str());
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Diagnose-Overlay
// ---------------------------------------------------------------------------------------------------------------------

void LivingWorld::DrawDebugOverlay() {
    lw::WorldSnapshot s_World;

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        s_World = m_World;
    }

    ImDrawList* s_List = ImGui::GetForegroundDrawList();
    ImFont* s_Font = FontOr(SDK()->GetImGuiRegularFont());

    for (const auto& s_Npc : s_World.m_Npcs) {
        if (s_Npc.m_Distance > 15.f) {
            break;
        }

        SVector2 s_Screen;

        if (!SDK()->WorldToScreen(SVector3(s_Npc.m_Head.x, s_Npc.m_Head.y, s_Npc.m_Head.z + 0.1f), s_Screen)) {
            continue;
        }

        const lw::GateState s_State = m_Gate.State(s_Npc.m_Key, s_World.m_GameTime);
        const char* s_StateName = s_State == lw::GateState::Warning ? "WARNUNG" :
            s_State == lw::GateState::Firing ? "FEUER" :
            s_State == lw::GateState::Persuaded ? "UEBERREDET" : "";

        char s_Buffer[384];
        snprintf(
            s_Buffer, sizeof(s_Buffer), "%s\n%s | %s%s%s\n%.1f m | %s %s",
            lw::DisplayName(s_Npc).c_str(), s_Npc.m_Outfit.c_str(), s_Npc.m_Guard ? "Wache" : "Zivil",
            s_Npc.m_Enforcer ? " | Enforcer" : "", s_Npc.m_Target ? " | ZIEL" : "", s_Npc.m_Distance,
            lw::BehaviorName(s_Npc.m_Behavior).c_str(), s_StateName
        );

        const ImVec2 s_Size = s_Font->CalcTextSizeA(13.f, FLT_MAX, 0.f, s_Buffer);
        const ImVec2 s_Pos { s_Screen.x - s_Size.x * 0.5f, s_Screen.y - s_Size.y };
        s_List->AddRectFilled({ s_Pos.x - 4.f, s_Pos.y - 2.f }, { s_Pos.x + s_Size.x + 4.f, s_Pos.y + s_Size.y + 2.f }, IM_COL32(0, 0, 0, 150), 4.f);
        s_List->AddText(s_Font, 13.f, s_Pos, s_State == lw::GateState::Idle ? IM_COL32(220, 230, 255, 255) : IM_COL32(255, 190, 80, 255), s_Buffer);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstellungsfenster
// ---------------------------------------------------------------------------------------------------------------------

bool LivingWorld::KeyButton(const char* p_Label, int& p_Key) {
    bool s_Changed = false;
    ImGui::PushID(p_Label);

    if (m_CapturingKey == &p_Key) {
        ImGui::Button("... Taste drücken (Esc = abbrechen)", { 260.f, 0.f });

        // Erst wenn die Maustaste vom Klick losgelassen ist, die naechste Taste nehmen.
        static double s_CaptureStart = 0.0;

        if (s_CaptureStart == 0.0) {
            s_CaptureStart = ImGui::GetTime();
        }

        if (ImGui::GetTime() - s_CaptureStart > 0.3) {
            for (int s_Vk = 1; s_Vk < 255; ++s_Vk) {
                if (s_Vk == VK_LBUTTON && ImGui::IsItemHovered()) {
                    continue;
                }

                if (GetAsyncKeyState(s_Vk) & 0x8000) {
                    if (s_Vk == VK_ESCAPE) {
                        m_CapturingKey = nullptr;
                    }
                    else if (s_Vk != VK_SHIFT && s_Vk != VK_CONTROL && s_Vk != VK_MENU) {
                        p_Key = s_Vk;
                        s_Changed = true;
                        m_CapturingKey = nullptr;
                    }

                    s_CaptureStart = 0.0;
                    break;
                }
            }
        }
    }
    else if (ImGui::Button((lw::KeyName(p_Key) + "##key").c_str(), { 260.f, 0.f })) {
        m_CapturingKey = &p_Key;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(p_Label);
    ImGui::PopID();
    return s_Changed;
}

void LivingWorld::DrawSettingsWindow() {
    static lw::Settings s_Edit;
    static bool s_Loaded = false;
    static bool s_Dirty = false;

    if (!s_Loaded) {
        s_Edit = CurrentSettings();
        s_Loaded = true;
    }

    ImGui::SetNextWindowSize({ 820.f, 640.f }, ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, { 0.10f, 0.32f, 0.30f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_TabSelected, { 0.12f, 0.45f, 0.42f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_TabHovered, { 0.16f, 0.58f, 0.54f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_CheckMark, c_Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, c_Accent);

    bool s_Open = true;

    if (ImGui::Begin(ICON_MD_RECORD_VOICE_OVER " Living World – KI-NPCs & Realismus###LivingWorldSettings", &s_Open)) {
        bool s_Changed = false;

        if (ImGui::BeginTabBar("##lwtabs")) {
            if (ImGui::BeginTabItem(ICON_MD_SETTINGS " Allgemein")) { DrawTabGeneral(s_Edit, s_Changed); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_SMART_TOY " KI")) { DrawTabAi(s_Edit, s_Changed); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_VOLUME_UP " Stimmen")) { DrawTabVoices(s_Edit, s_Changed); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_MIC " Spracheingabe")) { DrawTabMicrophone(s_Edit, s_Changed); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_SHIELD " Verhalten")) { DrawTabBehavior(s_Edit, s_Changed); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_PERSON " NPCs")) { DrawTabNpcs(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_FORUM " Gespräche")) { DrawTabConversations(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_MD_BUG_REPORT " Diagnose")) { DrawTabDiagnostics(s_Edit, s_Changed); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        s_Dirty |= s_Changed;

        // Erst speichern, wenn kein Regler mehr gezogen wird (das SDK schreibt bei jeder Aenderung die INI).
        if (s_Dirty && !ImGui::IsAnyItemActive()) {
            CommitSettings(s_Edit);
            s_Dirty = false;
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(5);

    if (!s_Open) {
        if (s_Dirty) {
            CommitSettings(s_Edit);
            s_Dirty = false;
        }

        m_ShowSettings = false;
        m_CapturingKey = nullptr;
        s_Loaded = false; // beim naechsten Oeffnen frisch laden
    }
}

void LivingWorld::DrawTabGeneral(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    p_Changed |= ImGui::Checkbox("Living World aktiv", &s.m_Enabled);
    Help("Schaltet alles aus: kein Chat, keine KI, keine Deeskalation. Das Spiel verhält sich dann wie ohne Mod.");

    int s_Language = static_cast<int>(s.m_Language);
    const char* s_Languages[] = { "Deutsch", "English" };

    if (ImGui::Combo("Sprache der NPCs", &s_Language, s_Languages, 2)) {
        s.m_Language = static_cast<lw::Language>(s_Language);
        p_Changed = true;
    }

    ImGui::SeparatorText("Tasten");
    p_Changed |= KeyButton("Chat öffnen (tippen)", s.m_ChatKey);
    p_Changed |= KeyButton("Push-to-Talk (gedrückt halten und sprechen)", s.m_PushToTalkKey);
    p_Changed |= KeyButton("Dieses Fenster öffnen/schließen", s.m_MenuKey);
    ImGui::TextDisabled("Tipp: Seitentasten der Maus (Maus 4/5) eignen sich gut für Push-to-Talk.");

    ImGui::SeparatorText("Anzeige");
    p_Changed |= ImGui::Checkbox("Untertitel", &s.m_ShowSubtitles);
    ImGui::SameLine(220.f);
    p_Changed |= ImGui::Checkbox("Sprechblasen über Köpfen", &s.m_ShowBubbles);
    p_Changed |= ImGui::Checkbox("Hinweise (\"[T] Ansprechen\", Warnungen)", &s.m_ShowHints);
    p_Changed |= ImGui::SliderFloat("Untertitel-Dauer (s)", &s.m_SubtitleSeconds, 3.f, 20.f, "%.0f");
    p_Changed |= ImGui::SliderFloat("Größe der Anzeige", &s.m_UiScale, 0.7f, 1.8f, "%.2f");

    ImGui::SeparatorText("Chat");
    p_Changed |= ImGui::Checkbox("Zeitlupe, solange du tippst", &s.m_SlowTimeWhileTyping);
    Help("Tippen dauert länger als Sprechen. Damit dich niemand umrennt, während du schreibst, läuft die Zeit langsamer.");

    if (s.m_SlowTimeWhileTyping) {
        p_Changed |= ImGui::SliderFloat("Spieltempo beim Tippen", &s.m_TypingTimeScale, 0.1f, 1.f, "%.2fx");
    }
}

void LivingWorld::DrawTabAi(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "NPCs antworten mit einem lokalen Sprachmodell. Standard ist Ollama (läuft auf deinem PC, kostenlos, offline). "
        "Alternativ jeder OpenAI-kompatible lokale Server (LM Studio, llama.cpp, KoboldCpp ...)."
    );

    int s_Provider = static_cast<int>(s.m_LlmProvider);
    const char* s_Providers[] = { "Ollama", "OpenAI-kompatibel (LM Studio, llama.cpp ...)" };

    if (ImGui::Combo("Server-Typ", &s_Provider, s_Providers, 2)) {
        s.m_LlmProvider = static_cast<lw::LlmProvider>(s_Provider);

        if (s.m_LlmProvider == lw::LlmProvider::OpenAiCompatible && s.m_LlmUrl.find("11434") != std::string::npos) {
            s.m_LlmUrl = "http://127.0.0.1:1234/v1";
        }
        else if (s.m_LlmProvider == lw::LlmProvider::Ollama && s.m_LlmUrl.find("1234") != std::string::npos) {
            s.m_LlmUrl = "http://127.0.0.1:11434";
        }

        p_Changed = true;
    }

    p_Changed |= InputString("Adresse", s.m_LlmUrl);
    p_Changed |= InputString("Modell", s.m_LlmModel);

    {
        std::scoped_lock s_Lock(m_KnownModelsMutex);

        if (!m_KnownModels.empty()) {
            ImGui::SameLine();

            if (ImGui::BeginCombo("##models", "", ImGuiComboFlags_NoPreview)) {
                for (const auto& s_Model : m_KnownModels) {
                    if (ImGui::Selectable(s_Model.c_str(), s_Model == s.m_LlmModel)) {
                        s.m_LlmModel = s_Model;
                        p_Changed = true;
                    }
                }

                ImGui::EndCombo();
            }
        }
    }

    if (s.m_LlmProvider == lw::LlmProvider::OpenAiCompatible) {
        p_Changed |= InputString("API-Schlüssel (meist leer)", s.m_LlmApiKey, ImGuiInputTextFlags_Password);
    }
    else {
        p_Changed |= ImGui::Checkbox("Ollama bei Bedarf automatisch starten", &s.m_AutoStartOllama);
    }

    if (ImGui::Button(ICON_MD_CHECK_CIRCLE " Verbindung testen & Modelle laden")) {
        CommitSettings(s);
        RunTask(m_ConnectionTask, [this](BackgroundTask& p_Task) {
            std::string s_Error;
            std::string s_Version;
            p_Task.SetResult("verbinde ...");

            if (!m_Llm.EnsureServer(s_Error) || !m_Llm.QueryVersion(s_Version, s_Error)) {
                p_Task.SetResult("Fehler: " + s_Error);
                return;
            }

            std::vector<std::string> s_Models;

            if (m_Llm.QueryModels(s_Models, s_Error)) {
                std::scoped_lock s_Lock(m_KnownModelsMutex);
                m_KnownModels = s_Models;
            }

            const auto s_Config = m_Llm.GetConfig();
            const bool s_HasModel = std::find(s_Models.begin(), s_Models.end(), s_Config.m_Model) != s_Models.end() ||
                std::find(s_Models.begin(), s_Models.end(), s_Config.m_Model + ":latest") != s_Models.end();

            p_Task.SetResult(
                "Verbunden (Version " + s_Version + "), " + std::to_string(s_Models.size()) + " Modell(e). " +
                (s_HasModel ? "Modell \"" + s_Config.m_Model + "\" ist da." : "ACHTUNG: Modell \"" + s_Config.m_Model + "\" fehlt – unten herunterladen.")
            );
        });
    }

    if (m_ConnectionTask.m_Running) {
        ImGui::SameLine();
        ImGui::TextColored(c_Muted, "läuft ...");
    }

    const std::string s_ConnectionResult = m_ConnectionTask.Result();

    if (!s_ConnectionResult.empty()) {
        ImGui::TextColored(s_ConnectionResult.rfind("Fehler", 0) == 0 ? c_Danger : c_Good, "%s", s_ConnectionResult.c_str());
    }

    ImGui::TextColored(c_Muted, "Status: %s", m_Llm.LastStatus().empty() ? "noch keine Anfrage" : m_Llm.LastStatus().c_str());

    ImGui::SeparatorText("Modell herunterladen (Ollama)");
    ImGui::TextWrapped(
        "Neben HITMAN bleibt wenig Grafikspeicher übrig, dann rechnet die KI auf dem Prozessor und wird langsam. "
        "Standard ist qwen3:1.7b (klein, bleibt gut in der Rolle). qwen2.5:3b spricht saubereres Deutsch, ist aber langsamer. "
        "Tipp: In HITMAN die Texturqualität senken – dann passt das Modell auf die Grafikkarte und antwortet in ca. 1 s."
    );

    struct ModelChoice {
        const char* m_Name;
        const char* m_Label;
    };

    constexpr ModelChoice s_Choices[] = {
        { "qwen3:1.7b", "Schnell: qwen3:1.7b (1,4 GB)" },
        { "qwen2.5:3b", "Besseres Deutsch: qwen2.5:3b (1,9 GB)" },
        { "qwen2.5:7b", "Klug, sehr langsam: qwen2.5:7b (4,7 GB)" },
    };

    for (size_t i = 0; i < std::size(s_Choices); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }

        if (ImGui::SmallButton(s_Choices[i].m_Label)) {
            strncpy_s(m_PullModelName, s_Choices[i].m_Name, _TRUNCATE);
            s.m_LlmModel = s_Choices[i].m_Name;
            p_Changed = true;
        }
    }

    ImGui::TextDisabled("Ein Klick wählt das Modell aus. Fehlt es noch, unten auf \"Herunterladen\" klicken.");
    ImGui::SetNextItemWidth(260.f);
    ImGui::InputText("##pull", m_PullModelName, sizeof(m_PullModelName));
    ImGui::SameLine();

    if (!m_ModelPullTask.m_Running) {
        if (ImGui::Button(ICON_MD_DOWNLOAD " Herunterladen")) {
            CommitSettings(s);
            const std::string s_Model = m_PullModelName;
            RunTask(m_ModelPullTask, [this, s_Model](BackgroundTask& p_Task) {
                std::string s_Error;
                p_Task.SetResult("starte Download ...");
                const bool s_Ok = m_Llm.PullModel(s_Model, [&](const lw::PullProgress& p_Progress) {
                    p_Task.m_Done = p_Progress.m_Completed;
                    p_Task.m_Total = p_Progress.m_Total;
                    p_Task.SetResult(p_Progress.m_Status);
                }, &p_Task.m_Cancel, s_Error);

                p_Task.SetResult(s_Ok ? "Fertig: " + s_Model + " ist installiert." : "Fehler: " + s_Error);

                std::vector<std::string> s_Models;
                std::string s_Ignored;

                if (s_Ok && m_Llm.QueryModels(s_Models, s_Ignored)) {
                    std::scoped_lock s_Lock(m_KnownModelsMutex);
                    m_KnownModels = s_Models;
                }
            });
        }
    }
    else if (ImGui::Button("Abbrechen##pull")) {
        m_ModelPullTask.m_Cancel.Cancel();
    }

    if (m_ModelPullTask.m_Running && m_ModelPullTask.m_Total > 0) {
        const float s_Fraction = static_cast<float>(m_ModelPullTask.m_Done) / static_cast<float>(m_ModelPullTask.m_Total);
        ImGui::ProgressBar(s_Fraction, { -FLT_MIN, 0.f }, (FormatMb(m_ModelPullTask.m_Done) + " / " + FormatMb(m_ModelPullTask.m_Total)).c_str());
    }

    if (const std::string s_Result = m_ModelPullTask.Result(); !s_Result.empty()) {
        ImGui::TextColored(c_Muted, "%s", s_Result.c_str());
    }

    ImGui::SeparatorText("Feintuning");
    p_Changed |= ImGui::Checkbox("Schnelle Antworten (sprechen, sobald der Satz fertig ist)", &s.m_FastReplies);
    Help("Die KI schreibt erst das Gefühl, dann den Satz, dann Zahlen für Vertrauen/Angst. Mit dieser Option spricht der NPC "
         "schon, sobald der Satz fertig ist. Tipp: In HITMAN die Bildrate begrenzen (z. B. 60 FPS) – dann hat die KI mehr "
         "Grafikkarte übrig und antwortet deutlich schneller.");
    p_Changed |= ImGui::SliderFloat("Kreativität (Temperatur)", &s.m_Temperature, 0.f, 1.5f, "%.2f");
    p_Changed |= ImGui::SliderInt("Max. Antwortlänge (Tokens)", &s.m_MaxTokens, 60, 400);
    p_Changed |= ImGui::SliderInt("Gedächtnis (Wortwechsel pro NPC)", &s.m_HistoryTurns, 0, 20);
    p_Changed |= ImGui::SliderInt("Kontextgröße", &s.m_ContextSize, 2048, 16384);

    int s_GpuMode = s.m_GpuLayers < 0 ? 0 : 1;
    const char* s_GpuModes[] = { "Automatisch (Grafikkarte wenn möglich)", "Nur Prozessor (schont die Grafikkarte)" };

    if (ImGui::Combo("Rechnen auf", &s_GpuMode, s_GpuModes, 2)) {
        s.m_GpuLayers = s_GpuMode == 0 ? -1 : 0;
        p_Changed = true;
    }

    Help("HITMAN belegt meist fast den ganzen Grafikspeicher. Dann rechnet die KI ohnehin größtenteils auf dem Prozessor.");
    p_Changed |= ImGui::SliderInt("KI-Prozessorkerne (0 = automatisch)", &s.m_LlmThreads, 0, 16);
    Help("Weniger Kerne = HITMAN ruckelt weniger, Antworten dauern länger. Bei Rucklern z. B. 4 probieren.");
    p_Changed |= ImGui::SliderInt("Modell im Speicher halten (Minuten)", &s.m_KeepAliveMinutes, 0, 120);
    p_Changed |= ImGui::SliderInt("Zeitlimit pro Antwort (s)", &s.m_TimeoutSeconds, 10, 180);

    ImGui::SeparatorText("Testgespräch");
    ImGui::SetNextItemWidth(-140.f);
    ImGui::InputText("##testsentence", m_TestSentence, sizeof(m_TestSentence));
    ImGui::SameLine();

    if (ImGui::Button("An Wache senden") && !m_TestTask.m_Running) {
        CommitSettings(s);
        const std::string s_Sentence = m_TestSentence;
        const lw::Settings s_Copy = s;
        RunTask(m_TestTask, [this, s_Sentence, s_Copy](BackgroundTask& p_Task) {
            p_Task.SetResult("Wache denkt nach ...");

            lw::WorldSnapshot s_World;
            s_World.m_LocationName = lw::FriendlyLocation("LOCATION_PARIS", "", s_Copy.m_Language);
            s_World.m_Player.m_Outfit = "Waiter";
            s_World.m_Player.m_InSuit = false;

            lw::NpcSnapshot s_Guard;
            s_Guard.m_Key = 42;
            s_Guard.m_Outfit = "Security Guard";
            s_Guard.m_Guard = true;
            s_Guard.m_Behavior = 58;
            s_Guard.m_Distance = 2.f;
            s_World.m_Npcs.push_back(s_Guard);

            lw::DialogueBook s_Book;
            lw::PromptContext s_Context;
            s_Context.m_Settings = &s_Copy;
            s_Context.m_World = &s_World;
            s_Context.m_Npc = &s_World.m_Npcs[0];
            s_Context.m_PlayerText = s_Sentence;

            lw::LlmJob s_Job;
            s_Job.m_Messages = s_Book.BuildMessages(s_Context);
            s_Job.m_Schema = lw::ReplySchema();
            const lw::LlmResult s_Result = m_Llm.RunNow(s_Job, &p_Task.m_Cancel);

            if (!s_Result.m_Ok) {
                p_Task.SetResult("Fehler: " + s_Result.m_Error);
                return;
            }

            const lw::NpcReply s_Reply = lw::ParseReply(s_Result.m_Content);
            p_Task.SetResult("Wache (" + lw::FormatSeconds(s_Result.m_Seconds) + "): \"" + s_Reply.m_Say + "\" [" + s_Reply.m_Emotion + "]");

            if (s_Copy.m_TtsEnabled && !s_Reply.m_Say.empty()) {
                lw::SpeakRequest s_Request;
                s_Request.m_Text = s_Reply.m_Say;
                s_Request.m_VoiceName = s_Copy.m_VoiceMale;
                s_Request.m_German = s_Copy.m_Language == lw::Language::German;
                s_Request.m_Tag = 2;
                s_Request.m_Volume = s_Copy.m_TtsVolume;
                s_Request.m_Style = { s_Copy.m_TtsBaseRate, -2, 100 };
                m_Speech.Speak(std::move(s_Request));
            }
        });
    }

    if (const std::string s_Result = m_TestTask.Result(); !s_Result.empty()) {
        ImGui::TextWrapped("%s", s_Result.c_str());
    }
}

void LivingWorld::DrawTabVoices(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    p_Changed |= ImGui::Checkbox("NPCs sprechen ihre Antworten laut aus", &s.m_TtsEnabled);
    p_Changed |= ImGui::Checkbox("Originale HITMAN-Sätze der NPCs stummschalten (experimentell)", &s.m_MuteVanillaVoices);
    Help("NPCs reden dann nur noch über die KI. Wirkt das bei einigen NPCs nicht: in HITMAN unter Optionen > Audio "
         "die Dialog-Lautstärke auf 0 stellen (betrifft dann auch 47) und die Untertitel ausschalten.");
    p_Changed |= ImGui::SliderFloat("Lautstärke", &s.m_TtsVolume, 0.f, 2.f, "%.2f");
    p_Changed |= ImGui::SliderInt("Sprechtempo", &s.m_TtsBaseRate, -5, 5);
    p_Changed |= ImGui::Checkbox("Räumlicher Klang (leiser mit Abstand, links/rechts)", &s.m_SpatialAudio);

    if (s.m_SpatialAudio) {
        p_Changed |= ImGui::SliderFloat("Hörweite der Stimmen (m)", &s.m_AudioMaxDistance, 8.f, 60.f, "%.0f");
    }

    const std::vector<lw::VoiceInfo> s_Voices = m_Speech.Voices();

    const auto s_VoiceCombo = [&](const char* p_Label, std::string& p_Value) {
        const char* s_Preview = p_Value.empty() ? "Automatisch" : p_Value.c_str();

        if (ImGui::BeginCombo(p_Label, s_Preview)) {
            if (ImGui::Selectable("Automatisch", p_Value.empty())) {
                p_Value.clear();
                p_Changed = true;
            }

            for (const auto& s_Voice : s_Voices) {
                const std::string s_Label = s_Voice.m_Name + (s_Voice.IsGerman() ? "  (Deutsch" : "  (Englisch") +
                    (s_Voice.m_Female ? ", weiblich)" : ", männlich)");

                if (ImGui::Selectable(s_Label.c_str(), p_Value == s_Voice.m_Name)) {
                    p_Value = s_Voice.m_Name;
                    p_Changed = true;
                }
            }

            ImGui::EndCombo();
        }
    };

    ImGui::SeparatorText("Stimmen");
    s_VoiceCombo("Männliche NPCs", s.m_VoiceMale);
    s_VoiceCombo("Weibliche NPCs", s.m_VoiceFemale);
    ImGui::TextDisabled("Jeder NPC bekommt automatisch eine eigene Tonhöhe und ein eigenes Tempo.");
    p_Changed |= ImGui::Checkbox("47 spricht getippte Sätze laut aus", &s.m_Tts47);

    if (s.m_Tts47) {
        s_VoiceCombo("Stimme von 47", s.m_Voice47);
    }

    if (ImGui::Button(ICON_MD_VOLUME_UP " Probe: Mann")) {
        CommitSettings(s);
        lw::SpeakRequest s_Request;
        s_Request.m_Text = s.m_Language == lw::Language::German ? "Bleiben Sie stehen! Was haben Sie hier zu suchen?" : "Stop right there! What are you doing here?";
        s_Request.m_VoiceName = s.m_VoiceMale;
        s_Request.m_German = s.m_Language == lw::Language::German;
        s_Request.m_Tag = 2;
        s_Request.m_Volume = s.m_TtsVolume;
        s_Request.m_Style = { s.m_TtsBaseRate, -2, 100 };
        m_Speech.Speak(std::move(s_Request));
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_VOLUME_UP " Probe: Frau")) {
        CommitSettings(s);
        lw::SpeakRequest s_Request;
        s_Request.m_Text = s.m_Language == lw::Language::German ? "Entschuldigung, kann ich Ihnen helfen?" : "Excuse me, can I help you?";
        s_Request.m_VoiceName = s.m_VoiceFemale;
        s_Request.m_Female = true;
        s_Request.m_German = s.m_Language == lw::Language::German;
        s_Request.m_Tag = 2;
        s_Request.m_Volume = s.m_TtsVolume;
        s_Request.m_Style = { s.m_TtsBaseRate, 1, 100 };
        m_Speech.Speak(std::move(s_Request));
    }

    if (!m_Speech.IsReady()) {
        const std::string s_Error = m_Speech.LastError();
        ImGui::TextColored(c_Danger, "Sprachausgabe nicht bereit%s%s", s_Error.empty() ? "" : ": ", s_Error.c_str());
    }

    ImGui::TextDisabled(
        "Mehr Stimmen: Windows-Einstellungen > Zeit und Sprache > Sprache > Deutsch > Sprachausgabe. "
        "Installierte Stimmen: %d", static_cast<int>(s_Voices.size())
    );
}

void LivingWorld::DrawTabMicrophone(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    p_Changed |= ImGui::Checkbox("Spracheingabe per Push-to-Talk", &s.m_SttEnabled);
    ImGui::TextWrapped(
        "Taste [%s] gedrückt halten, sprechen, loslassen. Die Erkennung (whisper.cpp) läuft komplett lokal auf deinem Prozessor.",
        lw::KeyName(s.m_PushToTalkKey).c_str()
    );

    const std::vector<std::string> s_Devices = lw::MicRecorder::ListDevices();
    const std::string s_Current = s.m_MicDevice < 0 || s.m_MicDevice >= static_cast<int>(s_Devices.size())
        ? "Windows-Standard"
        : s_Devices[static_cast<size_t>(s.m_MicDevice)];

    if (ImGui::BeginCombo("Mikrofon", s_Current.c_str())) {
        if (ImGui::Selectable("Windows-Standard", s.m_MicDevice < 0)) {
            s.m_MicDevice = -1;
            p_Changed = true;
        }

        for (size_t i = 0; i < s_Devices.size(); ++i) {
            if (ImGui::Selectable((s_Devices[i] + "##mic" + std::to_string(i)).c_str(), s.m_MicDevice == static_cast<int>(i))) {
                s.m_MicDevice = static_cast<int>(i);
                p_Changed = true;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Erkennungsmodell");

    for (const auto& s_Model : lw::KnownWhisperModels()) {
        ImGui::PushID(s_Model.m_File);
        const std::wstring s_Path = ModsDirectory() + L"\\LivingWorld\\models\\" + lw::Utf8ToWide(s_Model.m_File);
        const bool s_Exists = GetFileAttributesW(s_Path.c_str()) != INVALID_FILE_ATTRIBUTES;

        if (ImGui::RadioButton(s_Model.m_Label, s.m_WhisperModel == s_Model.m_File)) {
            s.m_WhisperModel = s_Model.m_File;
            p_Changed = true;
        }

        ImGui::SameLine(430.f);

        if (s_Exists) {
            ImGui::TextColored(c_Good, ICON_MD_CHECK_CIRCLE " vorhanden");
        }
        else if (!m_WhisperDownloadTask.m_Running) {
            if (ImGui::SmallButton((std::string(ICON_MD_DOWNLOAD " laden (") + std::to_string(s_Model.m_SizeMb) + " MB)").c_str())) {
                const std::string s_File = s_Model.m_File;
                RunTask(m_WhisperDownloadTask, [this, s_File, s_Path](BackgroundTask& p_Task) {
                    std::string s_Error;
                    p_Task.SetResult("lade " + s_File + " ...");
                    const bool s_Ok = lw::http::DownloadToFile(
                        lw::WhisperModelUrl(s_File), s_Path,
                        [&](uint64_t p_Done, uint64_t p_Total) {
                            p_Task.m_Done = p_Done;
                            p_Task.m_Total = p_Total;
                        },
                        &p_Task.m_Cancel, s_Error
                    );
                    p_Task.SetResult(s_Ok ? "Fertig: " + s_File : "Fehler: " + s_Error);
                });
            }
        }
        else {
            ImGui::TextColored(c_Muted, "...");
        }

        ImGui::PopID();
    }

    if (m_WhisperDownloadTask.m_Running && m_WhisperDownloadTask.m_Total > 0) {
        ImGui::ProgressBar(
            static_cast<float>(m_WhisperDownloadTask.m_Done) / static_cast<float>(m_WhisperDownloadTask.m_Total), { -FLT_MIN, 0.f },
            (FormatMb(m_WhisperDownloadTask.m_Done) + " / " + FormatMb(m_WhisperDownloadTask.m_Total)).c_str()
        );

        if (ImGui::SmallButton("Download abbrechen")) {
            m_WhisperDownloadTask.m_Cancel.Cancel();
        }
    }

    if (const std::string s_Result = m_WhisperDownloadTask.Result(); !s_Result.empty()) {
        ImGui::TextColored(c_Muted, "%s", s_Result.c_str());
    }

    ImGui::TextColored(c_Muted, "Erkennung: %s", m_Stt.Status().empty() ? "noch nicht benutzt" : m_Stt.Status().c_str());
    p_Changed |= ImGui::SliderInt("Prozessor-Threads", &s.m_SttThreads, 1, 12);

    ImGui::SeparatorText("Lautstärke deiner Stimme");
    p_Changed |= ImGui::Checkbox("Flüstern/Rufen an der Mikrofon-Lautstärke erkennen", &s.m_MicSetsVolume);
    Help("Leise gesprochen = nur ganz nahe NPCs hören dich. Laut gerufen = alle im weiten Umkreis.");

    if (s.m_MicSetsVolume) {
        p_Changed |= ImGui::SliderFloat("Flüstern unter (dB)", &s.m_WhisperThresholdDb, -60.f, -20.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Rufen über (dB)", &s.m_ShoutThresholdDb, -30.f, -3.f, "%.0f");
    }

    if (!m_TestTask.m_Running) {
        if (ImGui::Button(ICON_MD_MIC " Mikrofon testen (4 s sprechen)")) {
            CommitSettings(s);
            const lw::Settings s_Copy = s;
            const std::wstring s_ModelPath = WhisperModelPath(s);
            RunTask(m_TestTask, [this, s_Copy, s_ModelPath](BackgroundTask& p_Task) {
                lw::MicRecorder s_Mic;
                std::string s_Error;

                if (!s_Mic.Start(s_Copy.m_MicDevice, s_Error)) {
                    p_Task.SetResult("Fehler: " + s_Error);
                    return;
                }

                m_MicTestRunning = true;

                for (int i = 0; i < 40 && !p_Task.m_Cancel.IsCancelled(); ++i) {
                    p_Task.SetResult("Sprich jetzt ... " + std::to_string(4 - i / 10) + " s");
                    m_MicTestLevel = s_Mic.Level();
                    Sleep(100);
                }

                const std::vector<float> s_Samples = s_Mic.Stop();
                m_MicTestRunning = false;
                m_MicTestLevel = 0.f;

                const float s_Db = lw::MeasureLoudnessDb(s_Samples);
                const char* s_Class = s_Db < s_Copy.m_WhisperThresholdDb ? "= Flüstern" :
                    s_Db > s_Copy.m_ShoutThresholdDb ? "= Rufen" : "= normal";
                char s_Level[96];
                snprintf(s_Level, sizeof(s_Level), "Pegel %.0f dB %s. ", s_Db, s_Class);

                lw::SpeechRecognizer s_Recognizer;
                std::string s_Text;
                p_Task.SetResult(std::string(s_Level) + "Erkenne ...");

                if (!s_Recognizer.Load(s_ModelPath, s_Error) ||
                    !s_Recognizer.Transcribe(s_Samples, s_Copy.m_Language == lw::Language::German ? "de" : "en", s_Copy.m_SttThreads, s_Text, s_Error)) {
                    p_Task.SetResult(std::string(s_Level) + "Erkennung: " + s_Error);
                    return;
                }

                p_Task.SetResult(std::string(s_Level) + "Verstanden: \"" + s_Text + "\"");
            });
        }
    }

    if (m_MicTestRunning) {
        ImGui::ProgressBar(m_MicTestLevel, { -FLT_MIN, 0.f }, "Pegel");
    }

    if (const std::string s_Result = m_TestTask.Result(); !s_Result.empty()) {
        ImGui::TextWrapped("%s", s_Result.c_str());
    }

    ImGui::TextDisabled("Kein Ton? Windows-Einstellungen > Datenschutz > Mikrofon > \"Desktop-Apps Zugriff erlauben\".");
}

void LivingWorld::DrawTabBehavior(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    ImGui::SeparatorText("Hören");
    p_Changed |= ImGui::SliderFloat("Flüstern – Reichweite (m)", &s.m_RadiusWhisper, 0.5f, 6.f, "%.1f");
    p_Changed |= ImGui::SliderFloat("Normal – Reichweite (m)", &s.m_RadiusNormal, 2.f, 20.f, "%.1f");
    p_Changed |= ImGui::SliderFloat("Rufen – Reichweite (m)", &s.m_RadiusShout, 5.f, 50.f, "%.1f");
    p_Changed |= ImGui::Checkbox("Umstehende reagieren, wenn du rufst", &s.m_BystandersReact);

    if (s.m_BystandersReact) {
        p_Changed |= ImGui::SliderInt("max. zusätzliche Reaktionen", &s.m_MaxBystanderReplies, 0, 4);
    }

    p_Changed |= ImGui::Checkbox("NPCs sprechen dich von sich aus an", &s.m_NpcsInitiate);
    Help("Wenn du eine Weile direkt neben jemandem stehst und ihn ansiehst, spricht er dich an (\"Kann ich Ihnen helfen?\").");

    if (s.m_NpcsInitiate) {
        p_Changed |= ImGui::SliderFloat("nach (s) daneben stehen", &s.m_InitiateAfterSeconds, 2.f, 30.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Pause pro NPC (s)", &s.m_InitiateCooldown, 20.f, 600.f, "%.0f");
    }

    p_Changed |= ImGui::Checkbox("NPCs wissen grob, wo Zielpersonen sind", &s.m_NpcsKnowTargets);
    Help("Personal und Wachen können dir – wenn du sie überzeugst, bestichst oder einschüchterst – verraten, wo sich eine Zielperson aufhält.");

    ImGui::SeparatorText("Erst reden, dann schießen");
    p_Changed |= ImGui::Checkbox("Deeskalation aktiv", &s.m_Deescalation);
    Help("Bewaffnete NPCs schießen nicht sofort: Sie zielen, warnen dich und geben dir Zeit zu reden. "
         "Überzeugst du sie, lassen sie dich gehen. Greifst du an, schießen sie sofort zurück.");

    if (s.m_Deescalation) {
        p_Changed |= ImGui::SliderFloat("Warnzeit vor dem ersten Schuss (s)", &s.m_WarnSeconds, 0.f, 30.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Verlängerung pro Verhandlung (s)", &s.m_HoldFireExtend, 3.f, 60.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Maximale Verhandlungszeit (s)", &s.m_NegotiationMaxSeconds, 10.f, 180.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Überredet bleibt friedlich für (s)", &s.m_StandDownSeconds, 30.f, 900.f, "%.0f");
        p_Changed |= ImGui::SliderFloat("Nach Angriff von 47: sofort schießen für (s)", &s.m_AggressionMemorySeconds, 0.f, 120.f, "%.0f");
        p_Changed |= ImGui::Checkbox("Gruppe: andere Wachen warten das Gespräch ab", &s.m_GroupDeescalation);
        p_Changed |= ImGui::Checkbox("Automatische Warnrufe (\"Keine Bewegung!\")", &s.m_AutoWarnings);
    }

    ImGui::SeparatorText("Reaktionen auf Gewalt");
    p_Changed |= ImGui::Checkbox("Opfer und Zeugen reagieren auf Schüsse, Messerstiche und Angriffe", &s.m_ViolenceReactions);
    Help("Getroffene schreien vor Schmerz, Zeugen geraten in Panik oder (als Wache) greifen ein, wer nur einen Schuss hört, "
         "wird unruhig. Alle merken es sich – sprichst du sie später an, reagieren sie verängstigt, verletzt oder wütend.");

    if (s.m_ViolenceReactions) {
        p_Changed |= ImGui::SliderInt("Hörbare Zeugen pro Vorfall", &s.m_MaxReactionVoices, 0, 6);
    }

    ImGui::SeparatorText("Verwundungen (experimentell)");
    p_Changed |= ImGui::Checkbox("Verletzte verhalten sich realistisch", &s.m_Injuries);
    Help("Beinschuss: humpelt und stolpert. Zwei Beinschüsse, Bauch-/Brustschuss oder Messerstich: geht zu Boden, "
         "kriecht weg und stöhnt. Die Trefferstelle wird aus deiner Zielrichtung bestimmt.");

    if (s.m_Injuries) {
        p_Changed |= ImGui::Checkbox("Realistische Trefferzonen (Beine/Arme töten nicht sofort)", &s.m_RealisticHitZones);
        Help("Ohne diese Option sterben NPCs in HITMAN fast immer am ersten Treffer – dann gibt es keine Verwundeten.");
        p_Changed |= ImGui::SliderFloat("Tempo mit Beinschuss (m/s)", &s.m_LimpSpeed, 0.3f, 2.f, "%.1f");
        p_Changed |= ImGui::Checkbox("Messerstiche überlebbar (zu Boden, kriechen, verbluten)", &s.m_StabWounds);
        Help("Ein Stich tötet nicht sofort: der NPC geht zu Boden, kriecht weg und verblutet. Achtung: HITMANs eigene "
             "Nahkampf-Tötungsanimationen (Takedowns) umgehen das – das gilt nur für Stiche mit einer Klinge in der Hand.");
        p_Changed |= ImGui::Checkbox("Schwer Verletzte verbluten", &s.m_BleedOut);

        if (s.m_BleedOut) {
            p_Changed |= ImGui::SliderFloat("Verbluten frühestens nach (s)", &s.m_BleedSecondsMin, 15.f, 300.f, "%.0f");
            p_Changed |= ImGui::SliderFloat("Verbluten spätestens nach (s)", &s.m_BleedSecondsMax, 20.f, 600.f, "%.0f");
        }
    }

    ImGui::SeparatorText("Drohen, Entwaffnen, Ruf (experimentell)");
    p_Changed |= ImGui::Checkbox("Drohungen machen Angst", &s.m_ThreatReactions);
    Help("Sagst du z. B. \"Ich bin bewaffnet\" oder \"Keine Bewegung, sonst bist du tot\", geraten die Zuhörer in Panik, "
         "betteln um ihr Leben – und Bewaffnete, die gegen deinen Ruf keine Chance sehen, geben auf.");
    p_Changed |= ImGui::Checkbox("Waffe aus der Hand schießen/schlagen", &s.m_Disarm);
    Help("Armtreffer (oder ein Schlag ohne Waffe) gegen einen Bewaffneten: die Waffe fällt zu Boden, er bettelt um sein Leben "
         "und schießt nicht mehr. Die Spiel-KI kann sie theoretisch wieder aufheben.");

    if (s.m_Disarm) {
        p_Changed |= ImGui::SliderInt("Chance pro Treffer (%)", &s.m_DisarmChance, 0, 100);
    }

    p_Changed |= ImGui::Checkbox("Ruf: getötete Bewaffnete verschaffen Respekt", &s.m_Reputation);
    Help("Je mehr Wachen, Polizisten und Soldaten du ausschaltest, desto mehr Angst haben alle vor dir: Wachen zögern länger, "
         "ab 3 getöteten Bewaffneten ergeben sie sich bei Drohungen eher, als sich zu wehren.");
    p_Changed |= ImGui::Checkbox("Eingeschüchterte erstarren vor Angst (statt wegzurennen)", &s.m_SurrenderFreeze);
    Help("Bedrohte und Entwaffnete laufen nicht mehr wie im Originalspiel weg, um Alarm zu schlagen – die Mod lässt sie "
         "höchstens langsam zurückweichen. Experimentell: die Laufanimation des Spiels kann dabei auf der Stelle weiterlaufen.");

    if (s.m_SurrenderFreeze) {
        p_Changed |= ImGui::SliderFloat("Wie lange (s)", &s.m_SurrenderSeconds, 5.f, 120.f, "%.0f");
    }

    ImGui::SeparatorText("Realismus (experimentell)");
    p_Changed |= ImGui::SliderFloat("Schaden an 47", &s.m_PlayerDamageMultiplier, 0.5f, 3.f, "%.2fx");
    Help("1.00x = Originalspiel. Höher = Kugeln sind gefährlicher, Verhandeln lohnt sich mehr. Experimentell.");
}

void LivingWorld::DrawTabNpcs() {
    lw::WorldSnapshot s_World;

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        s_World = m_World;
    }

    if (!s_World.m_InLevel) {
        ImGui::TextDisabled("Nicht in einer Mission.");
        return;
    }

    ImGui::Text("%s  ·  Alarmstufe: %s", s_World.m_LocationName.c_str(), lw::DescribeTension(s_World.m_Ai.m_Tension, lw::Language::German).c_str());
    ImGui::TextColored(c_Muted, "47 trägt: %s%s%s", s_World.m_Player.m_Outfit.c_str(),
                       s_World.m_Ai.m_Trespassing ? "  ·  unbefugt hier" : "",
                       s_World.m_Ai.m_IllegalWeapon ? "  ·  Waffe sichtbar" : "");

    if (ImGui::BeginTable("##npcs", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, { 0.f, -1.f })) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Rolle");
        ImGui::TableSetupColumn("Abstand", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Tut gerade");
        ImGui::TableSetupColumn("Deeskalation", ImGuiTableColumnFlags_WidthFixed, 110.f);
        ImGui::TableSetupColumn("Infos");
        ImGui::TableHeadersRow();

        for (const auto& s_Npc : s_World.m_Npcs) {
            if (s_Npc.m_Distance > 30.f) {
                break;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(lw::DisplayName(s_Npc).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", s_Npc.m_Outfit.c_str(), s_Npc.m_Guard ? " (Wache)" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f m", s_Npc.m_Distance);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(lw::DescribeBehavior(s_Npc.m_Behavior, lw::Language::German).c_str());
            ImGui::TableNextColumn();

            const lw::GateState s_State = m_Gate.State(s_Npc.m_Key, s_World.m_GameTime);

            switch (s_State) {
                case lw::GateState::Warning: ImGui::TextColored(c_Warn, "warnt (%.0f s)", m_Gate.SecondsLeft(s_Npc.m_Key, s_World.m_GameTime)); break;
                case lw::GateState::Firing: ImGui::TextColored(c_Danger, "schießt"); break;
                case lw::GateState::Persuaded: ImGui::TextColored(c_Good, "überredet"); break;
                default: ImGui::TextDisabled("-"); break;
            }

            ImGui::TableNextColumn();
            ImGui::Text(
                "%s%s%s%s", s_Npc.m_Female ? "weiblich" : "männlich", s_Npc.m_Target ? ", ZIEL" : "",
                s_Npc.m_Enforcer ? ", erkennt Verkleidung" : "", !s_Npc.CanTalk() ? ", ausgeschaltet" : ""
            );
        }

        ImGui::EndTable();
    }
}

void LivingWorld::DrawTabConversations() {
    const auto s_Log = m_Book.GlobalLog();

    if (ImGui::Button("In Zwischenablage kopieren")) {
        std::string s_Text;

        for (const auto& s_Entry : s_Log) {
            s_Text += s_Entry.m_Speaker + ": " + s_Entry.m_Text + "\n";
        }

        ImGui::SetClipboardText(s_Text.c_str());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%d Zeilen in dieser Mission", static_cast<int>(s_Log.size()));

    if (ImGui::BeginChild("##conversation", { 0.f, 0.f }, ImGuiChildFlags_Borders)) {
        for (const auto& s_Entry : s_Log) {
            ImGui::TextColored(s_Entry.m_FromPlayer ? c_PlayerName : EmotionColor(s_Entry.m_Emotion), "%s:", s_Entry.m_Speaker.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", s_Entry.m_Text.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f) {
            ImGui::SetScrollHereY(1.f);
        }
    }

    ImGui::EndChild();
}

void LivingWorld::DrawTabDiagnostics(lw::Settings& s, bool& p_Changed) {
    ImGui::Spacing();
    p_Changed |= ImGui::Checkbox("NPC-Infos über den Köpfen anzeigen", &s.m_DebugOverlay);
    p_Changed |= ImGui::Checkbox("Ausführliches Protokoll", &s.m_DebugLog);
    p_Changed |= ImGui::Checkbox("KI-Prompts protokollieren", &s.m_LogPrompts);

    ImGui::SeparatorText("Zustand");
    ImGui::Text("KI: %s", m_Llm.LastStatus().empty() ? "-" : m_Llm.LastStatus().c_str());
    ImGui::Text("Wartende KI-Anfragen: %d", static_cast<int>(m_Llm.PendingCount()));
    ImGui::Text("Sprachausgabe: %s (%d Stimmen)", m_Speech.IsReady() ? "bereit" : "nicht bereit", static_cast<int>(m_Speech.Voices().size()));
    ImGui::Text("Spracherkennung: %s", m_Stt.Status().empty() ? "-" : m_Stt.Status().c_str());
    ImGui::Text("Abgefangene NPC-Schüsse: %d", m_BlockedShots.load());
    ImGui::Text("Waffen ohne erkannten Besitzer: %d", m_OwnerMisses.load());
    ImGui::Text("Gewalt-Ereignisse (Treffer, Schüsse, Tote): %d", m_ViolenceCount.load());
    ImGui::Text("Ruf: %d Bewaffnete und %d Zivilisten getötet", m_ArmedKills.load(), m_CivilianKills.load());
    ImGui::TextDisabled("Protokolldatei: Retail\\mods\\LivingWorld\\LivingWorld.log");

    ImGui::SeparatorText("Protokoll");

    if (ImGui::BeginChild("##log", { 0.f, 0.f }, ImGuiChildFlags_Borders)) {
        for (const auto& s_Entry : lw::log::Recent()) {
            const ImVec4 s_Color = s_Entry.m_Level == lw::log::Level::Error ? c_Danger :
                s_Entry.m_Level == lw::log::Level::Warn ? c_Warn : c_Muted;
            ImGui::TextColored(s_Color, "%s", s_Entry.m_Time.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", s_Entry.m_Text.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f) {
            ImGui::SetScrollHereY(1.f);
        }
    }

    ImGui::EndChild();
}
