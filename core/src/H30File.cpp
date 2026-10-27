#include "rd/H30File.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace rd {

namespace {

const char kSignature[] = "Rubber Duck H3O+ * D-LUSiON";
const char kOldSignature[] = "RuBBeR DuCK Sequence * D-LUSiON";  // version 1 files (1996)

class Reader
{
public:
    Reader(const uint8_t* d, size_t n) : data(d), size(n) {}
    bool ok() const { return !failed; }
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
        r.i32();
        f.songLength = r.i32() + 1;
        for (int i = 0; i < f.songLength && i < kSongLength; ++i)
            f.song[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        patternCount = 14;
    } else {
        return fail("unsupported file version");
    }
    for (int p = 0; p < patternCount; ++p)
        for (auto& st : f.patterns[static_cast<size_t>(p)].steps) {
            st.note = static_cast<int8_t>(r.u8());
            st.accent = r.i32();
            st.slide = r.i32();
        }
    if (!r.ok())
        return fail("truncated file");

    if (f.version < 2)
        return true;

    Params& p = f.params;
    f.tempoKnob = r.i32();
    r.i32();  // volume: saved, but the original loader skips it
    p.tune = r.i32();
    p.accent = r.i32();
    uint8_t osc1 = r.u8(), osc2 = r.u8();
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
    p.distortionSmooth = r.i32();
    p.distortionMix = r.i32();
    p.delayTime = r.i32();
    p.delayGain = r.i32();
    p.delayFeedback = r.i32();
    if (!r.ok())
        return fail("truncated parameter block");
    f.hasParams = true;
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
    writeI32(out, p.distortionMix);
    writeI32(out, p.distortionSmooth);
    writeI32(out, p.delayTime);
    writeI32(out, p.delayGain);
    writeI32(out, p.delayFeedback);
    return out;
}

} // namespace rd
