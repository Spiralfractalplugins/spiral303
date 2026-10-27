#pragma once

#include "../core/CoreInclude.h"

#include "Patch.h"
#include "../PluginParameters.h"

/** Default MIDI-map hardware tables. The latch holds the last seen CC values
    (hardware knobs are absolute) and shades a patch copy through
    applyCcOverrides; the switch itself clears the latch so a stale hardware
    move cannot keep hijacking the parameters after a map change. */
namespace MidiMapTables
{
    enum MidiMap { MapOff = 0, MapLaunchControl = 1, MapLaunchControlXL = 2, MapPush2 = 3 };

    /** Target ids for the CC tables below. */
    enum MidiTarget
    {
        CcNone = 0, CcCutoff, CcRes, CcEnv, CcContour, CcWarmth, CcDrive, CcSatMix,
        CcAttack, CcAccent, CcGlide, CcSlope, CcGateMix, CcTargetCount
    };

    /** The controller number a target answers to in the given map. The
        built-in host CCs (74 cutoff / 71 resonance) work in every map. */
    inline MidiTarget targetForController (int map, int controller) noexcept
    {
        switch (controller)
        {
            case 74: return CcCutoff;
            case 71: return CcRes;
            default: break;
        }

        if (map == MapPush2)
        {
            // Push 2 User-mode macro encoders (CC71..78) carry the row left to
            // right; the numbers double as the GM sound-controller meanings
            // (74 cutoff, 71 resonance, 73 attack, 72 release), so the layout
            // reads naturally on MIDI-savvy hardware.
            switch (controller)
            {
                case 71: return CcRes;
                case 72: return CcContour;  // release-ish: decay length
                case 73: return CcAttack;
                case 74: return CcCutoff;
                case 75: return CcEnv;
                case 76: return CcDrive;
                case 77: return CcAccent;
                case 78: return CcSatMix;
                default: return CcNone;
            }
        }
        if (map == MapLaunchControlXL)
        {
            // XL factory: send A (13..20), send B (29..36), sliders (70..77 unused).
            switch (controller)
            {
                case 13: return CcCutoff;
                case 14: return CcRes;
                case 15: return CcEnv;
                case 16: return CcContour;
                case 17: return CcWarmth;
                case 18: return CcDrive;
                case 19: return CcSatMix;
                case 20: return CcAttack;   // send A right edge carries the attack
                case 29: return CcAccent;
                case 30: return CcGlide;
                case 31: return CcSlope;
                case 32: return CcGateMix;
                default: return CcNone;
            }
        }
        if (map == MapLaunchControl)
        {
            // Launch Control mini factory: top row (21..28), buttons (113..120 unused).
            switch (controller)
            {
                case 21: return CcCutoff;
                case 22: return CcRes;
                case 23: return CcEnv;
                case 24: return CcContour;
                case 25: return CcWarmth;
                case 26: return CcDrive;
                case 27: return CcSatMix;
                case 28: return CcAttack;
                default: return CcNone;
            }
        }
        return CcNone;
    }

    /** Latched CC values, message-free, engine-thread owned. */
    struct Latch
    {
        struct Slot { float value = 0.0f; bool seen = false; };
        Slot slots[CcTargetCount];
        int currentMap = MapOff;

        void reset() noexcept
        {
            for (auto& slot : slots)
                slot = {};
        }

        void setMap (int map) noexcept
        {
            if (currentMap == map)
                return;
            currentMap = map;
            reset();
        }

        void store (MidiTarget target, float normalizedValue) noexcept
        {
            if (target == CcNone)
                return;
            slots[static_cast<int> (target)].value = normalizedValue;
            slots[static_cast<int> (target)].seen = true;
        }
    };

    /** Writes the latched CC values into a patch copy. The expressions are kept
        letter-for-letter from the original CC path: the renders are
        bit-exact-sensitive. */
    inline void applyCcOverrides (const Latch& latch, Patch& patch)
    {
        const auto at = [&latch] (MidiTarget t) -> const Latch::Slot&
        {
            return latch.slots[static_cast<int> (t)];
        };

        if (at (CcCutoff).seen)
            patch.cutoff = patch.cutoff * (0.1f + 1.9f * at (CcCutoff).value);
        if (at (CcRes).seen)
            patch.res = juce::jlimit (0.0f, 1.0f, at (CcRes).value);
        if (at (CcEnv).seen)
            patch.env = std::max (Patch::kEnvFloor, at (CcEnv).value);
        if (at (CcContour).seen)
            patch.contour = ParamCurves::contourFromNormalized (at (CcContour).value);
        if (at (CcWarmth).seen)
            patch.warmth = at (CcWarmth).value;
        if (at (CcDrive).seen)
            patch.drive = at (CcDrive).value;
        if (at (CcSatMix).seen)
            patch.satMix = at (CcSatMix).value;
        if (at (CcAttack).seen)
            patch.attackSeconds = ParamCurves::attackToSeconds (at (CcAttack).value);
        if (at (CcAccent).seen)
            patch.accent = at (CcAccent).value;
        if (at (CcGlide).seen)
            patch.glide = at (CcGlide).value;
        if (at (CcSlope).seen)
            patch.tone = at (CcSlope).value;
        if (at (CcGateMix).seen)
            patch.gateMix = at (CcGateMix).value;
    }
}
