#pragma once

#include <JuceHeader.h>

#include "../PluginProcessor.h"
#include "DarkLookAndFeel.h"
#include "ScopeAxes.h"

/** Two stacked indicators: the oscillator shape the morph knob is pointing at, and
    the lowpass curve it runs into. The response is drawn from the filter's own
    coefficients, so the peak moves with Res and the knee with Cutoff and Slope. */
class ScopeComponent : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    enum Mode { Combined, WaveOnly, FilterOnly };
    explicit ScopeComponent (PluginProcessor& p, Mode mode = Combined) : processor (p), mode (mode)
    {
        if (mode == WaveOnly)
            setTooltip ("One cycle of the oscillator, sine through saw to square.");
        else if (mode == FilterOnly)
        {
            setTooltip ("Resonant lowpass response, 20 Hz to 20 kHz.\n"
                        "drag: cutoff across / resonance up-down - wheel: cutoff.");
            setMouseCursor (juce::MouseCursor (juce::MouseCursor::CrosshairCursor));
        }
        else
            setTooltip ("Top: one cycle of the oscillator, sine through saw to square.\n"
                        "Bottom: the resonant lowpass response, 20 Hz to 20 kHz.");
    }

    void setScale (float s)
    {
        uiScale = s;
        repaint();
    }

    /** The dB gutter carries labels only while they are still readable. */
    static float gutterFor (float scale) { return scale >= 0.5f ? 15.0f * scale : 0.0f; }

    /** How many times a curve was rebuilt: the point of the cache is that painting
        the same parameters again does not re-run the maths. */
    int curveBuildsForTests() const { return curveBuilds; }

    /** Where the response axes live for the current bounds - the same rectangle the
        grid is drawn on, so a test can ask for a line instead of guessing it. */
    juce::Rectangle<float> responsePlot() const
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f).reduced (2.0f);
        return area.withLeft (area.getX() + gutterFor (uiScale));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = Skins::current();
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (th.panel);
        g.fillRoundedRectangle (area, th.corner);
        g.setColour (th.panelOutline);
        g.drawRoundedRectangle (area, th.corner, 1.0f);

        if (mode == WaveOnly)
            drawWave (g, area.reduced (2.0f));
        else if (mode == FilterOnly)
            drawResponse (g, area.reduced (2.0f));
        else
        {
            const float half = (area.getHeight() - 3.0f * uiScale) * 0.5f;
            drawWave (g, { area.getX() + 2.0f, area.getY() + 2.0f, area.getWidth() - 4.0f, half });
            drawResponse (g, { area.getX() + 2.0f, area.getY() + half + 4.0f * uiScale,
                                area.getWidth() - 4.0f, half });
        }
    }

    /** The cursor readout is painted only while the pointer is inside the plot;
        nothing here is polled, so an offline paint never shows it. */
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        setHover (respPlot.contains (p.x, p.y) ? p.x : -1.0f);
    }

    void mouseExit (const juce::MouseEvent&) override { setHover (-1.0f); }

    /** The response box is an input as well as a display, but only on its own:
        the wave half has nothing to grab and the combined scope is decoration. */
    void mouseDown (const juce::MouseEvent&) override
    {
        if (mode == FilterOnly)
        {
            dragCutoff = processor.getCutoffHz();
            dragRes    = processor.getResonance();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == FilterOnly)
            applyFilterDelta (e.position.x - e.mouseDownPosition.x,
                              e.position.y - e.mouseDownPosition.y);
    }

    /** One notch is one percent of the cutoff's normalised span, five with Shift -
        the same convention KnobSlider uses, so the wheel cannot mean two different
        speeds on the knob and on the plot. */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (mode != FilterOnly || wheel.deltaY == 0.0f)
            { Component::mouseWheelMove (e, wheel); return; }
        applyWheelDelta (wheel.deltaY, e.mods.isShiftDown());
    }

    /** Test seams: a headless test cannot post a real MouseEvent without a peer,
        but it can drive the same mapping the mouse handlers run. */
    void dragForTests (float dx, float dy)
    {
        dragCutoff = processor.getCutoffHz();
        dragRes    = processor.getResonance();
        applyFilterDelta (dx, dy);
    }

    void wheelForTests (float delta, bool shift) { applyWheelDelta (delta, shift); }

private:
    void setHover (float x)
    {
        if (std::abs (x - hoverX) < 0.5f)
            return;
        hoverX = x;
        repaint();
    }

    /** Pixels since the button went down, measured against where the drag started:
        an absolute mapping would teleport the knee under the first click. Three
        decades span the whole plot, so what the eye reads as an octave of travel
        is an octave of cutoff wherever the knee happens to stand. */
    void applyFilterDelta (float dx, float dy)
    {
        if (mode != FilterOnly)
            return;
        const auto plot = responsePlot();
        if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
            return;

        writeParam (ParamIDs::cutoff,
                    juce::jlimit (40.0f, 8000.0f, dragCutoff * std::pow (10.0f, dx / plot.getWidth() * 3.0f)));
        writeParam (ParamIDs::res,
                    juce::jlimit (0.0f, 1.0f, dragRes - dy / plot.getHeight()));
        repaint();
    }

    void applyWheelDelta (float delta, bool shift)
    {
        if (mode != FilterOnly)
            return;
        writeNormalised (ParamIDs::cutoff, normalised (ParamIDs::cutoff)
                                           + delta * (shift ? 0.05f : 0.01f));
    }

    /** setValueNotifyingHost is the only loop-free way in: the editor's
        SliderAttachment hears it and moves the knob with its callbacks muted, so
        the plot drives the parameter and the knob follows it, not the other way. */
    void writeParam (const juce::String& id, float value)
    {
        if (auto* param = processor.state().getParameter (id))
            param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (value));
    }

    float normalised (const juce::String& id)
    {
        auto* param = processor.state().getParameter (id);
        return param != nullptr ? param->getValue() : 0.0f;
    }

    void writeNormalised (const juce::String& id, float norm)
    {
        if (auto* param = processor.state().getParameter (id))
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& th = Skins::current();
        const float morph = processor.getWaveForScope();

        g.setColour (th.gridLine);
        g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 0.7f);
        // Phase grid: the cycle is the axis, so the quarters are its rulers.
        for (int i = 1; i < 4; ++i)
        {
            const float x = std::round (r.getX() + r.getWidth() * static_cast<float> (i) / 4.0f);
            g.setColour (i == 2 ? th.gridBeat : th.gridLine);
            g.drawVerticalLine (static_cast<int> (x), r.getY(), r.getBottom());
        }

        // The editor repaints these boxes 30 times a second whether or not a knob
        // moved, so the curve is built once per parameter set and only blitted after.
        const std::vector<float> key { morph, uiScale, r.getX(), r.getY(), r.getWidth(), r.getHeight() };
        if (key != waveKey)
        {
            waveKey = key;
            ++curveBuilds;
            const int samples = jmax (96, roundToInt (r.getWidth() / 2.0f));
            waveLine.clear();
            waveFill.clear();
            waveLine.preallocateSpace (samples * 3 + 8);
            waveFill.preallocateSpace (samples * 3 + 16);
            for (int i = 0; i <= samples; ++i)
            {
                const float t = static_cast<float> (i) / samples;   // phase 0..1
                const float y = r.getCentreY() - shapeAt (morph, t) * r.getHeight() * 0.42f;
                const float x = r.getX() + t * r.getWidth();
                if (i == 0)
                {
                    waveLine.startNewSubPath (x, y);
                    waveFill.startNewSubPath (x, r.getCentreY());
                }
                else
                {
                    waveLine.lineTo (x, y);
                    waveFill.lineTo (x, y);
                }
            }
            waveFill.lineTo (r.getRight(), r.getCentreY());
            waveFill.closeSubPath();
        }

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (th.accent.withAlpha (0.13f));
        g.fillPath (waveFill);
        g.setColour (th.accent);
        g.strokePath (waveLine, juce::PathStrokeType (1.6f));
        g.restoreState();

        drawCaption (g, r, waveName (morph));
    }

    void drawResponse (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& th = Skins::current();

        // A narrow gutter on the left carries the dB scale; below half size the
        // labels would be unreadable, so the plot takes the whole box.
        const float gutter = gutterFor (uiScale);
        ScopeAxes axes { r.withLeft (r.getX() + gutter) };
        respPlot = axes.plot;

        const float cutoff = processor.getCutoffHz();
        const float res = processor.getResonance();
        const float slope = processor.getSlope();

        const std::vector<float> key { cutoff, res, slope, uiScale,
                                       axes.plot.getX(), axes.plot.getY(),
                                       axes.plot.getWidth(), axes.plot.getHeight() };
        if (key != respKey)
        {
            respKey = key;
            ++curveBuilds;
            const int points = jmax (96, roundToInt (axes.plot.getWidth() / 2.0f));
            respLine.clear();
            respFill.clear();
            respLine.preallocateSpace (points * 3 + 8);
            respFill.preallocateSpace (points * 3 + 16);
            for (int i = 0; i <= points; ++i)
            {
                const float t = static_cast<float> (i) / points;
                const float freq = ScopeAxes::kFreqMin
                                 * std::pow (ScopeAxes::kFreqMax / ScopeAxes::kFreqMin, t);
                const float x = axes.freqToX (freq);
                const float y = axes.dbToY (ScopeAxes::responseDb (freq, cutoff, res, slope));
                if (i == 0)
                {
                    respLine.startNewSubPath (x, y);
                    respFill.startNewSubPath (x, axes.plot.getBottom());
                }
                else
                {
                    respLine.lineTo (x, y);
                    respFill.lineTo (x, y);
                }
            }
            respFill.lineTo (axes.plot.getRight(), axes.plot.getBottom());
            respFill.closeSubPath();
        }

        // The curve is not clamped any more, so the plot rectangle has to be a real
        // clip: at full resonance the hump tops out exactly on kDbTop and anything
        // pushed past the window - the far skirt below - must leave the panel alone.
        g.saveState();
        const juce::Rectangle<int> clip = axes.plot.toNearestInt();
        g.reduceClipRegion (clip);

        // Tint first, grid on top of it: a composited band would shift every
        // line off its palette literal, and the literals are what the tests
        // (and the Dark-skin invariant) compare against.
        g.setColour (th.accentAlt.withAlpha (0.13f));
        g.fillPath (respFill);

        float lastGridX = -100.0f;
        for (const float f : ScopeAxes::gridFrequencies())
        {
            const float x = std::round (axes.freqToX (f));
            // At small widths the decade minors fall on top of each other; the
            // majors keep their own column and the rest are dropped.
            if (x - lastGridX < 3.0f)
                continue;
            lastGridX = x;
            g.setColour (ScopeAxes::isMajorFrequency (f) ? th.gridBeat : th.gridLine);
            // 20 kHz lands exactly on the right edge, which the clip excludes, so
            // the last decade would never get its own line.
            g.drawVerticalLine (juce::jlimit (clip.getX(), clip.getRight() - 1, static_cast<int> (x)),
                                static_cast<float> (clip.getY()), static_cast<float> (clip.getBottom()));
        }
        for (const float db : ScopeAxes::gridDecibels())
        {
            g.setColour (db == 0.0f ? th.gridBeat : th.gridLine);
            g.drawHorizontalLine (static_cast<int> (std::round (axes.dbToY (db))),
                                  axes.plot.getX(), axes.plot.getRight());
        }

        g.setColour (th.accentAlt);
        g.strokePath (respLine, juce::PathStrokeType (1.5f));
        g.restoreState();

        if (gutter > 0.0f)
        {
            g.setColour (th.textDim);
            g.setFont (Theme::font (7.0f * uiScale));
            for (const float db : ScopeAxes::gridDecibels())
                g.drawText (juce::String (db, 0), r.getX(), axes.dbToY (db) - 6.0f * uiScale,
                            gutter - 2.0f, 12.0f * uiScale, juce::Justification::centredRight, false);
            for (const float f : { 100.0f, 1000.0f, 10000.0f })
                g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String (f, 0),
                            axes.freqToX (f) - 16.0f, axes.plot.getBottom() - 11.0f * uiScale,
                            32.0f, 10.0f * uiScale, juce::Justification::centred, false);
        }

        // The corner chip carries the cursor readout while the pointer is inside the
        // plot: the log axis is honest about where a line stands, but nobody reads a
        // frequency off pixels. It takes the summary's slot rather than a second one,
        // so the two labels can never overlap.
        const bool hovering = hoverX >= axes.plot.getX() && hoverX <= axes.plot.getRight();
        const float hoverFreq = hovering ? axes.xToFreq (hoverX) : 0.0f;
        if (hovering)
        {
            g.setColour (th.playhead.withAlpha (0.4f));
            g.drawVerticalLine (roundToInt (hoverX), axes.plot.getY(), axes.plot.getBottom());
        }

        drawCaption (g, r, hovering
            ? juce::String::formatted ("%.0f Hz  %+.1f dB",
                                       hoverFreq, ScopeAxes::responseDb (hoverFreq, cutoff, res, slope))
            : juce::String::formatted ("%.0f Hz  Q%.1f  %.0f dB/oct",
                                       cutoff, ScopeAxes::kMaxQ * res * res + 0.5f, 12.0f + 12.0f * slope));
    }

    /** A label in the corner of a plot. The trace runs behind it, so it sits on a
        chip of the palette's own label pair rather than on the curve. */
    void drawCaption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text) const
    {
        const auto& th = Skins::current();
        const auto font = Theme::font (8.5f * uiScale, juce::Font::bold);
        const float pad = 3.0f * uiScale;
        const juce::Rectangle<float> glyphs (Theme::textWidth (font, text),
                                             static_cast<float> (font.getHeight()));
        const auto box = glyphs.withPosition (r.getRight() - pad * 2.0f - glyphs.getWidth(),
                                              r.getY() + pad)
                           .expanded (pad, pad * 0.5f);
        g.setColour (th.chip);
        g.fillRoundedRectangle (box, 2.0f);
        g.setFont (font);
        g.setColour (th.chipText);
        g.drawText (text, box.toNearestInt(), juce::Justification::centred, false);
    }

    /** Same blend the oscillator uses, evaluated over one cycle. */
    static float shapeAt (float morph, float t)
    {
        const float saw = 2.0f * t - 1.0f;
        const float square = t < 0.5f ? 1.0f : -1.0f;
        const float sine = std::sin (juce::MathConstants<float>::twoPi * t);

        if (morph <= 0.5f)
            return (1.0f - morph * 2.0f) * sine + morph * 2.0f * saw;
        const float k = (morph - 0.5f) * 2.0f;
        return (1.0f - k) * saw + k * square;
    }

    static juce::String waveName (float morph)
    {
        if (morph < 0.04f)  return "SINE";
        if (morph > 0.96f)  return "SQUARE";
        if (std::abs (morph - 0.5f) < 0.04f) return "SAW";
        return morph < 0.5f ? "SINE-SAW" : "SAW-SQUARE";
    }

    Mode mode = Combined;
    PluginProcessor& processor;
    float uiScale = 1.0f;

    juce::Path waveLine, waveFill, respLine, respFill;
    std::vector<float> waveKey, respKey;
    int curveBuilds = 0;
    float hoverX = -1.0f;
    float dragCutoff = 900.0f, dragRes = 0.5f;
    juce::Rectangle<float> respPlot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
