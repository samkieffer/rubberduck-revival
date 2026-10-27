#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rd/Engine.h"

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
    int32_t extra = 0;       // unidentified field (+0x1830 in the original dialog)

    // Parses as the original loader (0x403010) does, including its quirks:
    // volume is stored but ignored, and the two distortion fields swap places.
    static bool parse(const uint8_t* data, size_t size, H30File& out, std::string* error = nullptr);
    static bool load(const std::string& path, H30File& out, std::string* error = nullptr);

    // Applies patterns, song and (if present) sound parameters to an engine,
    // keeping parameters the file does not store.
    void applyTo(Engine& engine) const;

    // Writes a version-4 file as the original saver (0x404140) does.
    std::vector<uint8_t> serialize(const Engine& engine, int tempoKnobValue) const;
};

} // namespace rd
