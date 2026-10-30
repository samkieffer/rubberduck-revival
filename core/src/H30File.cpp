#include "rd/H30File.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace rd {

namespace {

const char kSignature[] = "Rubber Duck H3O+ * D-LUSiON";
const char kOldSignature[] = "RuBBeR DuCK Sequence * D-LUSiON";  // version 1 files (1996)
// RubDuck's block after a version-2 file: the v1.02 engine's patterns E and F,
// which the original's 14 patterns leave out (see serialize).
const char kRubDuckTag[] = "RubDuck";
constexpr int32_t kRubDuckBlockVersion = 1;

class Reader
{
public:
    Reader(const uint8_t* d, size_t n) : data(d), size(n) {}
    bool ok() const { return !failed; }
    bool atEnd() const { return pos >= size; }
    uint8_t u8()
    {
        if (pos + 1 > size) { failed = true; return 0; }
        return data[pos++];
    }
    int32_t i32()
    {
        if (pos + 4 > size) { failed = true; return 0; }
        int32_t v;
        std::memcpy(&v, data + pos, 4);
        pos += 4;
        return v;
    }
    // MFC CArchive CString (short form: one length byte).
    std::string str()
    {
        uint8_t len = u8();
        if (pos + len > size) { failed = true; return {}; }
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }

private:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool failed = false;
};

void writeI32(std::vector<uint8_t>& out, int32_t v)
{
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

void writePattern(std::vector<uint8_t>& out, const Pattern& pat)
{
    for (const auto& st : pat.steps) {
        out.push_back(static_cast<uint8_t>(st.note));
        writeI32(out, st.accent);
        writeI32(out, st.slide);
    }
}

void readPattern(Reader& r, Pattern& pat)
{
    for (auto& st : pat.steps) {
        st.note = static_cast<int8_t>(r.u8());
        st.accent = r.i32();
        st.slide = r.i32();
    }
}

bool isEmpty(const Pattern& pat)
{
    for (const auto& st : pat.steps)
        if (st.note != Step{}.note || st.accent != 0 || st.slide != 0)
            return false;
    return true;
}

} // namespace

bool H30File::parse(const uint8_t* data, size_t size, H30File& f, std::string* error)
{
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    Reader r(data, size);
    const std::string signature = r.str();
    if (signature != kSignature && signature != kOldSignature)
        return fail("not a Rubber Duck file");
    f = H30File();
    f.version = r.i32();

    int patternCount;
    if (f.version == 3 || f.version == 4) {
        f.songLength = r.i32();
        for (auto& s : f.song) s = r.u8();
        patternCount = kNumPatterns;
    } else if (f.version < 3) {
        f.rangeStart = r.i32();
        f.rangeEnd = r.i32();
        f.songLength = f.rangeEnd + 1;
        for (int i = 0; i < f.songLength && i < kSongLength; ++i)
            f.song[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        patternCount = kNumPatternsV1;
    } else {
        return fail("unsupported file version");
    }
    for (int p = 0; p < patternCount; ++p)
        readPattern(r, f.patterns[static_cast<size_t>(p)]);
    if (!r.ok())
        return fail("truncated file");

    if (f.version < 2)
        return true;

    Params& p = f.params;
    f.tempoKnob = r.i32();
    f.volume = r.i32();  // saved, but the v2.12 loader skips it
    p.tune = r.i32();
    p.accent = r.i32();
    uint8_t osc1 = r.u8(), osc2 = r.u8();
    f.osc1Raw = osc1;
    f.osc2Raw = osc2;
    p.osc1Type = static_cast<uint8_t>(f.version == 3 ? osc1 + 1 : osc1);
    p.osc2Type = static_cast<uint8_t>(f.version == 2 ? osc2 + 1 : osc2);
    p.osc2Double = r.i32() != 0;
    p.oscMix = r.i32();
    p.cutoff = r.i32();
    p.attack = r.i32();
    p.decay = r.i32();
    p.modulation = r.i32();
    p.resonance = r.i32();
    if (f.version == 4)
        p.modification = r.i32() != 0;
    else
        p.modification = f.version == 3;
    if (f.version >= 3) {
        p.squelch = r.i32();
        p.drive = r.i32();
        p.tweak = r.i32() != 0;
        p.tweet = r.i32() != 0;
    }
    f.extra = r.i32();
    // The loader routes the first distortion field to "Drive" and the second to
    // "Gain", while the saver writes them the other way round.
    f.distortion1 = p.distortionDrive = r.i32();
    f.distortion2 = p.distortionGain = r.i32();
    p.delayTime = r.i32();
    p.delayGain = r.i32();
    p.delayFeedback = r.i32();
    if (!r.ok())
        return fail("truncated parameter block");
    f.hasParams = true;
    // Patterns E and F of a version-2 file written by RubDuck; the original
    // stops reading before them.
    if (f.version == 2 && !r.atEnd() && r.str() == kRubDuckTag) {
        r.i32();  // block version (1)
        for (int i = kNumPatternsV1; i < kNumPatterns; ++i)
            readPattern(r, f.patterns[static_cast<size_t>(i)]);
        if (!r.ok())
            return fail("truncated RubDuck block");
    }
    return true;
}

bool H30File::load(const std::string& path, H30File& out, std::string* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(bytes.data(), bytes.size(), out, error);
}

void H30File::applyTo(Engine& e) const
{
    e.patterns = patterns;
    e.song = song;
    e.songLength = songLength > 0 ? songLength : 1;
    e.selectPattern(song[0] < kNumPatterns ? song[0] : 0);
    if (!hasParams)
        return;
    const int32_t volume = e.params.volume;
    Params p = params;
    if (version < 3) {
        p.squelch = e.params.squelch;
        p.drive = e.params.drive;
        p.tweak = e.params.tweak;
        p.tweet = e.params.tweet;
    }
    p.volume = volume;
    e.params = p;
    e.setTempoKnob(tempoKnob);
    e.resetSmoothing();
}

void H30File::applyTo(EngineV1& e) const
{
    e.patterns = patterns;
    if (version < 3) {
        e.rangeStart = rangeStart;
        e.rangeEnd = rangeEnd;
    } else {
        // Song of a later version: its first pattern, or the range it spans.
        int hi = 0;
        for (int i = 0; i < songLength && i < kSongLength; ++i)
            if (song[static_cast<size_t>(i)] < kNumPatterns && song[static_cast<size_t>(i)] > hi)
                hi = song[static_cast<size_t>(i)];
        e.rangeStart = 0;
        e.rangeEnd = hi;
    }
    e.selectPattern(version < 3 ? rangeStart : (song[0] < kNumPatterns ? song[0] : 0));
    if (!hasParams) {
        e.reset();
        return;
    }
    ParamsV1& p = e.params;
    p.tune = params.tune;
    p.accent = params.accent;
    p.osc2Double = params.osc2Double;
    p.oscMix = params.oscMix;
    p.cutoff = params.cutoff;
    p.attack = params.attack;
    p.decay = params.decay;
    p.modulation = params.modulation;
    p.resonance = params.resonance;
    p.delayTime = params.delayTime;
    p.delayGain = params.delayGain;
    p.delayFeedback = params.delayFeedback;
    if (version == 2) {
        p.volume = volume;
        p.osc1Type = osc1Raw;
        p.osc2Type = osc2Raw;
        p.distortionAmount = distortion1;
        p.distortionCurve = distortion2;
        if (extra >= 0 && extra <= 2)
            e.playMode = static_cast<PlayModeV1>(extra);
    } else {
        // v2.12 numbering (triangle is RubDuck's 3 in the v1.02 engine).
        static const uint8_t kOsc1[4] = {3, 0, 1, 2};
        p.osc1Type = version == 3 ? osc1Raw : kOsc1[params.osc1Type & 3];
        p.osc2Type = params.osc2Type;
        p.distortionAmount = params.distortionGain;
        p.distortionCurve = params.distortionDrive;
    }
    e.setTempoKnob(tempoKnob);
    e.reset();
}

std::vector<uint8_t> H30File::serialize(const EngineV1& e, int tempoKnobValue) const
{
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(sizeof(kSignature) - 1));
    out.insert(out.end(), kSignature, kSignature + sizeof(kSignature) - 1);
    writeI32(out, 2);
    writeI32(out, e.rangeStart);
    writeI32(out, e.rangeEnd);
    for (int i = 0; i < kNumPatternsV1; ++i)
        writePattern(out, e.patterns[static_cast<size_t>(i)]);
    const ParamsV1& p = e.params;
    writeI32(out, tempoKnobValue);
    writeI32(out, p.volume);
    writeI32(out, p.tune);
    writeI32(out, p.accent);
    out.push_back(p.osc1Type);
    out.push_back(p.osc2Type);
    writeI32(out, p.osc2Double);
    writeI32(out, p.oscMix);
    writeI32(out, p.cutoff);
    writeI32(out, p.attack);
    writeI32(out, p.decay);
    writeI32(out, p.modulation);
    writeI32(out, p.resonance);
    writeI32(out, static_cast<int32_t>(e.playMode));
    writeI32(out, p.distortionAmount);
    writeI32(out, p.distortionCurve);
    writeI32(out, p.delayTime);
    writeI32(out, p.delayGain);
    writeI32(out, p.delayFeedback);
    // Patterns E and F, when used, in RubDuck's block: without them the file is
    // what the original saver writes.
    bool extended = false;
    for (int i = kNumPatternsV1; i < kNumPatterns; ++i)
        extended = extended || !isEmpty(e.patterns[static_cast<size_t>(i)]);
    if (extended) {
        out.push_back(static_cast<uint8_t>(sizeof(kRubDuckTag) - 1));
        out.insert(out.end(), kRubDuckTag, kRubDuckTag + sizeof(kRubDuckTag) - 1);
        writeI32(out, kRubDuckBlockVersion);
        for (int i = kNumPatternsV1; i < kNumPatterns; ++i)
            writePattern(out, e.patterns[static_cast<size_t>(i)]);
    }
    return out;
}

std::vector<uint8_t> H30File::serialize(const Engine& e, int tempoKnobValue) const
{
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(sizeof(kSignature) - 1));
    out.insert(out.end(), kSignature, kSignature + sizeof(kSignature) - 1);
    writeI32(out, 4);
    writeI32(out, e.songLength);
    out.insert(out.end(), e.song.begin(), e.song.end());
    for (const auto& pat : e.patterns)
        for (const auto& st : pat.steps) {
            out.push_back(static_cast<uint8_t>(st.note));
            writeI32(out, st.accent);
            writeI32(out, st.slide);
        }
    const Params& p = e.params;
    writeI32(out, tempoKnobValue);
    writeI32(out, p.volume);
    writeI32(out, p.tune);
    writeI32(out, p.accent);
    out.push_back(p.osc1Type);
    out.push_back(p.osc2Type);
    writeI32(out, p.osc2Double);
    writeI32(out, p.oscMix);
    writeI32(out, p.cutoff);
    writeI32(out, p.attack);
    writeI32(out, p.decay);
    writeI32(out, p.modulation);
    writeI32(out, p.resonance);
    writeI32(out, p.modification);
    writeI32(out, p.squelch);
    writeI32(out, p.drive);
    writeI32(out, p.tweak);
    writeI32(out, p.tweet);
    writeI32(out, extra);
    writeI32(out, p.distortionGain);
    writeI32(out, p.distortionDrive);
    writeI32(out, p.delayTime);
    writeI32(out, p.delayGain);
    writeI32(out, p.delayFeedback);
    return out;
}

} // namespace rd
