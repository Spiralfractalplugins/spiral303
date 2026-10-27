#pragma once

#include <array>

#include <JuceHeader.h>

/** Which skin is drawn. The order matches the UiSkin parameter choices. */
enum class Face { Dark = 0, Console, Wood, Metal };
inline constexpr int kFaceCount = 4;

/** What the knob body is machined out of. Decides the whole rim drawing. */
enum class KnobFinish { MoldedPlastic, Phenolic, KnurledMetal, Glowing };

/** Which generator SkinTextures runs for a surface. Flat means the palette
    colour is the whole story and no texture image is asked for. */
enum class Material { Flat, Brushed, Wood, Composite };

/** Headings and body text that must follow the skin but are built once and
    cached (the manual wraps its lines into stored colours). A role is resolved
    against the live palette at paint time instead of being baked in. */
enum class InkRole { Text, TextDim, Accent, AccentAlt, Pitch, Gate };

/** One skin: every colour the interface paints with, plus the material knobs
    (how many ribs, how many grain lines, whether the face is textured at all).
    The palette is process-global rather than `findColour()`-based: the grid,
    gate and scope components are painted offscreen by the tests with no editor
    above them, and grain, knurling and shadows are not colours at all. */
struct Palette
{
    // --- Core colours, the names the old Theme constants had ---
    juce::Colour background, panel, panelOutline, text, textDim, accent, accentAlt;
    juce::Colour pitch, gate, gridLine, gridBeat, knobTrack, playhead;

    // --- Material ---
    juce::Colour faceBase;  // flat colour the face texture is worked over
    juce::Colour faceHi, faceLo;    // light and shade of the texture itself
    juce::Colour bevelHi, bevelLo;  // lit and shadowed edges of a raised or cut edge
    juce::Colour shadow;    // contact shadow under a knob
    juce::Colour spec;      // specular highlight
    juce::Colour bodyTop, bodyBottom;   // knob dome gradient
    juce::Colour skirt;     // phenolic: the ring where the dome meets the skirt
    juce::Colour washer;    // phenolic: nickel washer under the body
    juce::Colour pointer;   // the index line on the knob
    juce::Colour screw, screwHi;
    juce::Colour ink;       // printed or engraved legend
    // The wordmark and the credit are printed on the cabinet itself, above
    // and beside the panels, where no panel colour holds them up.
    juce::Colour faceInk;
    juce::Colour grain;     // wood grain / brushing strokes
    juce::Colour ledOn, ledOff, hazard;
    // The VU ramp is the one colour scale that means something rather than
    // decorating, so it is part of the skin only so far as each cabinet's
    // lamps differ.
    juce::Colour meterGreen, meterAmber, meterRed;
    // The little toggle keys (Gate ON, Analog, Hiss): a recessed chip whose
    // legend has to stay readable on every cabinet.
    juce::Colour chip, chipText;

    KnobFinish knobFinish = KnobFinish::MoldedPlastic;
    Material faceMaterial = Material::Flat;    // the plate the panels sit on
    Material panelMaterial = Material::Flat;   // the recessed pockets
    int knurlCount = 0;     // 0 draws no knurling
    int grainLines = 0;     // streaks across a panel
    int woodLines = 0;      // grain paths across the face
    int screwsPerPanel = 0; // 0, 2 or 4 corner screws
    juce::uint32 grainSeed = 0;
    bool faceTexture = false;    // paint a material texture instead of a flat fill
    bool engravedLabel = false;  // legend cut into metal: dark text + lit lower edge
    bool glowingLabel = false;   // legend back-lit: soft copies under the crisp text
    float corner = 6.0f;
};

namespace Skins
{
    /** Four tables. Dark reproduces the original Theme constants exactly, so the
        skin-0 renders and every existing pixel check stay where they were. */
    inline const std::array<Palette, kFaceCount>& all()
    {
        static const std::array<Palette, kFaceCount> palettes = []() -> std::array<Palette, kFaceCount>
        {
            Palette dark;
            dark.background   = juce::Colour (0xff181b20);
            dark.panel        = juce::Colour (0xff22262d);
            dark.panelOutline = juce::Colour (0xff333a44);
            dark.text         = juce::Colour (0xffc8d0dc);
            dark.textDim      = juce::Colour (0xff838d9a);
            dark.accent       = juce::Colour (0xffe8a03c);
            dark.accentAlt    = juce::Colour (0xff4ac8e8);
            dark.pitch        = juce::Colour (0xffb26de8);
            dark.gate         = juce::Colour (0xffe8564a);
            dark.gridLine     = juce::Colour (0xff2c313a);
            dark.gridBeat     = juce::Colour (0xff3d4451);
            dark.knobTrack    = juce::Colour (0xff3a4150);
            dark.playhead     = juce::Colour (0xfff2f5f9);

            // Never drawn: MoldedPlastic keeps the flat dome and no face texture.
            dark.faceBase     = juce::Colour (0xff181b20);
            dark.faceHi       = juce::Colour (0xff2a2f37);
            dark.faceLo       = juce::Colour (0xff0e1115);
            dark.bevelHi      = juce::Colour (0xff4a5260);
            dark.bevelLo      = juce::Colour (0xff0d1013);
            dark.shadow       = juce::Colour (0xff000000);
            dark.spec         = juce::Colour (0xffffffff);
            dark.bodyTop      = juce::Colour (0xff464e5c);   // the literal the old dome gradient used
            dark.bodyBottom   = juce::Colour (0xff1b1e24);
            dark.skirt        = juce::Colour (0xff1b1e24);
            dark.washer       = juce::Colour (0xff8b93a0);
            dark.pointer      = juce::Colour (0xffc8d0dc);
            dark.screw        = juce::Colour (0xff3a4150);
            dark.screwHi      = juce::Colour (0xff5b6472);
            dark.ink          = juce::Colour (0xff7a8492);
            dark.faceInk      = dark.text;
            dark.grain        = juce::Colour (0xff2c313a);
            dark.ledOn        = juce::Colour (0xffe8564a);
            dark.ledOff       = juce::Colour (0xff3a4150);
            dark.hazard       = juce::Colour (0xffe8a03c);
            dark.meterGreen   = juce::Colour (0xff56d384);
            dark.meterAmber   = juce::Colour (0xfff0c040);
            dark.meterRed     = juce::Colour (0xfff04040);
            dark.chip         = dark.background;
            dark.chipText     = dark.textDim;
            dark.knobFinish   = KnobFinish::MoldedPlastic;
            dark.corner       = 6.0f;

            Palette console;
            console.background   = juce::Colour (0xff0a0e16);
            console.panel        = juce::Colour (0xff141d2b);
            console.panelOutline = juce::Colour (0xff2b4560);
            console.text         = juce::Colour (0xffd9f2ff);
            console.textDim      = juce::Colour (0xff7fa0bd);
            console.accent       = juce::Colour (0xff38c8ff);   // cyan: the primary value arc
            console.accentAlt    = juce::Colour (0xff39f0c8);   // mint: the second channel
            console.pitch        = juce::Colour (0xffd24bf0);   // magenta marker
            console.gate         = juce::Colour (0xffff4d3a);   // alarm red
            console.gridLine     = juce::Colour (0xff18222f);
            console.gridBeat     = juce::Colour (0xff2a4056);
            console.knobTrack    = juce::Colour (0xff1b2a3d);
            console.playhead     = juce::Colour (0xfff2fbff);

            console.faceBase     = juce::Colour (0xff101722);
            console.faceHi       = juce::Colour (0xff1d2c40);
            console.faceLo       = juce::Colour (0xff060a11);
            console.bevelHi      = juce::Colour (0xff5f88ad);
            console.bevelLo      = juce::Colour (0xff030609);
            console.shadow       = juce::Colour (0xff000000);
            console.spec         = juce::Colour (0xffeaf7ff);
            console.bodyTop      = juce::Colour (0xff2b3b4e);
            console.bodyBottom   = juce::Colour (0xff0c141d);
            console.skirt        = juce::Colour (0xff16202b);
            console.washer       = juce::Colour (0xff38c8ff);
            console.pointer      = juce::Colour (0xff9ff0ff);
            console.screw        = juce::Colour (0xff27455f);
            console.screwHi      = juce::Colour (0xff63a2c8);
            console.ink          = juce::Colour (0xffbfe6ff);
            console.faceInk      = console.text;
            console.grain        = juce::Colour (0xff16222f);
            console.ledOn        = juce::Colour (0xff4dff9e);
            console.ledOff       = juce::Colour (0xff1c3242);
            console.hazard       = juce::Colour (0xffffc21f);
            console.meterGreen   = juce::Colour (0xff3dffb0);
            console.meterAmber   = juce::Colour (0xffffd24d);
            console.meterRed     = juce::Colour (0xffff4d3a);
            console.chip         = juce::Colour (0xff0b1420);
            console.chipText     = console.textDim;
            console.knobFinish   = KnobFinish::Glowing;
            console.faceMaterial = Material::Composite;
            console.panelMaterial = Material::Composite;
            // The console grip is a hex collar, not a knurled band: both at once
            // reads as noise at 44 px.
            console.knurlCount   = 0;
            console.grainLines   = 0;
            console.grainSeed    = 0xc0501e5u;
            console.faceTexture  = true;
            console.glowingLabel = true;
            console.corner       = 5.0f;

            Palette wood;
            // Walnut cheeks, aluminium insert, cream silk-printing, black phenolic.
            wood.background   = juce::Colour (0xff4a2c1a);
            wood.panel        = juce::Colour (0xffada393);
            wood.panelOutline = juce::Colour (0xff7a7161);
            wood.text         = juce::Colour (0xff241f16);
            wood.textDim      = juce::Colour (0xff413a2f);
            wood.accent       = juce::Colour (0xff632a0a);
            wood.accentAlt    = juce::Colour (0xff173f52);
            wood.pitch        = juce::Colour (0xff4a2674);
            wood.gate         = juce::Colour (0xffa11f14);
            wood.gridLine     = juce::Colour (0xff9d9382);
            wood.gridBeat     = juce::Colour (0xff7e7463);
            wood.knobTrack    = juce::Colour (0xff8c8271);
            wood.playhead     = juce::Colour (0xff1d1710);

            wood.faceBase     = juce::Colour (0xff5b3722);
            wood.faceHi       = juce::Colour (0xff7d5233);
            wood.faceLo       = juce::Colour (0xff2e1a0e);
            wood.bevelHi      = juce::Colour (0xffd8d0c0);
            wood.bevelLo      = juce::Colour (0xff453f34);
            wood.shadow       = juce::Colour (0xff20140a);
            wood.spec         = juce::Colour (0xfffff6e4);
            wood.bodyTop      = juce::Colour (0xff2a2622);
            wood.bodyBottom   = juce::Colour (0xff100e0c);
            wood.skirt        = juce::Colour (0xffe6dcc4);
            wood.washer       = juce::Colour (0xffc9c3b4);
            wood.pointer      = juce::Colour (0xfff2ead6);
            wood.screw        = juce::Colour (0xff8b8272);
            wood.screwHi      = juce::Colour (0xffd5cdba);
            wood.ink          = juce::Colour (0xff2b2519);
            wood.faceInk      = juce::Colour (0xfff0e4c8);   // the cream of a silkscreen on walnut
            wood.grain        = juce::Colour (0xff3a2012);
            wood.ledOn        = juce::Colour (0xffb3230f);
            wood.ledOff       = juce::Colour (0xff8c8271);
            wood.hazard       = juce::Colour (0xffd6a512);
            wood.meterGreen   = juce::Colour (0xff6fbf7a);
            wood.meterAmber   = juce::Colour (0xffe0a83c);
            wood.meterRed     = juce::Colour (0xffe06a4a);
            wood.chip         = juce::Colour (0xffc9bda6);
            wood.chipText     = wood.textDim;
            wood.knobFinish   = KnobFinish::Phenolic;
            wood.faceMaterial = Material::Wood;      // walnut cheeks and frame
            wood.panelMaterial = Material::Brushed;  // anodised aluminium insert
            wood.grainLines   = 26;
            wood.woodLines    = 26;
            wood.grainSeed    = 0x5e3b21au;
            wood.faceTexture  = true;
            wood.engravedLabel = true;
            wood.corner       = 4.0f;

            Palette metal;
            // One anodised plate everywhere, dark pockets, engraved legends, screws.
            metal.background   = juce::Colour (0xff3b4147);
            metal.panel        = juce::Colour (0xff242a2f);
            metal.panelOutline = juce::Colour (0xff5d666e);
            metal.text         = juce::Colour (0xffe2e9f0);
            metal.textDim      = juce::Colour (0xff9fa9b2);
            metal.accent       = juce::Colour (0xffdca05a);
            metal.accentAlt    = juce::Colour (0xff63c6e0);
            metal.pitch        = juce::Colour (0xffa084d2);
            metal.gate         = juce::Colour (0xffd2564a);
            metal.gridLine     = juce::Colour (0xff2c3339);
            metal.gridBeat     = juce::Colour (0xff3d464e);
            metal.knobTrack    = juce::Colour (0xff171c20);
            metal.playhead     = juce::Colour (0xfff6f9fc);

            metal.faceBase     = juce::Colour (0xff474e55);
            metal.faceHi       = juce::Colour (0xff79828b);
            metal.faceLo       = juce::Colour (0xff23282c);
            metal.bevelHi      = juce::Colour (0xffaeb8c0);
            metal.bevelLo      = juce::Colour (0xff12161a);
            metal.shadow       = juce::Colour (0xff0c0f12);
            metal.spec         = juce::Colour (0xfffdfeff);
            metal.bodyTop      = juce::Colour (0xff838c95);
            metal.bodyBottom   = juce::Colour (0xff2e343a);
            metal.skirt        = juce::Colour (0xff454d55);
            metal.washer       = juce::Colour (0xffb9c2ca);
            metal.pointer      = juce::Colour (0xfff3f6f8);
            metal.screw        = juce::Colour (0xff6d767f);
            metal.screwHi      = juce::Colour (0xffc4ccd3);
            metal.ink          = juce::Colour (0xffe6ebf0);
            metal.faceInk      = metal.text;
            metal.grain        = juce::Colour (0xff8d979f);
            metal.ledOn        = juce::Colour (0xff5be07a);
            metal.ledOff       = juce::Colour (0xff39414a);
            metal.hazard       = juce::Colour (0xffd8b41f);
            metal.meterGreen   = juce::Colour (0xff5be07a);
            metal.meterAmber   = juce::Colour (0xffe8b84a);
            metal.meterRed     = juce::Colour (0xffe87050);
            metal.chip         = juce::Colour (0xff171c20);
            metal.chipText     = metal.textDim;
            metal.knobFinish   = KnobFinish::KnurledMetal;
            metal.faceMaterial = Material::Brushed;
            metal.panelMaterial = Material::Brushed;
            metal.knurlCount   = 32;
            metal.grainLines   = 34;
            metal.screwsPerPanel = 4;
            metal.grainSeed    = 0x46d5a17u;
            metal.faceTexture  = true;
            metal.engravedLabel = true;
            metal.corner       = 4.0f;

            return { dark, console, wood, metal };
        }();
        return palettes;
    }

    inline int& slot()
    {
        static int index = 0;
        return index;
    }

    inline int currentIndex() { return slot(); }

    inline const Palette& current() { return all()[static_cast<size_t> (slot())]; }

    /** Which table this reference points into — the texture cache keys on it.
        Only valid for a palette taken out of all() or current(). */
    inline int indexOf (const Palette& p) { return static_cast<int> (&p - &all().front()); }

    /** Message thread only: the editor's timer calls this, and everything that
        paints reads the table without a lock. */
    inline void select (int index)
    {
        slot() = juce::jlimit (0, kFaceCount - 1, index);
    }

    inline const juce::StringArray& names()
    {
        static const juce::StringArray labels { "Dark", "Console", "Wood", "Metal" };
        return labels;
    }

    inline const juce::Colour& ink (const Palette& p, InkRole role)
    {
        switch (role)
        {
            case InkRole::Text:      return p.text;
            case InkRole::TextDim:   return p.textDim;
            case InkRole::Accent:    return p.accent;
            case InkRole::AccentAlt: return p.accentAlt;
            case InkRole::Pitch:     return p.pitch;
            case InkRole::Gate:      return p.gate;
        }
        return p.text;
    }
}
