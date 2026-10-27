#include "rd/LadderFilter.h"

#include "rd/Numeric.h"

namespace rd {

void LadderFilter::setCutoff(float x)
{
    cutoff = x;
    pTarget = static_cast<float>(static_cast<double>(x) + x - 1.0);
}

void LadderFilter::setResonance(float r)
{
    double d = static_cast<double>(cutoff) - 0.7f;
    rTarget = static_cast<float>((d * d * 7.0f + 0.3f) * r);
}

void LadderFilter::reset()
{
    x1 = x2 = x3 = x4 = 0;
    y1 = y2 = y3 = y4 = 0;
    rState = pState = feedback = 0;
}

static inline double clampSample(double v)
{
    if (v > 32767.0f)
        return 32767.0f;
    if (v < -32768.0f)
        return -32768.0f;
    return v;
}

void LadderFilter::process(int16_t* buf, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        double r = (static_cast<double>(rState) + rTarget) * 0.5f;
        float rStored = static_cast<float>(r);
        rState = rStored;
        double p = (static_cast<double>(pState) + pTarget) * 0.5f;
        pState = static_cast<float>(p);

        double t = clampSample(static_cast<double>(buf[i]) - feedback);

        double y = (t - y1) * p + x1;
        y1 = static_cast<float>(y);
        x1 = static_cast<float>(t);
        t = (t + y1) * 0.5;

        y = (t - y2) * p + x2;
        y2 = static_cast<float>(y);
        x2 = static_cast<float>(t);
        t = (t + y2) * 0.5;

        y = (t - y3) * p + x3;
        y3 = static_cast<float>(y);
        x3 = static_cast<float>(t);
        t = (t + y3) * 0.5;

        y = (t - y4) * p + x4;
        y4 = static_cast<float>(y);
        x4 = static_cast<float>(t);
        t = (t + y4) * 0.5;

        feedback = static_cast<float>(static_cast<double>(rStored) * t);
        buf[i] = static_cast<int16_t>(ftol(clampSample(t)));
    }
}

} // namespace rd
