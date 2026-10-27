#include "rd/Shaper.h"

#include <cmath>

#include "rd/Numeric.h"

namespace rd {

Shaper::Shaper()
{
    configure(0, 0, 0);
    s210_ = 0;
}

void Shaper::configure(uint8_t mode, uint8_t amount, uint8_t ceiling)
{
    amount_ = amount;
    mode_ = mode;
    const float g = static_cast<float>(1.0 - std::log(256.0 - amount) / std::log(256.0));

    if (mode <= 2) {
        const double k = static_cast<double>(g) * 100.0f + 1.0f;
        const int32_t clip = (ceiling + 0x100) << (mode == 2 ? 7 : 6);
        table_[128] = 0;
        for (int j = 1; j <= 128; ++j) {
            int32_t v = ftol(static_cast<double>(j * 128) * k);
            if (v > clip)
                v = clip;
            table_[128 + j] = static_cast<int16_t>(v);
            table_[128 - j] = static_cast<int16_t>(mode == 1 ? v : -v);
        }
    } else if (mode == 3) {
        double e = std::exp(static_cast<double>(ceiling) * -0.01227184630308513);
        double a = 1.0 - e;
        s208_ = static_cast<float>(a);
        s20c_ = static_cast<float>(1.0f - a);
    }
}

static inline int32_t lookup(const int16_t* t, int16_t s)
{
    int32_t x = s;
    int32_t idx = (x + 0x8000) >> 8;
    int32_t frac = x - (idx << 8) + 0x8000;
    int32_t v = t[idx] * (0x100 - frac) + t[idx + 1] * frac;
    return divPow2(v, 8);
}

void Shaper::process(int16_t* buf, unsigned n, int (*rand)())
{
    switch (mode_) {
    case 0:
        for (unsigned i = 0; i < n; ++i)
            buf[i] = static_cast<int16_t>(lookup(table_, buf[i]));
        break;
    case 1:
        for (unsigned i = 0; i < n; ++i) {
            int32_t y = lookup(table_, buf[i]);
            double s = (static_cast<double>(y) - s20c_) + s208_ * 0.99;
            s208_ = static_cast<float>(s);
            buf[i] = static_cast<int16_t>(ftol(s));
            s20c_ = static_cast<float>(y);
        }
        break;
    case 2:
        for (unsigned i = 0; i < n; ++i) {
            int32_t v = table_[(static_cast<int32_t>(buf[i]) + 0x8000) >> 8];
            int32_t half = v / 2;
            int32_t r = rand ? rand() : 0;
            buf[i] = static_cast<int16_t>((r >> 8) * (half / 128) + half);
        }
        break;
    case 3:
        for (unsigned i = 0; i < n; ++i) {
            double wet = static_cast<double>(buf[i]) * s20c_ * 19.7f + static_cast<double>(s208_) * s210_;
            s210_ = static_cast<float>(wet);
            if (wet > 32767.0f)
                s210_ = 32767.0f;
            else if (wet < -32768.0f)
                s210_ = -32768.0f;
            int32_t dry = (0xff - amount_) * buf[i];
            int32_t v = ftol(s210_) * amount_ + dry;
            buf[i] = static_cast<int16_t>(divPow2(v, 8));
        }
        break;
    }
}

} // namespace rd
