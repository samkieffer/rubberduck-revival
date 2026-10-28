#pragma once

#include <cstdint>
#include <cstring>

namespace rd {

extern const uint32_t kPitchTableBits[4096];
extern const int8_t kDriftTable[512];
// Same tables as found in RUBDUCK.EXE v1.02.
extern const uint32_t kPitchTableV1Bits[4096];
extern const int8_t kDriftTableV1[512];

inline float pitchTable(int index)
{
    float f;
    std::memcpy(&f, &kPitchTableBits[index & 4095], sizeof f);
    return f;
}

inline float pitchTableV1(int index)
{
    float f;
    std::memcpy(&f, &kPitchTableV1Bits[index & 4095], sizeof f);
    return f;
}

} // namespace rd
