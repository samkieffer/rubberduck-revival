#pragma once

#include <cstdint>
#include <cstring>

namespace rd {

extern const uint32_t kPitchTableBits[4096];
extern const int8_t kDriftTable[512];

inline float pitchTable(int index)
{
    float f;
    std::memcpy(&f, &kPitchTableBits[index & 4095], sizeof f);
    return f;
}

} // namespace rd
