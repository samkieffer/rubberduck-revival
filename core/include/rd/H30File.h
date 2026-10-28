#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rd/Engine.h"
#include "rd/EngineV1.h"

namespace rd {

// Contents of a Rubber Duck machine-state file (.h30), versions 2 to 4.
struct H30File {
    int version = 0;
    int songLength = 1;
    std::array<uint8_t, kSongLength> song{};
    std::array<Pattern, kNumPatterns> patterns{};
    bool hasParams = false;
    int tempoKnob = 128;
    Params params;           // only the fields the file stores are meaningful
    int32_t extra = 0;       // v1.02 play mode (+0x1830 in the v2.12 dialog, unused there)
    // Fields as stored, for the v1.02 engine.
    int32_t rangeStart = 0;  // versions 1-2: pattern range of the sequence modes
    int32_t rangeEnd = 0;
    int32_t volume = 0x80;
    uint8_t osc1Raw = 1, osc2Raw = 0;
    int32_t distortion1 = 0, distortion2 = 0;  // in file order

    // Parses as the original loader (0x403010) does, including its quirks:
    // volume is stored but ignored, and the two distortion fields swap places.
    static bool parse(const uint8_t* data, size_t size, H30File& out, std::string* error = nullptr);
    static bool load(const std::string& path, H30File& out, std::string* error = nullptr);

    // Applies patterns, song and (if present) sound parameters to an engine,
    // keeping parameters the file does not store.
    void applyTo(Engine& engine) const;

    // Same for the v1.02 engine, as its loader 0x40b5e0 does for version 1-2
    // files (version 1: patterns only); later versions are converted.
    void applyTo(EngineV1& engine) const;

    // Writes a version-4 file as the original saver (0x404140) does.
    std::vector<uint8_t> serialize(const Engine& engine, int tempoKnobValue) const;
    // Writes a version-2 file as the v1.02 saver (0x40b1c0) does.
    std::vector<uint8_t> serialize(const EngineV1& engine, int tempoKnobValue) const;
};

} // namespace rd
