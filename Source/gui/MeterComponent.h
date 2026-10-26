#pragma once

#include <array>
#include <algorithm>
#include <cmath>

#include <JuceHeader.h>

#include "../PluginProcessor.h"
#include "DarkLookAndFeel.h"
#include "MeterBallistics.h"

/** Output-meter in the psykick style with an optional horizontal layout: the
    bar shows RMS, the thin line is the held peak, the limiter gain reduction
    shows as either a flame (vertical) or a glowing strip (horizontal). */
class MeterComponent : public juce::Component,
                       public juce::SettableTooltipClient,
                       public juce::Timer
{
public:
    explicit MeterComponent (PluginProcessor& p, bool horizontal = false)
        : processor (p), horizontalLayout (horizontal)
    {
        startTimerHz (30);
        setTooltip ("Master output meter after the limiter. Green/yellow/red are "
                    "level zones; the held white line is the peak, red at the "
                    "ceiling. GR shows gain reduction in dB.");
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        // The first tick has no predecessor to measure against; assume the poll
        // rate the timer was started at instead of freezing the bar.
        const double dt = lastTick > 0.0 ? now - lastTick : 1.0 / 30.0;
        lastTick = now;

        meters.update (processor.takeMeterPeak(), processor.getMeterRms(),
                       processor.getLimiterGain(), dt);

        const int barQ = tenths (meters.barDb());
        const int holdQ = tenths (meters.holdDb());
        const int grQ = tenths (meters.grDb());
        const int clipQ = meters.clipped() ? 1 : 0;
        if (barQ == lastBarQ && holdQ == lastHoldQ && grQ == lastGrQ && clipQ == lastClipQ)
            return;
        lastBarQ = barQ;
        lastHoldQ = holdQ;
        lastGrQ = grQ;
        lastClipQ = clipQ;
        repaint();
    }

    /** Drive the ballistics without a timer or an audio thread, so the paint can
        be checked pixel by pixel at a known level. */
    void setLevelsForTests (float peak, float rms, float limiterGain, double dt = 1.0 / 30.0)
    {
        stopTimer();
        meters.update (peak, rms, limiterGain, dt);
        repaint();
    }

    float barDbForTests() const { return meters.barDb(); }

    void paint (juce::Graphics& g) override
    {
        const auto& th = Skins::current();
        auto r = getLocalBounds().toFloat();
        g.setColour (th.background.darker (0.3f));
        g.fillRoundedRectangle (r, th.corner);

        const float scale = jmin (1.0f, r.getHeight() / 84.0f);

        if (horizontalLayout)
        {
            // Header variant: a horizontal bar growing right = louder, with
            // the 0 dB mark at the right-high edge.
            const float barX = r.getX() + 4.0f, barY = r.getY() + r.getHeight() * 0.35f,
                        barW = r.getWidth() - 8.0f, barH = r.getHeight() * 0.55f;
            g.setColour (th.knobTrack.withAlpha (0.55f));
            g.fillRoundedRectangle (barX, barY, barW, barH, 2.0f);

            // Zones, anchored on the same axis the bar is drawn on.
            const float xRed = barX + barW * MeterBallistics::fraction (-6.0f);
            g.setColour (th.meterGreen.withAlpha (0x30 / 255.0f));
            g.fillRect (barX, barY, xRed - barX, barH);
            g.setColour (th.meterAmber.withAlpha (0x30 / 255.0f));
            g.fillRect (xRed, barY, barX + barW - xRed, barH);

            // RMS fill. The gradient spans the whole track, not the fill, so a
            // given x keeps its colour no matter how loud the bar is.
            const float rmsNorm = MeterBallistics::fraction (meters.barDb());
            juce::ColourGradient grad (th.meterGreen, barX, barY + barH,
                                        th.meterRed, barX + barW, barY + barH, false);
            grad.addColour (MeterBallistics::fraction (-6.0f), th.meterAmber);
            g.setGradientFill (grad);
            g.fillRect (barX + 1.0f, barY + 1.0f, barW * rmsNorm - 2.0f, barH - 2.0f);

            // Held peak line.
            g.setColour (meters.clipped() ? th.gate : th.playhead.withAlpha (0.85f));
            g.fillRect (barX + barW * MeterBallistics::fraction (meters.holdDb()) - 1.0f,
                        barY, 2.0f, barH);

            // GR strip on the bottom while limiting.
            const float grDb = meters.grDb();
            g.setColour (grDb > 0.5f ? th.accent.withAlpha (0.9f) : th.knobTrack);
            g.fillRect (barX, r.getBottom() - 4.0f, barW, 2.0f);
        }
        else
        {
            // (the original vertical paint, unchanged)
            auto r2 = r;
            // fall through to the existing vertical code by re-painting on a
            // conventional bar:
            const float barX = r2.getX() + 3.0f;
            const float barW = r2.getWidth() * 0.42f;
            const float barY = r2.getY() + 4.0f;
            const float barH = r2.getHeight() - 8.0f;
            auto levelToY = [&] (float db)
            {
                return barY + (1.0f - MeterBallistics::fraction (db)) * barH;
            };
            g.setColour (th.knobTrack.withAlpha (0.55f));
            g.fillRoundedRectangle (barX, barY, barW, barH, 2.0f);
            g.setColour (th.meterAmber.withAlpha (0x30 / 255.0f));
            g.fillRect (static_cast<int> (barX), static_cast<int> (barY),
                        static_cast<int> (barW),
                        static_cast<int> (levelToY (-6.0f) - barY));
            g.setColour (th.meterGreen);
            const int topY = static_cast<int> (levelToY (meters.barDb()));
            g.fillRect (static_cast<int> (barX + 0.5f), topY,
                        static_cast<int> (barW - 1.0f),
                        static_cast<int> (barY + barH - topY));
            const int peakY = static_cast<int> (levelToY (meters.holdDb()));
            g.setColour (meters.clipped() ? th.gate : th.playhead.withAlpha (0.85f));
            g.fillRect (static_cast<int> (barX), peakY - 1, static_cast<int> (barW), 2);
            const float grDb = meters.grDb();
            g.setColour (grDb > 0.5f ? th.accent : th.textDim);
            g.setFont (Theme::font (8.0f * scale));
            g.drawText ("GR " + juce::String (grDb, 1),
                        r.getX(), r.getBottom() - 14.0f, r.getWidth(), 12,
                        juce::Justification::centred, false);
        }
    }

    void setScale ([[maybe_unused]] float s) {}

private:
    static int tenths (float db) { return static_cast<int> (std::lround (db * 10.0f)); }

    PluginProcessor& processor;
    MeterBallistics meters;
    double lastTick = 0.0;
    int lastBarQ = -10000, lastHoldQ = -10000, lastGrQ = -10000, lastClipQ = -1;
    bool horizontalLayout = false;
};
