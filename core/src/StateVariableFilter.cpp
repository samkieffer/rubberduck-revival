#include "rd/StateVariableFilter.h"

#include "rd/Numeric.h"

namespace rd {

void StateVariableFilter::configure(Mode m, int32_t sampleRate, float frequency, float damping)
{
    rate = sampleRate;
    mode = m;
    setFrequency(frequency);
    setDamping(damping);
}

void StateVariableFilter::setFrequency(float hz)
{
    const double pi = 3.14159265358979323846;
    double s = std::sin(static_cast<double>(hz) * pi / static_cast<double>(rate));
    f = static_cast<float>(s + s);
}

void StateVariableFilter::setDamping(float x)
{
    q = static_cast<float>(1.0 / (static_cast<double>(x) * 400.0f + 2.0));
}

void StateVariableFilter::reset()
{
    band1 = low1 = band2 = low2 = 0;
}

void StateVariableFilter::process(int16_t* buf, unsigned n)
{
    if (n == 0)
        return;
    const double F = f, Q = q;
    switch (mode) {
    case LowPass: {
        double low = low1, band = band1;
        for (unsigned i = 0; i < n; ++i) {
            low += F * band;
            double hp = (static_cast<double>(buf[i]) - low) - Q * band;
            buf[i] = static_cast<int16_t>(fistp(low));
            band += hp * F;
        }
        band1 = static_cast<float>(band);
        low1 = static_cast<float>(low);
        break;
    }
    case HighPass: {
        double low = low1, band = band1;
        for (unsigned i = 0; i < n; ++i) {
            low += F * band;
            double hp = (static_cast<double>(buf[i]) - low) - Q * band;
            buf[i] = static_cast<int16_t>(fistp(hp));
            band = F * hp + band;
        }
        low1 = static_cast<float>(low);
        band1 = static_cast<float>(band);
        break;
    }
    case BandPass: {
        double low = low1, band = band1;
        for (unsigned i = 0; i < n; ++i) {
            low += F * band;
            double hp = (static_cast<double>(buf[i]) - low) - Q * band;
            band += hp * F;
            buf[i] = static_cast<int16_t>(fistp(band));
        }
        low1 = static_cast<float>(low);
        band1 = static_cast<float>(band);
        break;
    }
    case DoubleLowPass: {
        double b1 = band1, l1 = low1, l2 = low2;
        for (unsigned i = 0; i < n; ++i) {
            int32_t in = static_cast<int32_t>(buf[i]) >> 3;
            double L1 = F * b1 + l1;
            double L2 = F * static_cast<double>(band2) + l2;
            l1 = L1;
            l2 = L2;
            double hp1 = (static_cast<double>(in) - l1) - Q * b1;
            double d2 = ((L1 - L2) - Q * l1) * F;
            buf[i] = static_cast<int16_t>(fistp(l2));
            double nb1 = hp1 * F + b1;
            band2 = static_cast<float>(d2 + l1);
            b1 = nb1;
        }
        band1 = static_cast<float>(b1);
        low2 = static_cast<float>(l2);
        low1 = static_cast<float>(l1);
        break;
    }
    }
}

} // namespace rd
