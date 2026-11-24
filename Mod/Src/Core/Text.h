#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lw {
    std::wstring Utf8ToWide(std::string_view p_Utf8);
    std::string WideToUtf8(std::wstring_view p_Wide);

    std::string Trim(std::string_view p_Text);
    std::string ToLowerAscii(std::string_view p_Text);

    // Kleinbuchstaben inkl. deutscher Umlaute (UTF-8 -> UTF-8), fuer unscharfe Namensvergleiche.
    std::string ToLowerUtf8(std::string_view p_Text);

    bool ContainsWord(std::string_view p_Haystack, std::string_view p_Needle);
    std::vector<std::string> SplitWords(std::string_view p_Text);

    // Kuerzt auf hoechstens p_MaxBytes, ohne ein UTF-8-Zeichen zu zerschneiden.
    std::string TruncateUtf8(std::string_view p_Text, size_t p_MaxBytes);

    // Stabiler 32-Bit-Hash (FNV-1a) fuer Persoenlichkeit/Stimme pro NPC.
    uint32_t StableHash(std::string_view p_Text);

    // Entfernt Steuerzeichen und Markdown-Reste, die TTS und Untertitel stoeren.
    std::string CleanSpokenText(std::string_view p_Text);

    std::string FormatSeconds(double p_Seconds);
}
