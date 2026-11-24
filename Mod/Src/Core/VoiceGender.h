#pragma once

// Automatisch aus EActorVoiceVariation (ZHMModSDK Glacier/Enums.h) erzeugt: alle weiblichen Stimmvarianten.
// Regeln: "FEM" im Namen, Job-Stimmen F05..F10 bzw. FCH/FES/FHI/FI (Maenner: M06..M11 ...), bekannte Frauenfiguren.

#include <algorithm>
#include <iterator>

namespace lw {
    inline bool IsFemaleVoiceVariation(int p_Value) {
        static constexpr int c_Female[] = { 2, 11, 12, 13, 14, 21, 23, 26, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 76, 77, 78, 84, 90, 91, 112, 113, 114, 115, 116, 117, 119, 120, 133, 134, 135, 139, 140, 141, 142, 155, 156, 157, 158, 159, 160, 161, 168, 170, 171, 172, 173, 174, 187, 188, 189, 195, 197, 198, 199, 200, 210, 211, 212, 213, 214, 220, 221, 222, 223, 224, 230, 231, 235, 236, 237, 259, 260, 261, 262, 273, 296, 297, 300, 301, 302, 303, 304, 312, 313, 314, 338, 339, 356, 365, 371, 391, 402, 403, 406, 409, 412, 427, 428, 429, 430, 434, 435, 436, 453, 454, 455, 460, 461, 462, 463, 470, 471, 472, 473, 484, 491, 492, 493, 494, 500, 513, 514, 515, 529, 530, 531, 532, 545, 546, 547, 548, 557, 560, 561, 568 };
        return std::find(std::begin(c_Female), std::end(c_Female), p_Value) != std::end(c_Female);
    }
}