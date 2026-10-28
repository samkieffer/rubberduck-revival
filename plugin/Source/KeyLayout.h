#pragma once

#include <cstdint>

// Character typed by a key of the US keyboard *position* under the user's
// current layout (so the tracker piano rows sit at the same place on AZERTY,
// QWERTZ...). Positions are macOS virtual key codes (kVK_ANSI_*); returns 0
// when unknown. Kept apart from JUCE so the Carbon headers stay out of its way.
namespace keylayout {

enum Position : uint16_t {
    A = 0, S = 1, D = 2, F = 3, H = 4, G = 5, Z = 6, X = 7, C = 8, V = 9, B = 11,
    Q = 12, W = 13, E = 14, R = 15, Y = 16, T = 17, K1 = 18, K2 = 19, K3 = 20, K6 = 22, K5 = 23,
    K9 = 25, K7 = 26, K0 = 29, O = 31, U = 32, I = 34, P = 35, L = 37, J = 38, Semicolon = 41,
    Comma = 43, Slash = 44, N = 45, M = 46, Period = 47
};

char32_t characterAt(Position position);

} // namespace keylayout
