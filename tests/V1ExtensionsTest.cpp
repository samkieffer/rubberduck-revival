// RubDuck's extensions of the v1.02 engine (version 2.0): patterns E and F,
// stored in a block after the v1.02 file, and the triangle oscillator.
// Original files are covered by bit_exact_fixtures and engine_v1_sync.
//   v1_extensions
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "rd/EngineV1.h"
#include "rd/H30File.h"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok)
        failures++;
}

rd::Pattern line(int root)
{
    rd::Pattern p;
    for (int s = 0; s < rd::kStepsPerPattern; ++s) {
        p.steps[static_cast<size_t>(s)].note = static_cast<int8_t>(root + (s % 4) * 3);
        p.steps[static_cast<size_t>(s)].accent = s % 4 == 0;
        p.steps[static_cast<size_t>(s)].slide = s % 8 == 7;
    }
    return p;
}

bool same(const rd::Pattern& a, const rd::Pattern& b)
{
    for (int s = 0; s < rd::kStepsPerPattern; ++s) {
        const auto& x = a.steps[static_cast<size_t>(s)];
        const auto& y = b.steps[static_cast<size_t>(s)];
        if (x.note != y.note || x.accent != y.accent || x.slide != y.slide)
            return false;
    }
    return true;
}

std::vector<int16_t> render(rd::EngineV1& e, unsigned blocks)
{
    std::vector<int16_t> out(blocks * rd::kBlockSizeV1);
    e.setRunning(true);
    for (unsigned k = 0; k < blocks; ++k)
        e.render(&out[k * rd::kBlockSizeV1], 1);
    return out;
}

double rms(const std::vector<int16_t>& v)
{
    double sum = 0.0;
    for (int16_t s : v)
        sum += static_cast<double>(s) * s;
    return std::sqrt(sum / static_cast<double>(v.size())) / 32768.0;
}

} // namespace

int main()
{
    // Files: without E and F, the v1.02 layout; with them, the same bytes and
    // RubDuck's block after.
    rd::EngineV1 a;
    for (int i = 0; i < rd::kNumPatternsV1; ++i)
        a.patterns[static_cast<size_t>(i)] = line(36 + i);
    a.rangeStart = 2;
    a.rangeEnd = 15;
    rd::EngineV1 b = a;
    b.patterns[14] = line(60);
    b.patterns[15] = line(30);
    b.params.osc1Type = 3;
    a.params.osc1Type = 3;
    const rd::H30File writer;
    const auto plain = writer.serialize(a, 128);
    const auto extended = writer.serialize(b, 128);
    // Signature 1 + 27, version, range, 14 patterns x 16 steps x 9 bytes, then
    // 17 int32 parameters and the two oscillator bytes.
    check(plain.size() == 1 + 27 + 4 + 8 + 14 * 16 * 9 + 17 * 4 + 2, "without E and F: the v1.02 file layout");
    check(extended.size() == plain.size() + 1 + 7 + 4 + 2 * 16 * 9
              && std::equal(plain.begin(), plain.end(), extended.begin()),
          "with E and F: the same bytes, then RubDuck's block");

    rd::H30File f;
    std::string err;
    check(rd::H30File::parse(extended.data(), extended.size(), f, &err) && f.version == 2, "parse a file with the block");
    rd::EngineV1 c;
    f.applyTo(c);
    bool kept = true;
    for (int i = 0; i < rd::kNumPatterns; ++i)
        kept = kept && same(c.patterns[static_cast<size_t>(i)], b.patterns[static_cast<size_t>(i)]);
    check(kept, "patterns 0-F survive the round trip");
    check(c.rangeStart == 2 && c.rangeEnd == 15 && c.currentPatternIndex() == 2, "range up to F survives");
    check(c.params.osc1Type == 3, "triangle survives");

    auto tagless = plain;
    tagless.insert(tagless.end(), {3, 'a', 'b', 'c'});
    check(rd::H30File::parse(tagless.data(), tagless.size(), f) && same(f.patterns[14], rd::Pattern{}),
          "other trailing bytes are ignored");
    auto cut = extended;
    cut.pop_back();
    check(!rd::H30File::parse(cut.data(), cut.size(), f), "a truncated block is an error");

    // Engine: E plays, and Sequence goes through E and F.
    {
        rd::EngineV1 e;
        e.patterns[14] = line(48);
        e.selectPattern(14);
        check(e.currentPatternIndex() == 14, "pattern E can be selected");
        check(rms(render(e, 2000)) > 0.01, "pattern E plays");
        rd::EngineV1 empty;
        empty.selectPattern(14);
        check(rms(render(empty, 2000)) < 1e-4, "an empty pattern E is silent");
    }
    {
        rd::EngineV1 e;
        e.rangeStart = 13;
        e.rangeEnd = 15;
        e.playMode = rd::PlayModeV1::Sequence;
        e.selectPattern(13);
        std::vector<int> seen{e.currentPatternIndex()};
        e.setRunning(true);
        std::vector<int16_t> block(rd::kBlockSizeV1);
        for (unsigned k = 0; k < 40000 && seen.size() < 5; ++k) {
            e.render(block.data(), 1);
            if (e.currentPatternIndex() != seen.back())
                seen.push_back(e.currentPatternIndex());
        }
        check(seen == std::vector<int>({13, 14, 15, 13, 14}), "Sequence plays D, E, F, then D again");
    }

    // Triangle: a waveform of its own.
    {
        rd::EngineV1 saw, tri;
        saw.patterns[0] = tri.patterns[0] = line(48);
        saw.params.osc1Type = 1;
        tri.params.osc1Type = 3;
        const auto s = render(saw, 2000), t = render(tri, 2000);
        check(rms(t) > 0.01, "triangle plays");
        check(s != t, "triangle differs from saw");
    }

    return failures == 0 ? 0 : 1;
}
