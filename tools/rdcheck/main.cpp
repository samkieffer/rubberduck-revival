// rdcheck: verifies that this build renders every reference pattern exactly as
// the original rdh3o.exe does, using hashes recorded from the original.
//
//   rdcheck <installer content folder> [tests/reference_hashes.txt]
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rd/Engine.h"
#include "rd/H30File.h"

namespace {

uint64_t fnv1a(const std::vector<int16_t>& samples)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int16_t s : samples) {
        uint16_t u = static_cast<uint16_t>(s);
        for (int i = 0; i < 2; ++i) {
            h ^= (u >> (8 * i)) & 0xff;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: rdcheck <content-dir> [reference_hashes.txt]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string hashes = argc > 2 ? argv[2] : "tests/reference_hashes.txt";
    std::ifstream in(hashes);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", hashes.c_str());
        return 2;
    }
    int checked = 0, failed = 0, missing = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        const uint64_t expected = std::stoull(line.substr(0, 16), nullptr, 16);
        std::string rel = line.substr(18);
        // Optional play mode suffix: "<path>|<mode>".
        int mode = 0;
        const auto bar = rel.rfind('|');
        if (bar != std::string::npos) {
            mode = std::atoi(rel.c_str() + bar + 1);
            rel = rel.substr(0, bar);
        }
        rd::H30File file;
        if (!rd::H30File::load(dir + "/" + rel, file)) {
            missing++;
            continue;
        }
        rd::Engine engine;
        file.applyTo(engine);
        engine.playMode = static_cast<rd::PlayMode>(mode);
        engine.setRunning(true);
        std::vector<int16_t> out(3000 * rd::kBlockSize);
        for (unsigned b = 0; b < 3000; b += 16)
            engine.render(&out[b * rd::kBlockSize], 3000 - b < 16 ? 3000 - b : 16);
        checked++;
        if (fnv1a(out) != expected) {
            failed++;
            std::printf("MISMATCH %s\n", rel.c_str());
        }
    }
    std::printf("rdcheck: %d patterns checked, %d mismatches, %d not found\n", checked, failed, missing);
    return failed == 0 && checked > 0 ? 0 : 1;
}
