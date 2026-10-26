#pragma once

#include <cmath>
#include <cstddef>

/** One 2x oversampling stage around a memoryless-in-time shaper: the input is
    zero-stuffed to twice the rate, low-passed at the old Nyquist frequency (a
    31-tap Hann-windowed sinc), shaped at the higher rate, low-passed again and
    decimated. Whatever the shaper creates above the old Nyquist frequency is
    cut before it can fold back down as aliasing.

    Stages nest: a 4x path is one stage whose shaper is another stage. Each stage
    adds about 15 samples at its own high rate of latency.

    JuceLite-compatible: plain C++, no JUCE types, so the WebAssembly core compiles
    this header too. */
class Oversampler2x
{
public:
    static constexpr int kTaps = 31;

    Oversampler2x()
    {
        // Ideal lowpass at a quarter of the high rate (the old Nyquist frequency),
        // windowed. Odd offsets from the centre are zero, which keeps the filter a
        // halfband: every second tap is nothing but the centre.
        constexpr double pi = 3.14159265358979323846;
        const int centre = (kTaps - 1) / 2;
        double sum = 0.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const int m = n - centre;
            const double ideal = m == 0 ? 0.5 : std::sin (pi * m / 2.0) / (pi * m);
            const double window = 0.5 - 0.5 * std::cos (2.0 * pi * n / (kTaps - 1));
            taps[n] = ideal * window;
            sum += taps[n];
        }
        // Unit DC gain for the decimator; the interpolator needs twice that, because
        // half of its inputs are zeros.
        for (int n = 0; n < kTaps; ++n)
        {
            taps[n] = static_cast<float> (taps[n] / sum);
            upTaps[n] = 2.0f * taps[n];
        }
    }

    void reset()
    {
        for (int n = 0; n < kTaps; ++n)
        {
            upLine[n] = 0.0f;
            downLine[n] = 0.0f;
        }
    }

    /** One base-rate sample in, one base-rate sample out. `shape` maps a sample at
        the doubled rate to a sample at the doubled rate. */
    template <typename Shaper>
    float process (float x, Shaper&& shape)
    {
        pushUp (2.0f * x);
        const float u0 = convolve (upLine, upTaps);
        pushUp (0.0f);
        const float u1 = convolve (upLine, upTaps);

        pushDown (shape (u0));
        pushDown (shape (u1));
        return convolve (downLine, taps);
    }

private:
    void pushUp (float v)
    {
        for (int n = kTaps - 1; n > 0; --n)
            upLine[n] = upLine[n - 1];
        upLine[0] = v;
    }

    void pushDown (float v)
    {
        for (int n = kTaps - 1; n > 0; --n)
            downLine[n] = downLine[n - 1];
        downLine[0] = v;
    }

    static float convolve (const float* line, const float* h)
    {
        float acc = 0.0f;
        for (int n = 0; n < kTaps; ++n)
            acc += h[n] * line[n];
        return acc;
    }

    float taps[kTaps] {};
    float upTaps[kTaps] {};
    float upLine[kTaps] {};
    float downLine[kTaps] {};
};
