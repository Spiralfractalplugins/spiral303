#pragma once

#include <algorithm>
#include <cmath>

// The magnitude of the filter cascade, in decibels.
//
// It lives here, without JUCE, because the WebAssembly build draws the same
// curve in its interface. Source/gui/ScopeAxes.h calls this too, so the two
// always agree.
//
// Second-order lowpass magnitude in dB, blended between the single section
// (12 dB/oct) and the cascaded pair (24 dB/oct) the engine crossfades to. The
// resonance is counted exactly once: the full Q is split over the two sections,
// each running at sqrt(q). Peak of one^2: with u = r^2 the denominator
// g = (1-u)^2 + u/q is smallest at u = 1 - 1/(2q), where 1/g = q/(1 - 1/(4q)),
// which is 18.75 -> 25.46 dB at q = 18.5 - the top of the scope's window.
inline float filterResponseDb (float freq, float cutoff, float res, float slope)
{
    const float q = 0.5f + res * res * 18.0f;
    const float ratio = freq / std::max (1.0f, cutoff);
    const float r2 = ratio * ratio;
    const float one = 1.0f / std::sqrt ((1.0f - r2) * (1.0f - r2) + r2 / q);
    return 20.0f * std::log10 (std::max (1.0e-5f, one + slope * (one * one - one)));
}
