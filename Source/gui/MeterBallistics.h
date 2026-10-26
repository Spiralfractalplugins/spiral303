#pragma once

#include <algorithm>
#include <cmath>

#include <JuceHeader.h>

/** Meter ballistics in dB per second.
    The old meter multiplied its held peak by a constant once per timer tick, so
    the release speed was really "per poll", not per second: a 30 Hz timer and a
    60 Hz one gave visibly different falls, and a window that stopped redrawing
    for a second jumped straight to the floor. Everything here is in decibels and
    moves by rate * dt, so the fall reads the same whatever the poll rate is.
    Only this uses elapsed time; textures stay on a seeded Random. */
struct MeterBallistics
{
    static constexpr float kFloorDb = -60.0f;
    static constexpr float kTopDb = 0.0f;
    static constexpr float kBarFallDbPerSecond = 40.0f;
    static constexpr float kHoldFallDbPerSecond = 20.0f;
    static constexpr double kHoldSeconds = 1.5;
    static constexpr double kClipSeconds = 1.0;

    void update (float peak, float rms, float limiterGain, double dtSeconds)
    {
        // A stalled UI thread must not drop the bar to the floor in one step, and
        // a counter wrap must not run the decay backwards.
        const double dt = std::min (std::max (dtSeconds, 0.0), 0.25);

        // Attack is instant, release is a rate: the max of the two is the bar.
        barValue = std::max (toDb (rms),
                             barValue - static_cast<float> (kBarFallDbPerSecond) * static_cast<float> (dt));

        const float peakDb = toDb (peak);
        if (peakDb >= holdValue)
        {
            holdValue = peakDb;
            holdAge = 0.0;
        }
        else
        {
            holdAge += dt;
            if (holdAge > kHoldSeconds)
                holdValue = std::max (barValue,
                                      holdValue - static_cast<float> (kHoldFallDbPerSecond)
                                                * static_cast<float> (dt));
        }

        grValue = std::max (reductionDb (limiterGain),
                            grValue - static_cast<float> (kBarFallDbPerSecond) * static_cast<float> (dt));

        clipAge = peak > 1.0f ? 0.0 : clipAge + dt;
    }

    float barDb() const { return barValue; }
    float holdDb() const { return holdValue; }
    float grDb() const { return grValue; }
    bool clipped() const { return clipAge < kClipSeconds; }

    /** The one and only dB to pixel mapping: every zone edge, knee and marker on
        the meter is a call to this, so they cannot drift apart. */
    static float fraction (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kTopDb - kFloorDb));
    }

    static float toDb (float linear)
    {
        return juce::Decibels::gainToDecibels (linear, kFloorDb);
    }

    static float reductionDb (float limiterGain)
    {
        return std::max (0.0f, -juce::Decibels::gainToDecibels (limiterGain, kFloorDb));
    }

private:
    float barValue = kFloorDb, holdValue = kFloorDb, grValue = 0.0f;
    double holdAge = 0.0, clipAge = kClipSeconds;
};
