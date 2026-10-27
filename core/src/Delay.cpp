#include "rd/Delay.h"

#include "rd/Numeric.h"

namespace rd {

void Delay::init(uint32_t maxMs, uint16_t sampleRate)
{
    rate = sampleRate;
    uint32_t maxLen = static_cast<uint32_t>((static_cast<uint64_t>(rate) * maxMs) / 1000);
    length = maxLen;
    line.assign(maxLen, 0);
    clear();
}

void Delay::set(uint32_t ms, float g, float fb)
{
    gain = ftol(static_cast<double>(g) * 256.0f);
    feedback = ftol(static_cast<double>(fb) * 256.0f);
    uint32_t newLen = static_cast<uint32_t>((static_cast<uint64_t>(rate) * ms) / 1000);
    for (uint32_t i = length; i < newLen && i < line.size(); ++i)
        line[i] = 0;
    length = newLen;
}

void Delay::clear()
{
    for (uint32_t i = 0; i < length && i < line.size(); ++i)
        line[i] = 0;
    pos = 0;
}

void Delay::process(int16_t* buf, unsigned n)
{
    if (length == 0)
        return;
    const int32_t dryGain = 0x100 - gain;
    const int32_t keep = 0x100 - feedback;
    for (unsigned i = 0; i < n; ++i) {
        int32_t d = line[pos];
        line[pos] = static_cast<int16_t>(divPow2(buf[i] * keep + feedback * d, 8));
        buf[i] = static_cast<int16_t>(divPow2(buf[i] * dryGain + gain * d, 8));
        if (++pos >= length)
            pos = 0;
    }
}

} // namespace rd
