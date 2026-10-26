#pragma once

#include <JuceHeader.h>

#include "Palette.h"
#include "SkinTextures.h"

namespace Theme
{
    /** JUCE 8 deprecated Font(height, styleFlags), so all UI text goes through here. */
    inline juce::Font font (float height, int style = juce::Font::plain)
    {
        return juce::Font (juce::FontOptions (height, style));
    }

    /** JUCE 8 dropped Font::getStringWidth; a GlyphArrangement measures properly. */
    inline float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, ga.getNumGlyphs(), true).getWidth();
    }
}

/** The knob is drawn in layers, and the finish decides what each layer is made
    of. MoldedPlastic keeps the original two-stop dome and nothing else, so the
    Dark skin renders exactly the pixels it did before this system existed. */
namespace KnobPaint
{
    inline float upperArc (juce::Graphics& g, float cx, float cy, float r,
                           float from, float to, juce::Colour colour, float thickness)
    {
        juce::Path p;
        p.addCentredArc (cx, cy, r, r, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness));
        return r;
    }

    inline void shadow (juce::Graphics& g, const Palette& th, float cx, float cy, float r)
    {
        // Fake blur: two offset ellipses of decreasing size instead of a
        // Gaussian, which would cost a second image per knob.
        g.setColour (th.shadow.withAlpha (0.42f));
        g.fillEllipse (cx - r + 1.5f, cy - r + 2.5f, r * 2.0f, r * 2.0f);
        g.setColour (th.shadow.withAlpha (0.22f));
        const float inner = r * 0.92f;
        g.fillEllipse (cx - inner + 1.0f, cy - inner + 1.8f, inner * 2.0f, inner * 2.0f);
    }

    /** Hex collar for the glowing finish: machined flats under the dome. */
    inline void collar (juce::Graphics& g, const Palette& th, float cx, float cy, float r)
    {
        const float rr = r * 1.10f;
        juce::Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const float a = static_cast<float> (i) * (juce::MathConstants<float>::twoPi / 6.0f) + 0.26f;
            const float px = cx + std::cos (a) * rr, py = cy + std::sin (a) * rr;
            i == 0 ? hex.startNewSubPath (px, py) : hex.lineTo (px, py);
        }
        hex.closeSubPath();
        g.setColour (th.bevelLo.withAlpha (0.85f));
        g.fillPath (hex);
        g.setColour (th.bevelHi.withAlpha (0.55f));
        g.strokePath (hex, juce::PathStrokeType (1.0f));
    }

    /** Nickel washer under a phenolic body, wider than the dome it carries. */
    inline void washer (juce::Graphics& g, const Palette& th, float cx, float cy, float r)
    {
        const float wr = r * 1.13f;
        g.setGradientFill ({ th.washer, cx - wr, cy - wr,
                             th.washer.darker(), cx + wr, cy + wr, false });
        g.fillEllipse (cx - wr, cy - wr, wr * 2.0f, wr * 2.0f);
        g.setColour (th.bevelLo.withAlpha (0.75f));
        g.drawEllipse (cx - wr, cy - wr, wr * 2.0f, wr * 2.0f, 0.8f);
        upperArc (g, cx, cy, wr - 0.6f, juce::MathConstants<float>::pi,
                  juce::MathConstants<float>::twoPi, th.spec.withAlpha (0.45f), 0.8f);
    }

    inline void body (juce::Graphics& g, const Palette& th, float cx, float cy, float r,
                      juce::Colour fill, bool material)
    {
        const float d = r * 2.0f;
        if (! material)
        {
            g.setGradientFill (juce::ColourGradient (th.bodyTop, cx - r, cy - r,
                                                     th.bodyBottom, cx + r, cy + r, false));
            g.fillEllipse (cx - r, cy - r, d, d);
            g.setColour (fill.withAlpha (0.5f));
            g.drawEllipse (cx - r, cy - r, d, d, 1.0f);
            return;
        }

        juce::ColourGradient dome (th.bevelHi, cx, cy - r, th.bevelLo, cx, cy + r, false);
        dome.addColour (0.16, th.bodyTop);
        dome.addColour (0.80, th.bodyBottom);
        g.setGradientFill (dome);
        g.fillEllipse (cx - r, cy - r, d, d);

        if (th.knobFinish == KnobFinish::Phenolic)
            // The cream print ring where the dome meets the skirt.
            upperArc (g, cx, cy, r - 0.9f, 0.35f, juce::MathConstants<float>::pi - 0.35f,
                      th.skirt.withAlpha (0.55f), 1.1f);
        else
        {
            g.setColour (th.skirt.withAlpha (0.85f));
            g.drawEllipse (cx - r, cy - r, d, d, 1.0f);
        }
    }

    /** Rotated with the value: the machined face and, for metal, the knurled rim. */
    inline void texture (juce::Graphics& g, const Palette& th, float cx, float cy,
                         float r, float angle)
    {
        const int d = static_cast<int> (std::round (r * 2.0f));
        if (d < 8)
            return;
        const juce::AffineTransform rot = juce::AffineTransform::rotation (angle, cx, cy);
        g.saveState();
        g.addTransform (rot);
        if (auto face = SkinTextures::knobFace (th, d); face.isValid())
            g.drawImageAt (face, cx - static_cast<float> (d) * 0.5f, cy - static_cast<float> (d) * 0.5f);
        if (auto ring = SkinTextures::knurl (th, d); ring.isValid())
            g.drawImageAt (ring, cx - static_cast<float> (d) * 0.5f, cy - static_cast<float> (d) * 0.5f);
        g.restoreState();
    }

    inline void bevel (juce::Graphics& g, const Palette& th, float cx, float cy, float r)
    {
        upperArc (g, cx, cy, r - 0.5f, juce::MathConstants<float>::pi,
                  juce::MathConstants<float>::twoPi, th.bevelHi.withAlpha (0.70f), 1.0f);
        upperArc (g, cx, cy, r - 0.5f, 0.0f, juce::MathConstants<float>::pi,
                  th.bevelLo.withAlpha (0.45f), 1.0f);
    }

    inline void spec (juce::Graphics& g, const Palette& th, float cx, float cy, float r)
    {
        // One bright crescent across the top-left, a faint bounce below.
        upperArc (g, cx, cy, r * 0.82f, -2.45f, -0.75f, th.spec.withAlpha (0.45f), 2.2f);
        upperArc (g, cx, cy, r * 0.82f, 0.55f, 1.65f, th.spec.withAlpha (0.16f), 1.2f);
    }

    inline void pointer (juce::Graphics& g, const Palette& th, float cx, float cy, float r, float angle)
    {
        const float cosA = std::cos (angle), sinA = std::sin (angle);
        const float x0 = cx + cosA * r * 0.55f, y0 = cy + sinA * r * 0.55f;
        const float x1 = cx + cosA * r * 0.90f, y1 = cy + sinA * r * 0.90f;
        const bool glowing = th.knobFinish == KnobFinish::Glowing;

        if (th.knobFinish == KnobFinish::MoldedPlastic)
        {
            // The original flat indicator: Dark must not gain a pixel of relief.
            g.setColour (th.text);
            g.drawLine (x0, y0, x1, y1, 2.5f);
        }
        else if (th.knobFinish == KnobFinish::Phenolic)
        {
            // Silk-printed white on black: the dark partner edge keeps it legible.
            g.setColour (th.bevelLo.withAlpha (0.85f));
            g.drawLine (x0 + 0.7f, y0 + 0.7f, x1 + 0.7f, y1 + 0.7f, 3.2f);
            g.setColour (th.pointer);
            g.drawLine (x0, y0, x1, y1, 2.2f);
        }
        else if (glowing)
        {
            g.setColour (th.pointer.withAlpha (0.35f));
            g.drawLine (x0 - 1.4f, y0 - 1.4f, x1 + 1.4f, y1 + 1.4f, 4.6f);
            g.setColour (th.pointer);
            g.drawLine (x0, y0, x1, y1, 2.2f);
        }
        else
        {
            // Cut into the metal: the groove is dark, its far edge catches light.
            g.setColour (th.bevelLo);
            g.drawLine (x0, y0, x1, y1, 2.2f);
            g.setColour (th.pointer.withAlpha (0.95f));
            g.drawLine (x0 - 0.7f, y0 - 0.7f, x1 - 0.7f, y1 - 0.7f, 1.1f);
        }
    }

    /** LED bloom at the end of the arc, for the console finish. */
    inline void arcLed (juce::Graphics& g, float cx, float cy, float trackR, float angle,
                        juce::Colour fill, bool glowing)
    {
        const float px = cx + std::cos (angle) * trackR, py = cy + std::sin (angle) * trackR;
        if (glowing)
        {
            g.setColour (fill.withAlpha (0.10f));
            g.fillEllipse (px - 7.0f, py - 7.0f, 14.0f, 14.0f);
            g.setColour (fill.withAlpha (0.22f));
            g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
        }
        g.setColour (fill);
        g.fillEllipse (px - 2.4f, py - 2.4f, 4.8f, 4.8f);
    }
}

class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    float uiScale = 1.0f;

    DarkLookAndFeel()
    {
        applyPalette();
    }

    /** The ids JUCE itself paints with (menus, labels, buttons) have to follow the
        skin too — the palette alone only covers what this project draws by hand. */
    void applyPalette()
    {
        const auto& th = Skins::current();
        setColour (juce::Slider::backgroundColourId, th.panel);
        setColour (juce::Slider::trackColourId, th.knobTrack);
        setColour (juce::Slider::rotarySliderFillColourId, th.accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, th.knobTrack);
        setColour (juce::Slider::thumbColourId, th.text);
        setColour (juce::Label::textColourId, th.text);
        setColour (juce::TextButton::buttonColourId, th.panel);
        setColour (juce::TextButton::textColourOffId, th.text);
        setColour (juce::TextButton::textColourOnId, th.accent);
        setColour (juce::ToggleButton::textColourId, th.text);
        setColour (juce::ComboBox::backgroundColourId, th.panel);
        setColour (juce::ComboBox::outlineColourId, th.panelOutline);
        setColour (juce::ComboBox::textColourId, th.text);
        setColour (juce::ComboBox::arrowColourId, th.accent);
        setColour (juce::PopupMenu::backgroundColourId, th.panel);
        setColour (juce::PopupMenu::textColourId, th.text);
        setColour (juce::PopupMenu::highlightedTextColourId, th.background);
    }

    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override
    {
        return juce::LookAndFeel_V4::getTextButtonFont (button, buttonHeight).withHeight (13.0f * uiScale);
    }

    /** V4 hard-codes a 3 px corner here, which reads as a different shape next to every
        other rectangle in the window. */
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        using namespace juce;
        const auto& th = Skins::current();
        const Rectangle<int> boxBounds (0, 0, width, height);

        g.setColour (box.findColour (ComboBox::backgroundColourId));
        g.fillRoundedRectangle (boxBounds.toFloat(), th.corner);

        g.setColour (box.findColour (ComboBox::outlineColourId));
        g.drawRoundedRectangle (boxBounds.toFloat().reduced (0.5f, 0.5f), th.corner, 1.0f);

        const Rectangle<int> arrowZone (width - 30, 0, 20, height);
        Path path;
        path.startNewSubPath (static_cast<float> (arrowZone.getX()) + 3.0f,
                              static_cast<float> (arrowZone.getCentreY()) - 2.0f);
        path.lineTo (static_cast<float> (arrowZone.getCentreX()),
                     static_cast<float> (arrowZone.getCentreY()) + 3.0f);
        path.lineTo (static_cast<float> (arrowZone.getRight()) - 3.0f,
                     static_cast<float> (arrowZone.getCentreY()) - 2.0f);

        g.setColour (box.findColour (ComboBox::arrowColourId)
                        .withAlpha (box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath (path, PathStrokeType (2.0f));
    }

    /** The horizontal mini faders of the FX strip: a flat track, the filled part
        in the knob's accent colour and a small square thumb — the same visual
        language the rotary uses, without any V4 grooving. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float /*min*/, float /*max*/,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace juce;
        const auto& th = Skins::current();
        const bool focused = slider.hasKeyboardFocus (true);
        const float radius = th.corner;

        const float fx = static_cast<float> (x) + 2.0f;
        const float fy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
        const float fw = static_cast<float> (width) - 4.0f;

        // The filled segment is drawn wherever the style reads left-to-right
        // (the FX row only ever uses that). Blur-free halo for keyboard focus,
        // same as the knobs get.
        const bool horizontal = style != Slider::LinearVertical
                                  && style != Slider::LinearBarVertical;

        if (focused)
        {
            g.setColour (slider.findColour (Slider::rotarySliderFillColourId).withAlpha (0.16f));
            g.fillRoundedRectangle (fx - 2.0f, fy - static_cast<float> (height) * 0.5f - 2.0f,
                                    fw + 4.0f, static_cast<float> (height) + 4.0f, radius);
        }

        g.setColour (slider.findColour (Slider::trackColourId));
        g.fillRoundedRectangle (fx, fy - 2.0f, fw, 4.0f, 2.0f);

        const bool horizontalFill = horizontal;
        if (horizontalFill)
        {
            const float px = jlimit (fx, fx + fw, sliderPos);
            if (px > fx + 0.5f)
            {
                g.setColour (slider.findColour (Slider::rotarySliderFillColourId));
                g.fillRoundedRectangle (fx, fy - 2.0f, px - fx, 4.0f, 2.0f);
            }
            g.setColour (slider.findColour (Slider::thumbColourId));
            g.fillRoundedRectangle (px - 3.0f, fy - 5.0f, 6.0f, 10.0f, 2.0f);
            g.setColour (th.panelOutline.withAlpha (0.6f));
            g.drawRoundedRectangle (px - 3.0f, fy - 5.0f, 6.0f, 10.0f, 2.0f, 1.0f);
        }
        else
        {
            const float py = jlimit (fy - static_cast<float> (height) * 0.5f,
                                     fy + static_cast<float> (height) * 0.5f, sliderPos);
            g.setColour (slider.findColour (Slider::rotarySliderFillColourId));
            g.fillRoundedRectangle (fx - 2.0f, py, 4.0f, fy + static_cast<float> (height) * 0.5f - py, 2.0f);
            g.setColour (slider.findColour (Slider::thumbColourId));
            g.fillRoundedRectangle (fx - 5.0f, py - 3.0f, 10.0f, 6.0f, 2.0f);
        }
    }

    /** V4 paints pentagon grooves by default; the flat track above leaves them out. */
    void drawLinearSliderBackground (juce::Graphics&, int, int, int, int,
                                     float, float, float,
                                     const juce::Slider::SliderStyle, juce::Slider&) override
    {
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        using namespace juce;
        const auto& th = Skins::current();
        const float cx = static_cast<float> (x) + static_cast<float> (width) * 0.5f;
        const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
        const float outer = jmin (static_cast<float> (width), static_cast<float> (height)) * 0.5f - 1.0f;
        const float trackR = outer - 6.0f;
        const float bodyR  = trackR - 3.5f;
        const float norm   = static_cast<float> (juce::jlimit (0.0, 1.0, static_cast<double> (sliderPos)));
        const float angle  = rotaryStartAngle + norm * (rotaryEndAngle - rotaryStartAngle) - MathConstants<float>::halfPi;
        const Colour fill = slider.findColour (Slider::rotarySliderFillColourId);
        const bool focused = slider.hasKeyboardFocus (true);

        // Focus halo, behind everything else: the knob is only ~44 px wide, so a
        // ring drawn at the rim would land on the tick marks (same language the
        // psykick editor uses for its focused knobs).
        if (focused)
        {
            g.setColour (fill.withAlpha (0.22f));
            g.fillEllipse (cx - outer, cy - outer, outer * 2.0f, outer * 2.0f);
            g.setColour (th.text.withAlpha (0.9f));
            g.drawEllipse (cx - outer, cy - outer, outer * 2.0f, outer * 2.0f, 1.5f);
        }

        // tick marks
        for (int i = 0; i <= 10; ++i)
        {
            const float t = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * static_cast<float> (i) / 10.0f
                            - MathConstants<float>::halfPi;
            const bool major = i == 0 || i == 5 || i == 10;
            const float r0 = trackR + 2.0f;
            const float r1 = major ? outer : outer - 2.0f;
            g.setColour (static_cast<float> (i) / 10.0f <= norm + 0.001f
                             ? fill.withAlpha (0.9f) : th.textDim.withAlpha (0.45f));
            g.drawLine (cx + std::cos (t) * r0, cy + std::sin (t) * r0,
                        cx + std::cos (t) * r1, cy + std::sin (t) * r1, major ? 1.4f : 1.0f);
        }

        // Track and value arcs. Path arcs measure angles from 12 o'clock,
        // so they take the raw slider angles, unlike the cos/sin maths below.
        const float sweep = rotaryEndAngle - rotaryStartAngle;
        Path track;
        track.addCentredArc (cx, cy, trackR, trackR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (th.knobTrack);
        g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

        if (norm > 0.002f)
        {
            Path arc;
            arc.addCentredArc (cx, cy, trackR, trackR, 0.0f,
                               rotaryStartAngle, rotaryStartAngle + norm * sweep, true);
            g.setColour (fill);
            g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // Layer order for every material skin: shadow, washer or collar, dome,
        // the rotated face and rim texture, bevel, specular, pointer. Dark takes
        // the flat path and skips straight to the dome it has always drawn.
        const bool material = th.knobFinish != KnobFinish::MoldedPlastic;
        // Below ~18 px across the detail turns to mush at any rib spacing, so a
        // heavily scaled-down window drops it and keeps the shape.
        const bool detail = material && bodyR >= 9.0f;

        if (material)
        {
            // A phenolic body sits on a nickel washer and a metal body in a
            // hex collar, so the foot that casts the shadow is wider than the
            // dome. Without that the washer swallows the contact shadow and
            // the knob floats.
            const float foot = th.knobFinish == KnobFinish::MoldedPlastic
                                 ? bodyR : bodyR * 1.22f;
            KnobPaint::shadow (g, th, cx, cy, foot);
            if (th.knobFinish == KnobFinish::Phenolic)
                KnobPaint::washer (g, th, cx, cy, bodyR);
            else if (th.knobFinish == KnobFinish::Glowing)
                KnobPaint::collar (g, th, cx, cy, bodyR);
        }

        KnobPaint::body (g, th, cx, cy, bodyR, fill, material);
        if (detail)
        {
            KnobPaint::texture (g, th, cx, cy, bodyR, angle);
            KnobPaint::bevel (g, th, cx, cy, bodyR);
            KnobPaint::spec (g, th, cx, cy, bodyR);
        }
        KnobPaint::pointer (g, th, cx, cy, bodyR, angle);
        KnobPaint::arcLed (g, cx, cy, trackR, angle, fill,
                           th.knobFinish == KnobFinish::Glowing);
    }
};
