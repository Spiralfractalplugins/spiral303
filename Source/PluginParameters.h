#pragma once

#include <cmath>

#include "core/CoreInclude.h"

namespace ParamIDs
{
    // Plain text rather than juce::String: the WebAssembly build has no JUCE,
    // and a const char* converts to juce::String / juce::Identifier implicitly,
    // so the native build is unaffected.
    inline constexpr const char* cutoff    { "Cutoff" };
    inline constexpr const char* wave      { "Wave" };
    inline constexpr const char* res       { "Res" };
    inline constexpr const char* env       { "EnvAmt" };
    inline constexpr const char* contour   { "Contour" };
    inline constexpr const char* accent    { "Accent" };
    inline constexpr const char* glide     { "Glide" };
    inline constexpr const char* drive     { "Drive" };
    inline constexpr const char* satMix    { "DistMix" };
    inline constexpr const char* tone      { "Slope" };
    inline constexpr const char* warmth    { "Warmth" };
    inline constexpr const char* clockDiv  { "ClockDiv" };
    inline constexpr const char* swing     { "Swing" };
    inline constexpr const char* bpm       { "BPM" };
    inline constexpr const char* syncMode  { "Sync" };
    inline constexpr const char* manualPlay{ "Play" };
    inline constexpr const char* gateOn    { "GateOn" };
    inline constexpr const char* gateDiv   { "GateDiv" };
    inline constexpr const char* gateAtk   { "GateAtk" };
    inline constexpr const char* gateRel   { "GateRel" };
    inline constexpr const char* masterGain{ "Gain" };
    // Appended last on purpose: AUv2 hands the host index-based addresses, so inserting
    // new parameters in the middle would renumber everything after them.
    inline constexpr const char* gateMix     { "GateMix" };
    inline constexpr const char* satType     { "SatType" };
    inline constexpr const char* analogOn    { "AnalogOn" };
    inline constexpr const char* analogMode  { "AnalogMode" };
    inline constexpr const char* analogHiss  { "AnalogHiss" };
    // Appended after all existing parameters; changing the order would renumber
    // host-facing AUv2 indices.
    inline constexpr const char* filterType   { "FilterType" };
    inline constexpr const char* seqOn        { "SeqOn" };
    inline constexpr const char* limiterOn    { "LimOn" };
    inline constexpr const char* limiterThreshold { "LimThr" };
    inline constexpr const char* limiterCeiling  { "LimCeil" };
    inline constexpr const char* attack          { "Attack" };
    inline constexpr const char* midiMap         { "MidiMap" };
    // FX tail: appended before the skin selector, so the skin keeps its spot as
    // the last parameter of the list (AUv2 index stability).
    inline constexpr const char* fxDelMix     { "FxDelMix" };
    inline constexpr const char* fxDelDiv     { "FxDelDiv" };
    inline constexpr const char* fxDelFb      { "FxDelFb" };
    inline constexpr const char* fxDelTone    { "FxDelTone" };
    inline constexpr const char* fxRevMix     { "FxRevMix" };
    inline constexpr const char* fxRevDecay   { "FxRevDecay" };
    inline constexpr const char* oversample   { "Oversample" };
    // Still appended last, for the same AUv2 reason: this one only chooses how
    // the interface is drawn and the audio thread never reads it.
    inline constexpr const char* uiSkin          { "UiSkin" };
}

namespace CustomKeys
{
    /** State property names for non-automatable custom state. */
    inline constexpr const char* sequence  { "sequence" };
    inline constexpr const char* gate      { "gate" };
    inline constexpr const char* presetNote { "presetNote" };
}

/** Shared parameter curves: the single definition of every knob mapping that
    more than one call site needs (the processor's patch build and the engine's
    MIDI-map CC overrides). Formulas are bit-exact-hot, so call sites must keep
    evaluating through these functions and never copy the arithmetic. */
struct ParamCurves
{
    /** 0 = the instant legacy attack, 1 = ~1.2 s: the square shape makes
        higher values grow disproportionately, like a knob that reads
        in seconds. */
    static double attackToSeconds (float attackKnob) noexcept
    {
        return attackKnob <= 0.0f ? 0.0 : 0.02 + 1.2 * attackKnob * attackKnob;
    }

    /** The contour knob spans 0.05..1.5 (a fraction of step length); a MIDI CC
        covers the same span from its own 0..1 range. The expression is kept
        letter-for-letter from the original CC path: the DSP renders are
        bit-exact-sensitive. */
    static float contourFromNormalized (float normalized) noexcept
    {
        return juce::jlimit (0.05f, 1.5f, 0.05f + 1.45f * normalized);
    }

    /** The delay tone knob is a log sweep 500..8000 Hz; 0.65 lands on the
        ~3 kHz warmth the analog echo is known for. Both processors run this
        same expression, so a preset sounds the same in either build. */
    static double delayToneToHz (float toneKnob) noexcept
    {
        const double t = jlimit (0.0f, 1.0f, toneKnob);
        return 500.0 * std::exp (std::log (8000.0 / 500.0) * t);
    }

    /** The beat multipliers of the Delay Div choices ("1/4", "1/8", "1/8.",
        "1/16", "1/8T"), one shared table next to the dividers of the clock. */
    static double delayDivBeats (int index) noexcept
    {
        static const double table[5] = { 1.0, 0.5, 0.75, 0.25, 1.0 / 3.0 };
        return table[jlimit (0, 4, index)];
    }
};
