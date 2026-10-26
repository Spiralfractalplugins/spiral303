#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <JuceHeader.h>

#include "Palette.h"

/** Cached material plates: brushed aluminium, walnut, composite, plus the knob
    face, knurling, screws and LEDs that go on top of them. Everything is
    generated from the palette's fixed seed and never from the clock, so a given
    skin renders identically every run and the GUI tests can assert on pixels.
    Message-thread only: the cache has no lock because painting is where these
    are asked for. */
namespace SkinTextures
{
    namespace ids
    {
        inline constexpr int kFace = 0, kPanel = 1, kKnobFace = 2, kKnurl = 3,
                             kScrew = 4, kLedOn = 5, kLedOff = 6;
    }

    struct Key
    {
        int skin = 0, kind = 0, a = 0, b = 0;
        juce::uint32 version = 0;

        bool operator== (const Key& o) const noexcept
        {
            return skin == o.skin && kind == o.kind && a == o.a && b == o.b
                   && version == o.version;
        }
    };

    struct KeyHash
    {
        std::size_t operator() (const Key& k) const noexcept
        {
            juce::uint32 h = 0x811c9dc5u;
            auto mix = [&h] (juce::uint32 v) noexcept
            {
                h ^= v;
                h *= 0x01000193u;
            };
            mix (static_cast<juce::uint32> (k.skin));
            mix (static_cast<juce::uint32> (k.kind));
            mix (static_cast<juce::uint32> (k.a));
            mix (static_cast<juce::uint32> (k.b));
            mix (k.version);
            return static_cast<std::size_t> (h);
        }
    };

    inline juce::uint32& version()
    {
        static juce::uint32 v = 1;
        return v;
    }

    inline std::unordered_map<Key, juce::Image, KeyHash>& cache()
    {
        static std::unordered_map<Key, juce::Image, KeyHash> entries;
        return entries;
    }

    /** Called whenever the palette changes. The version rides in the key, so a
        stale image can never be handed back even if a reference outlives the
        clear. */
    inline void invalidate()
    {
        ++version();
        cache().clear();
    }

    inline juce::Random seeded (const Palette& th, int kind, int a, int b)
    {
        juce::uint32 h = th.grainSeed ^ 0x9e3779b9u;
        auto mix = [&h] (juce::uint32 v) noexcept
        {
            h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
            h *= 0xc2b2ae35u;
            h ^= h >> 15;
        };
        mix (static_cast<juce::uint32> (Skins::indexOf (th)));
        mix (static_cast<juce::uint32> (kind));
        mix (static_cast<juce::uint32> (a));
        mix (static_cast<juce::uint32> (b));
        mix (version());
        return juce::Random (static_cast<int> (h & 0x7fffffff));
    }

    inline int bucket (int v, int step)
    {
        return ((v + step - 1) / step) * step;
    }

    template <typename Generator>
    inline juce::Image getOrBuild (const Palette& th, int kind, int a, int b, Generator&& gen)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

        const Key key { Skins::indexOf (th), kind, a, b, version() };
        auto& entries = cache();
        if (auto it = entries.find (key); it != entries.end())
            return it->second;

        if (entries.size() > 96)
            entries.clear();

        juce::Random rng = seeded (th, kind, a, b);
        const juce::Image img = gen (th, a, b, rng);
        entries.emplace (key, img);
        return img;
    }

    //==========================================================================
    // Generators

    /** A surface shades around its own fill: a cream aluminium pocket on a
        walnut face must not pick up the wood's browns. */
    struct Tone
    {
        juce::Colour base, hi, lo;
    };

    inline Tone faceTone (const Palette& th)
    {
        return { th.faceBase, th.faceHi, th.faceLo };
    }

    inline Tone panelTone (const Palette& th)
    {
        return { th.panel, th.panel.brighter (0.22f), th.panel.darker (0.30f) };
    }

    /** The turned blank itself. Wood skins have a walnut face behind a black
        phenolic knob, so its machining marks must not borrow the wood's tans. */
    inline Tone knobTone (const Palette& th)
    {
        return { th.bodyTop, th.bevelHi, th.bevelLo };
    }

    inline void shadeVertical (juce::Graphics& g, const Tone& tone, float w, float h, float strength)
    {
        const float mid = h * 0.45f;
        g.setGradientFill ({ tone.hi.withAlpha (0.30f * strength), 0.0f, 0.0f,
                             tone.hi.withAlpha (0.0f), 0.0f, mid, false });
        g.fillRect (0.0f, 0.0f, w, mid);
        g.setGradientFill ({ tone.lo.withAlpha (0.0f), 0.0f, mid,
                             tone.lo.withAlpha (0.38f * strength), 0.0f, h, false });
        g.fillRect (0.0f, mid, w, h - mid);
    }

    /** Machined aluminium: a broad light-to-shade, per-row brush strokes, one
        big soft sheen and the dark lip of the sheet's edge. */
    inline void brushed (juce::Graphics& g, const Palette& th, const Tone& tone, int w, int h,
                         juce::Random& rng, float strength)
    {
        g.fillAll (tone.base);
        shadeVertical (g, tone, static_cast<float> (w), static_cast<float> (h), strength);

        const float fw = static_cast<float> (w), fh = static_cast<float> (h);
        for (int y = 0; y < h; ++y)
        {
            const float r = rng.nextFloat();
            const bool heavy = (y % 7) == 0;
            g.setColour ((r < 0.56f ? tone.hi : tone.lo)
                             .withAlpha ((heavy ? 0.085f : 0.030f) * strength * (0.45f + r)));
            g.drawLine (0.0f, y + 0.5f, fw, y + 0.5f, heavy ? 1.0f : 0.7f);
        }

        g.setGradientFill ({ th.spec.withAlpha (0.085f * strength), fw * 0.22f, fh * 0.10f,
                             th.spec.withAlpha (0.0f), fw * 0.78f, fh * 0.62f, true });
        g.fillRect (0.0f, 0.0f, fw, fh);

        g.setColour (tone.lo.withAlpha (0.55f * strength));
        g.drawLine (0.0f, fh - 0.5f, fw, fh - 0.5f, 1.0f);
        g.drawLine (fw - 0.5f, 0.0f, fw - 0.5f, fh, 1.0f);
        g.setColour (tone.hi.withAlpha (0.35f * strength));
        g.drawLine (0.0f, 0.5f, fw, 0.5f, 1.0f);
    }

    /** Walnut: the light wanders through long grain, two cathedral arcs cross
        the plate, pores speckle it and a varnish band lies over the top. */
    inline void wood (juce::Graphics& g, const Palette& th, const Tone& tone, int w, int h, juce::Random& rng)
    {
        const float fw = static_cast<float> (w), fh = static_cast<float> (h);
        g.fillAll (tone.base);
        shadeVertical (g, tone, fw, fh, 0.75f);

        const int lines = jmax (4, th.woodLines);
        for (int k = 0; k < lines; ++k)
        {
            const float base = (static_cast<float> (k) + 0.5f) * fh / static_cast<float> (lines)
                               + rng.nextFloat() * 8.0f - 4.0f;
            const float wander = 3.5f + rng.nextFloat() * 11.0f;
            juce::Path p;
            for (int x = 0; x <= w; x += 6)
            {
                const float fx = static_cast<float> (x);
                const float y = base
                                + std::sin (fx * 0.021f + static_cast<float> (k) * 1.73f) * 2.4f
                                + std::sin (fx * 0.0038f + static_cast<float> (k) * 0.7f) * wander;
                if (x == 0)
                    p.startNewSubPath (fx, y);
                else
                    p.lineTo (fx, y);
            }
            // A board never reads through hairlines alone: walnut alternates
            // wide bands of hard late wood and soft early wood, and the dark
            // line is only the boundary between them. So swing the tone first
            // over a wide path, then cut the boundary and its light ring.
            const float width = 1.1f + rng.nextFloat() * 1.2f;
            const float band = 6.0f + rng.nextFloat() * 10.0f;
            const bool late = rng.nextBool();
            g.setColour (late ? tone.lo.withAlpha (0.16f + 0.12f * rng.nextFloat())
                              : tone.hi.withAlpha (0.13f + 0.09f * rng.nextFloat()));
            g.strokePath (p, juce::PathStrokeType (band));
            g.setColour (th.grain.withAlpha (0.20f + 0.16f * rng.nextFloat()));
            g.strokePath (p, juce::PathStrokeType (width));
            g.saveState();
            g.addTransform (juce::AffineTransform::translation (0.0f, width + band * 0.30f));
            g.setColour (tone.hi.withAlpha (late ? 0.20f : 0.11f));
            g.strokePath (p, juce::PathStrokeType (1.1f));
            g.restoreState();
        }

        for (int c = 0; c < 2; ++c)
        {
            const float cy = fh * (0.32f + 0.36f * static_cast<float> (c));
            juce::Path arc;
            arc.startNewSubPath (fw * 0.04f, cy + fh * 0.16f);
            arc.quadraticTo (fw * 0.5f, cy - fh * (0.10f + 0.04f * static_cast<float> (c)),
                             fw * 0.96f, cy + fh * 0.16f);
            g.setColour (th.grain.withAlpha (0.05f + 0.03f * static_cast<float> (c)));
            g.strokePath (arc, juce::PathStrokeType (1.6f));
        }

        const int pores = jmax (60, w / 5);
        for (int i = 0; i < pores; ++i)
        {
            const float x = rng.nextFloat() * fw, y = rng.nextFloat() * fh;
            g.setColour (tone.lo.withAlpha (0.05f + 0.10f * rng.nextFloat()));
            g.fillRect (x, y, 1.0f + rng.nextFloat(), 1.0f);
        }

        g.setGradientFill ({ th.spec.withAlpha (0.055f), 0.0f, fh,
                             th.spec.withAlpha (0.0f), fw, 0.0f, false });
        g.fillRect (0.0f, 0.0f, fw, fh);
    }

    /** 45° warning tape inside a vertical band. */
    inline void hazardTape (juce::Graphics& g, const Palette& th, juce::Rectangle<float> r, float pitch)
    {
        g.saveState();
        juce::Path clip;
        clip.addRectangle (r);
        g.reduceClipRegion (clip);
        g.setColour (th.hazard.withAlpha (0.55f));
        g.fillRect (r);
        g.setColour (th.faceLo.withAlpha (0.80f));
        const float w = r.getWidth();
        for (float y = r.getY() - w; y < r.getBottom() + w; y += pitch * 2.0f)
        {
            juce::Path stripe;
            stripe.startNewSubPath (r.getX(), y);
            stripe.lineTo (r.getRight(), y + w);
            stripe.lineTo (r.getRight(), y + w + pitch);
            stripe.lineTo (r.getX(), y + pitch);
            stripe.closeSubPath();
            g.fillPath (stripe);
        }
        g.setColour (th.bevelHi.withAlpha (0.35f));
        g.drawVerticalLine (roundToInt (r.getRight() - 1.0f), r.getY(), r.getBottom());
        g.restoreState();
    }

    /** Cabin composite: matte plate, faint scanlines, corners sunk into shade. */
    inline void composite (juce::Graphics& g, const Palette& th, const Tone& tone, int w, int h, bool edges)
    {
        const float fw = static_cast<float> (w), fh = static_cast<float> (h);
        g.fillAll (tone.base);
        shadeVertical (g, tone, fw, fh, 0.6f);

        // Woven laminate: the tows sit proud of the resin, so the scanline pair
        // is the whole story and it has to survive being seen at 1:1.
        for (int y = 0; y < h; y += 3)
        {
            g.setColour (tone.hi.withAlpha (0.11f));
            g.drawLine (0.0f, y + 0.5f, fw, y + 0.5f, 1.0f);
            g.setColour (tone.lo.withAlpha (0.26f));
            g.drawLine (0.0f, y + 1.5f, fw, y + 1.5f, 1.0f);
        }

        const float r = jmax (fw, fh) * 0.75f;
        g.setGradientFill ({ th.spec.withAlpha (0.10f), fw * 0.5f, fh * 0.42f,
                             tone.lo.withAlpha (0.45f), fw * 0.5f + r, fh * 0.42f, true });
        g.fillRect (0.0f, 0.0f, fw, fh);

        if (edges)
        {
            // Narrower than this the stripes merge into a dirty edge; the tape
            // has to be readable as a warning, not as an artifact.
            const float tape = jmax (10.0f, fw * 0.012f);
            hazardTape (g, th, { 0.0f, 0.0f, tape, fh }, tape * 0.85f);
            hazardTape (g, th, { fw - tape, 0.0f, tape, fh }, tape * 0.85f);
        }
    }

    /** The rotating face of a knob, as a transparent overlay on the dome. */
    inline void knobFace (juce::Graphics& g, const Palette& th, const Tone& tone, int d, juce::Random& rng)
    {
        const float fd = static_cast<float> (d);
        const float cx = fd * 0.5f, cy = fd * 0.5f, r = fd * 0.5f - 1.0f;

        if (th.knobFinish == KnobFinish::KnurledMetal)
        {
            // Circular machining: concentric rings, alternating light and dark.
            for (float rr = r * 0.22f; rr < r * 0.96f; rr += 1.35f)
            {
                g.setColour ((rng.nextFloat() < 0.55f ? tone.hi : tone.lo)
                                 .withAlpha (0.05f + 0.06f * rng.nextFloat()));
                g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.8f);
            }
            g.setColour (th.spec.withAlpha (0.10f));
            g.drawEllipse (cx - r * 0.5f, cy - r * 0.5f, r, r, 0.7f);
        }
        else if (th.knobFinish == KnobFinish::Phenolic)
        {
            // Pressed bakelite: two mould seams and a little mottle.
            for (int i = 0; i < 2; ++i)
            {
                const float x = cx + (i == 0 ? -r * 0.42f : r * 0.42f);
                g.setColour (th.spec.withAlpha (0.07f));
                g.drawLine (x, cy - r * 0.85f, x, cy + r * 0.85f, 1.0f);
                g.setColour (th.bevelLo.withAlpha (0.12f));
                g.drawLine (x + 1.0f, cy - r * 0.85f, x + 1.0f, cy + r * 0.85f, 0.8f);
            }
            for (int i = 0; i < d; ++i)
            {
                const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                const float rr = r * std::sqrt (rng.nextFloat()) * 0.9f;
                g.setColour (th.spec.withAlpha (0.02f + 0.03f * rng.nextFloat()));
                g.fillRect (cx + std::cos (a) * rr, cy + std::sin (a) * rr, 1.0f, 1.0f);
            }
        }
        else if (th.knobFinish == KnobFinish::Glowing)
        {
            for (float y = -r; y < r; y += 2.0f)
            {
                g.setColour (th.grain.withAlpha (0.06f));
                g.drawLine (cx - r, cy + y, cx + r, cy + y, 0.8f);
            }
        }
    }

    /** Straight knurling on the knob's rim, as an annulus the caller rotates
        with the value. */
    inline void knurl (juce::Graphics& g, const Palette& th, const Tone& tone, int d, juce::Random& rng)
    {
        const float cx = static_cast<float> (d) * 0.5f;
        const float rOuter = cx - 0.6f;
        const float rInner = rOuter - jmax (3.0f, static_cast<float> (d) * 0.17f);
        // Ribs stay about 3 px apart: a 44 px knob gets a readable cut, the big
        // knobs get the full count the palette asks for.
        const int spacing = 3;
        const int fit = static_cast<int> (juce::MathConstants<float>::twoPi * rInner
                                          / static_cast<float> (spacing));
        const int n = jlimit (8, jmax (8, th.knurlCount), fit);
        const float pitch = juce::MathConstants<float>::twoPi / static_cast<float> (n);

        // A filed knurl is a crest and a void, alternating, not a diamond
        // lattice: the crossing second pass dissolves into dots at this size.
        for (int i = 0; i < n; ++i)
        {
            const float a = static_cast<float> (i) * pitch;
            const float cosA = std::cos (a), sinA = std::sin (a);
            g.setColour (th.spec.withAlpha (0.40f + 0.10f * rng.nextFloat()));
            g.drawLine (cx + cosA * rInner, cx + sinA * rInner,
                        cx + cosA * rOuter, cx + sinA * rOuter, 1.0f);
            const float b = a + pitch * 0.5f;
            const float cosB = std::cos (b), sinB = std::sin (b);
            g.setColour (tone.lo.withAlpha (0.50f + 0.12f * rng.nextFloat()));
            g.drawLine (cx + cosB * rInner, cx + sinB * rInner,
                        cx + cosB * rOuter, cx + sinB * rOuter, 1.5f);
        }

        g.setColour (th.bevelLo.withAlpha (0.40f));
        g.drawEllipse (cx - rInner, cx - rInner, rInner * 2.0f, rInner * 2.0f, 1.0f);
    }

    inline void screw (juce::Graphics& g, const Palette& th, int px, juce::Random& rng)
    {
        const float d = static_cast<float> (px);
        const float r = d * 0.5f - 0.5f;
        g.setGradientFill ({ th.screwHi, 0.0f, 0.0f, th.screw.darker(), d, d, false });
        g.fillEllipse (0.5f, 0.5f, r * 2.0f, r * 2.0f);
        g.setColour (th.bevelLo.withAlpha (0.75f));
        g.drawEllipse (0.5f, 0.5f, r * 2.0f, r * 2.0f, 0.8f);

        const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float dx = std::cos (a) * r * 0.62f, dy = std::sin (a) * r * 0.62f;
        g.setColour (th.bevelLo.withAlpha (0.85f));
        g.drawLine (d * 0.5f - dx, d * 0.5f - dy, d * 0.5f + dx, d * 0.5f + dy, 1.3f);
        g.setColour (th.spec.withAlpha (0.35f));
        g.drawLine (d * 0.5f - dx - 0.6f, d * 0.5f - dy - 0.6f,
                    d * 0.5f + dx - 0.6f, d * 0.5f + dy - 0.6f, 0.6f);
        g.setColour (th.spec.withAlpha (0.30f));
        g.drawEllipse (d * 0.5f - r * 0.7f, d * 0.5f - r * 0.7f, r * 0.9f, r * 0.9f, 0.7f);
    }

    /** Recessed LED in its bezel; lit adds the three-ring bloom that JUCE's
        blur-free drawing gets by stacking translucent ellipses. */
    inline void led (juce::Graphics& g, const Palette& th, int px, bool on, juce::Random&)
    {
        const float d = static_cast<float> (px);
        const float cx = d * 0.5f, cy = d * 0.5f, r = d * 0.5f - 1.0f;

        if (on)
        {
            g.setColour (th.ledOn.withAlpha (0.12f));
            g.fillEllipse (cx - r * 1.9f, cy - r * 1.9f, r * 3.8f, r * 3.8f);
            g.setColour (th.ledOn.withAlpha (0.30f));
            g.fillEllipse (cx - r * 1.45f, cy - r * 1.45f, r * 2.9f, r * 2.9f);
        }
        g.setColour (th.bevelLo.withAlpha (0.85f));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setGradientFill ({ on ? th.ledOn.brighter() : th.ledOff.darker(), cx - r * 0.4f, cy - r * 0.4f,
                             on ? th.ledOn : th.ledOff, cx, cy, true });
        g.fillEllipse (cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
        g.setColour (th.spec.withAlpha (on ? 0.75f : 0.25f));
        g.fillEllipse (cx - r * 0.34f, cy - r * 0.44f, r * 0.42f, r * 0.30f);
    }

    //==========================================================================
    // Public API. Flat materials return a null image, which callers skip.

    inline juce::Image face (const Palette& th, int w, int h)
    {
        if (w < 2 || h < 2 || th.faceMaterial == Material::Flat || ! th.faceTexture)
            return {};
        const int bw = bucket (w, 8), bh = bucket (h, 8);
        return getOrBuild (th, ids::kFace, bw, bh,
                           [&] (const Palette& p, int a, int b, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, b, true);
                               juce::Graphics g (img);
                               const Tone tone = faceTone (p);
                               if (p.faceMaterial == Material::Brushed)
                                   brushed (g, p, tone, a, b, r, 1.0f);
                               else if (p.faceMaterial == Material::Wood)
                                   wood (g, p, tone, a, b, r);
                               else
                                   composite (g, p, tone, a, b, true);
                               return img;
                           });
    }

    inline juce::Image panel (const Palette& th, int w, int h)
    {
        if (w < 2 || h < 2 || th.panelMaterial == Material::Flat)
            return {};
        const int bw = bucket (w, 8), bh = bucket (h, 8);
        return getOrBuild (th, ids::kPanel, bw, bh,
                           [&] (const Palette& p, int a, int b, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, b, true);
                               juce::Graphics g (img);
                               const Tone tone = panelTone (p);
                               if (p.panelMaterial == Material::Brushed)
                                   brushed (g, p, tone, a, b, r, 0.55f);
                               else if (p.panelMaterial == Material::Wood)
                                   wood (g, p, tone, a, b, r);
                               else
                                   composite (g, p, tone, a, b, false);
                               return img;
                           });
    }

    inline juce::Image knobFace (const Palette& th, int d)
    {
        if (d < 8 || th.knobFinish == KnobFinish::MoldedPlastic)
            return {};
        return getOrBuild (th, ids::kKnobFace, d, 0,
                           [&] (const Palette& p, int a, int, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, a, true);
                               juce::Graphics g (img);
                               const Tone tone = knobTone (p);
                               knobFace (g, p, tone, a, r);
                               return img;
                           });
    }

    inline juce::Image knurl (const Palette& th, int d)
    {
        if (d < 12 || th.knurlCount <= 0)
            return {};
        return getOrBuild (th, ids::kKnurl, d, 0,
                           [&] (const Palette& p, int a, int, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, a, true);
                               juce::Graphics g (img);
                               const Tone tone = knobTone (p);
                               knurl (g, p, tone, a, r);
                               return img;
                           });
    }

    inline juce::Image screw (const Palette& th, int px)
    {
        if (px < 4)
            return {};
        const int b = bucket (px, 2);
        return getOrBuild (th, ids::kScrew, b, 0,
                           [&] (const Palette& p, int a, int, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, a, true);
                               juce::Graphics g (img);
                               screw (g, p, a, r);
                               return img;
                           });
    }

    inline juce::Image ledBezel (const Palette& th, int px, bool on)
    {
        if (px < 4)
            return {};
        const int b = bucket (px, 2);
        return getOrBuild (th, on ? ids::kLedOn : ids::kLedOff, b, 0,
                           [&] (const Palette& p, int a, int, juce::Random& r) {
                               juce::Image img (juce::Image::ARGB, a, a, true);
                               juce::Graphics g (img);
                               led (g, p, a, on, r);
                               return img;
                           });
    }
}
