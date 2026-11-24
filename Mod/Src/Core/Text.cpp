#include "Text.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace lw {
    std::wstring Utf8ToWide(std::string_view p_Utf8) {
        if (p_Utf8.empty()) {
            return {};
        }

        const int s_Size = MultiByteToWideChar(CP_UTF8, 0, p_Utf8.data(), static_cast<int>(p_Utf8.size()), nullptr, 0);

        if (s_Size <= 0) {
            return {};
        }

        std::wstring s_Result(static_cast<size_t>(s_Size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p_Utf8.data(), static_cast<int>(p_Utf8.size()), s_Result.data(), s_Size);
        return s_Result;
    }

    std::string WideToUtf8(std::wstring_view p_Wide) {
        if (p_Wide.empty()) {
            return {};
        }

        const int s_Size = WideCharToMultiByte(
            CP_UTF8, 0, p_Wide.data(), static_cast<int>(p_Wide.size()), nullptr, 0, nullptr, nullptr
        );

        if (s_Size <= 0) {
            return {};
        }

        std::string s_Result(static_cast<size_t>(s_Size), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, p_Wide.data(), static_cast<int>(p_Wide.size()), s_Result.data(), s_Size, nullptr, nullptr
        );
        return s_Result;
    }

    std::string Trim(std::string_view p_Text) {
        size_t s_Start = 0;
        size_t s_End = p_Text.size();

        while (s_Start < s_End && static_cast<unsigned char>(p_Text[s_Start]) <= ' ') {
            ++s_Start;
        }

        while (s_End > s_Start && static_cast<unsigned char>(p_Text[s_End - 1]) <= ' ') {
            --s_End;
        }

        return std::string(p_Text.substr(s_Start, s_End - s_Start));
    }

    std::string ToLowerAscii(std::string_view p_Text) {
        std::string s_Result(p_Text);
        std::transform(s_Result.begin(), s_Result.end(), s_Result.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        });
        return s_Result;
    }

    std::string ToLowerUtf8(std::string_view p_Text) {
        std::wstring s_Wide = Utf8ToWide(p_Text);

        for (auto& s_Char : s_Wide) {
            s_Char = static_cast<wchar_t>(std::towlower(s_Char));

            // towlower kennt die Umlaute je nach Locale nicht zuverlaessig.
            switch (s_Char) {
                case L'Ä': s_Char = L'ä'; break;
                case L'Ö': s_Char = L'ö'; break;
                case L'Ü': s_Char = L'ü'; break;
                default: break;
            }
        }

        return WideToUtf8(s_Wide);
    }

    std::vector<std::string> SplitWords(std::string_view p_Text) {
        std::vector<std::string> s_Words;
        std::string s_Current;

        for (const char c : p_Text) {
            const auto s_Byte = static_cast<unsigned char>(c);
            const bool s_IsWordChar = (s_Byte >= 0x80) || std::isalnum(s_Byte) || c == '-' || c == '\'';

            if (s_IsWordChar) {
                s_Current.push_back(c);
            }
            else if (!s_Current.empty()) {
                s_Words.push_back(std::move(s_Current));
                s_Current.clear();
            }
        }

        if (!s_Current.empty()) {
            s_Words.push_back(std::move(s_Current));
        }

        return s_Words;
    }

    bool ContainsWord(std::string_view p_Haystack, std::string_view p_Needle) {
        if (p_Needle.size() < 3) {
            return false;
        }

        const std::string s_Needle = ToLowerUtf8(p_Needle);

        for (const auto& s_Word : SplitWords(p_Haystack)) {
            if (ToLowerUtf8(s_Word) == s_Needle) {
                return true;
            }
        }

        return false;
    }

    std::string TruncateUtf8(std::string_view p_Text, size_t p_MaxBytes) {
        if (p_Text.size() <= p_MaxBytes) {
            return std::string(p_Text);
        }

        size_t s_Cut = p_MaxBytes;

        // Nicht mitten in einer UTF-8-Folge abschneiden (Folgebytes = 10xxxxxx).
        while (s_Cut > 0 && (static_cast<unsigned char>(p_Text[s_Cut]) & 0xC0) == 0x80) {
            --s_Cut;
        }

        return std::string(p_Text.substr(0, s_Cut));
    }

    uint32_t StableHash(std::string_view p_Text) {
        uint32_t s_Hash = 2166136261u;

        for (const char c : p_Text) {
            s_Hash ^= static_cast<unsigned char>(c);
            s_Hash *= 16777619u;
        }

        return s_Hash;
    }

    std::string CleanSpokenText(std::string_view p_Text) {
        std::string s_Result;
        s_Result.reserve(p_Text.size());

        bool s_InStageDirection = false;

        for (size_t i = 0; i < p_Text.size(); ++i) {
            const char c = p_Text[i];

            // Regieanweisungen wie *seufzt* oder [lacht] fliegen raus.
            if (c == '*' || c == '[' || c == ']') {
                if (c == '[') s_InStageDirection = true;
                else if (c == ']') s_InStageDirection = false;
                else s_InStageDirection = !s_InStageDirection;
                continue;
            }

            if (s_InStageDirection) {
                continue;
            }

            if (c == '\r' || c == '\n' || c == '\t') {
                if (!s_Result.empty() && s_Result.back() != ' ') {
                    s_Result.push_back(' ');
                }
                continue;
            }

            if (static_cast<unsigned char>(c) < 0x20) {
                continue;
            }

            if (c == '_' || c == '#' || c == '`') {
                continue;
            }

            s_Result.push_back(c);
        }

        // Doppelte Leerzeichen zusammenfassen.
        std::string s_Collapsed;
        s_Collapsed.reserve(s_Result.size());

        for (const char c : s_Result) {
            if (c == ' ' && !s_Collapsed.empty() && s_Collapsed.back() == ' ') {
                continue;
            }

            s_Collapsed.push_back(c);
        }

        std::string s_Trimmed = Trim(s_Collapsed);

        // Anfuehrungszeichen um den ganzen Satz entfernen.
        if (s_Trimmed.size() >= 2 && s_Trimmed.front() == '"' && s_Trimmed.back() == '"') {
            s_Trimmed = Trim(std::string_view(s_Trimmed).substr(1, s_Trimmed.size() - 2));
        }

        return s_Trimmed;
    }

    std::string FormatSeconds(double p_Seconds) {
        char s_Buffer[32];
        snprintf(s_Buffer, sizeof(s_Buffer), "%.1f s", p_Seconds);
        return s_Buffer;
    }
}
