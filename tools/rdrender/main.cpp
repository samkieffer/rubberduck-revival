// rdrender: renders a Rubber Duck .h30 machine state to a WAV file.
//
//   rdrender pattern.h30 out.wav [--bars N] [--blocks N] [--raw] [--mode single|sequence|shuffle]
//
// The output is 44.1 kHz 16-bit stereo through the original output stage
// (master volume), or with --raw the mono voice signal before the volume.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rd/Engine.h"
#include "rd/H30File.h"
#include "rd/Mixing.h"

namespace {

void writeLE(std::FILE* f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        std::fputc(static_cast<int>((v >> (8 * i)) & 0xff), f);
}

bool writeWav(const char* path, const std::vector<int16_t>& samples, int channels)
{
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    writeLE(f, 36 + dataBytes, 4);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLE(f, 16, 4);
    writeLE(f, 1, 2);
    writeLE(f, static_cast<uint32_t>(channels), 2);
    writeLE(f, rd::kSampleRate, 4);
    writeLE(f, static_cast<uint32_t>(rd::kSampleRate * channels * 2), 4);
    writeLE(f, static_cast<uint32_t>(channels * 2), 2);
    writeLE(f, 16, 2);
    std::fwrite("data", 1, 4, f);
    writeLE(f, dataBytes, 4);
    for (int16_t s : samples)
        writeLE(f, static_cast<uint16_t>(s), 2);
    std::fclose(f);
    return true;
}

int usage()
{
    std::fprintf(stderr, "usage: rdrender in.h30 out.wav [--bars N] [--blocks N] [--raw] [--mode single|sequence|shuffle]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return usage();
    const char* in = argv[1];
    const char* out = argv[2];
    double bars = 4;
    long blocks = -1;
    bool raw = false;
    rd::PlayMode mode = rd::PlayMode::Single;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bars") && i + 1 < argc) bars = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--blocks") && i + 1 < argc) blocks = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--raw")) raw = true;
        else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) {
            std::string m = argv[++i];
            mode = m == "sequence" ? rd::PlayMode::Sequence : m == "shuffle" ? rd::PlayMode::Shuffle : rd::PlayMode::Single;
        } else return usage();
    }

    rd::H30File file;
    std::string err;
    if (!rd::H30File::load(in, file, &err)) {
        std::fprintf(stderr, "%s: %s\n", in, err.c_str());
        return 1;
    }
    rd::Engine engine;
    file.applyTo(engine);
    engine.playMode = mode;
    engine.setRunning(true);

    if (blocks < 0) {
        // One bar = 16 steps = 2^20 position units.
        double blockMs = 128000.0 / rd::kSampleRate;
        double barMs = 4 * 60000.0 / engine.getTempo();
        blocks = static_cast<long>(bars * barMs / blockMs) + 1;
    }

    std::vector<int16_t> voice(static_cast<size_t>(blocks) * rd::kBlockSize);
    const unsigned chunk = 16;
    for (long b = 0; b < blocks; b += chunk) {
        unsigned n = static_cast<unsigned>(blocks - b < chunk ? blocks - b : chunk);
        engine.render(&voice[static_cast<size_t>(b) * rd::kBlockSize], n);
    }

    if (raw)
        return writeWav(out, voice, 1) ? 0 : 1;

    // Output stage: left gain (128 - pan) * G >> 8, right (pan + 129) * G >> 8,
    // with G = volume << 8 and pan 0, each applied with the mixer's fixed-point gain.
    std::vector<int16_t> left = voice, right = voice;
    const uint32_t g = static_cast<uint32_t>(engine.params.volume) << 8;
    for (size_t b = 0; b < voice.size(); b += rd::kBlockSize) {
        uint32_t gl = (128u * g) >> 8, gr = (129u * g) >> 8;
        if (gl <= 0x7f) std::memset(&left[b], 0, rd::kBlockSize * 2); else rd::mix::applyGain(&left[b], rd::kBlockSize, gl);
        if (gr <= 0x7f) std::memset(&right[b], 0, rd::kBlockSize * 2); else rd::mix::applyGain(&right[b], rd::kBlockSize, gr);
    }
    std::vector<int16_t> stereo(voice.size() * 2);
    for (size_t i = 0; i < voice.size(); ++i) {
        stereo[2 * i] = left[i];
        stereo[2 * i + 1] = right[i];
    }
    return writeWav(out, stereo, 2) ? 0 : 1;
}
