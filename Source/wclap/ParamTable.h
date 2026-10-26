#pragma once

#include <cstdint>

// The parameter table of the WebAssembly build.
//
// It mirrors the native parameter list exactly: the same order, the same ids,
// the same ranges and the same defaults. The ids are what the state file
// carries, so a session written by one build loads in the other.

namespace spiral303
{
    enum ParamIndex
    {
        kCutoff = 0,
        kWave,
        kRes,
        kEnv,
        kContour,
        kAccent,
        kGlide,
        kDrive,
        kSatMix,
        kTone,
        kWarmth,
        kSwing,
        kBpm,
        kClockDiv,
        kSyncMode,
        kGateDiv,
        kGateOn,
        kManualPlay,
        kGateMix,
        kGateAtk,
        kGateRel,
        kMasterGain,
        kSatType,
        kAnalogOn,
        kAnalogMode,
        kAnalogHiss,
        kFilterType,
        kSeqOn,
        kLimiterOn,
        kLimiterThreshold,
        kLimiterCeiling,
        kAttack,
        kMidiMap,
        kFxDelMix,
        kFxDelDiv,
        kFxDelFb,
        kFxDelTone,
        kFxRevMix,
        kFxRevDecay,
        kOversample,
        kUiSkin,
        kParamCount
    };

    struct ParamDef
    {
        const char* id;            // the id the state file uses, same as the native build
        const char* name;          // what the host shows
        const char* unit;          // " Hz", " dB" or empty
        double minValue;
        double maxValue;
        double defaultValue;
        bool isChoice;
        bool isBoolean;
        int decimals;              // how many digits the text form shows
        const char* const* choices;
        int numChoices;
    };

    int paramCount();
    const ParamDef& paramDef (int index);

    /** The index of a parameter by its id, or -1. */
    int paramIndexForId (const char* id);

    /** The numeric id the host sees. A hash of the text id, so that inserting a
        parameter in the middle does not renumber the ones after it. */
    uint32_t paramId (int index);

    /** The index of a parameter by the host's numeric id, or -1. */
    int paramIndexForHostId (uint32_t id);
}
