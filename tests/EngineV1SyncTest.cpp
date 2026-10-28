// Host synchronisation of the v1.02 engine: driving it with positions that
// advance by exactly one block increment per block must give the same output
// as the free-running original sequencer, in every play mode.
//   engine_v1_sync <fixtures folder>
#include <cstdio>
#include <string>
#include <vector>

#include "rd/EngineV1.h"
#include "rd/H30File.h"

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "tests/fixtures";
    int failures = 0;
    for (int n = 1; n <= 24; ++n) {
        char base[32];
        std::snprintf(base, sizeof base, "v1_synthetic_%02d.h30", n);
        rd::H30File f;
        if (!rd::H30File::load(dir + "/" + base, f)) {
            std::printf("cannot load %s/%s\n", dir.c_str(), base);
            return 1;
        }
        rd::EngineV1 free, synced;
        f.applyTo(free);
        f.applyTo(synced);
        free.setRunning(true);
        synced.setRunning(true);
        const unsigned blocks = 4000;
        std::vector<int16_t> a(blocks * rd::kBlockSizeV1), b(a.size());
        uint32_t p = 0;
        unsigned bars = 0;
        for (unsigned k = 0; k < blocks; ++k) {
            free.render(&a[k * rd::kBlockSizeV1], 1);
            if (synced.setNextPosition(p) & rd::EngineV1::kNewBar)
                bars++;
            synced.render(&b[k * rd::kBlockSizeV1], 1);
            p += static_cast<uint32_t>(synced.blockIncrement());
        }
        size_t diff = 0;
        for (size_t i = 0; i < a.size(); ++i)
            diff += a[i] != b[i];
        const bool ok = diff == 0 && bars > 0;
        std::printf("%-28s %s  mismatches=%zu bars=%u\n", base, ok ? "OK  " : "FAIL", diff, bars);
        if (!ok)
            failures++;
    }
    return failures == 0 ? 0 : 1;
}
