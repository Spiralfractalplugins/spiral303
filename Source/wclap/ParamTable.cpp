#include "ParamTable.h"

#include <cstring>

namespace spiral303
{
    namespace
    {
        const char* const kClockChoices[] = { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T" };
        const char* const kSyncChoices[] = { "Host", "Manual", "Midi Clock" };
        const char* const kGateDivChoices[] = { "16th", "8th", "32nd", "16T" };
        const char* const kSatTypeChoices[] =
        {
            "Tanh", "Drive (Rational)", "Triode (Tube)", "Fold (Wavefolder)", "Master Clip",
            "Tape", "Tape Warm", "Cassette", "Reel (Hi-Fi)", "Tape (Hot)", "Clean", "Crunch"
        };
        const char* const kAnalogModeChoices[] = { "Standard", "Moog Thick", "Virus Hyper" };
        const char* const kFilterTypeChoices[] =
        {
            "Classic (SVF)", "Diode Ladder (303)", "Moog Ladder",
            "TPT SVF", "Sallen-Key", "One-Pole", "Vowel", "Diode 303 (Asym)"
        };
        const char* const kMidiMapChoices[] = { "Off", "Launch Control", "Launch Control XL", "Push 2" };
        const char* const kSkinChoices[] = { "Dark", "Console", "Wood", "Metal" };
        const char* const kStartModeChoices[] = { "Transport", "Note Trigger" };
        const char* const kDelayDivChoices[] = { "1/4", "1/8", "1/8.", "1/16", "1/8T" };
        const char* const kOversampleChoices[] = { "OS Off", "OS 2x", "OS 4x" };

        // The order is the native parameter list, so that a host which shows
        // the parameters in order shows them the same way in both builds.
        const ParamDef kParams[kParamCount] =
        {
            { "Cutoff",       "Cutoff",             " Hz", 40.0,    8000.0, 900.0, false, false, 0, nullptr, 0 },
            { "Wave",         "Wave",               "",     0.0,    1.0,    0.5,   false, false, 2, nullptr, 0 },
            { "Res",          "Resonance",          "",     0.0,    1.0,    0.62,  false, false, 2, nullptr, 0 },
            { "EnvAmt",       "Env Amount",         "",     0.0,    1.0,    0.55,  false, false, 2, nullptr, 0 },
            { "Contour",      "Contour",            "",     0.05,   1.5,    0.5,   false, false, 2, nullptr, 0 },
            { "Accent",       "Accent",             "",     0.0,    1.0,    0.75,  false, false, 2, nullptr, 0 },
            { "Glide",        "Glide",              "",     0.0,    1.0,    0.3,   false, false, 2, nullptr, 0 },
            { "Drive",        "Drive",              "",     0.0,    1.0,    0.15,  false, false, 2, nullptr, 0 },
            { "DistMix",      "Dist Mix",           "",     0.0,    1.0,    0.5,   false, false, 2, nullptr, 0 },
            { "Slope",        "Slope",              "",     0.0,    1.0,    1.0,   false, false, 2, nullptr, 0 },
            { "Warmth",       "Warmth",             "",     0.0,    1.0,    0.4,   false, false, 2, nullptr, 0 },
            { "Swing",        "Swing",              "",     0.0,    0.6,    0.0,   false, false, 2, nullptr, 0 },
            { "BPM",          "Tempo",              " BPM", 20.0,   400.0,  130.0, false, false, 1, nullptr, 0 },
            { "ClockDiv",     "Clock",              "",     0.0,    5.0,    4.0,   true,  false, 0, kClockChoices, 6 },
            { "Sync",         "Sync",               "",     0.0,    2.0,    0.0,   true,  false, 0, kSyncChoices, 3 },
            { "GateDiv",      "Gate Div",           "",     0.0,    3.0,    0.0,   true,  false, 0, kGateDivChoices, 4 },
            { "GateOn",       "Gate On",            "",     0.0,    1.0,    0.0,   false, true,  0, nullptr, 0 },
            { "Play",         "Manual Play",        "",     0.0,    1.0,    1.0,   false, true,  0, nullptr, 0 },
            { "GateMix",      "Gate Mix",           "",     0.0,    1.0,    1.0,   false, false, 2, nullptr, 0 },
            { "GateAtk",      "Gate Atk",           " ms",  0.2,    50.0,   2.0,   false, false, 1, nullptr, 0 },
            { "GateRel",      "Gate Rel",           " ms",  0.5,    200.0,  14.0,  false, false, 1, nullptr, 0 },
            { "Gain",         "Gain",               " dB",  -24.0,  12.0,   0.0,   false, false, 1, nullptr, 0 },
            { "SatType",      "Saturation Type",    "",     0.0,    11.0,   0.0,   true,  false, 0, kSatTypeChoices, 12 },
            { "AnalogOn",     "Analog On",          "",     0.0,    1.0,    1.0,   false, true,  0, nullptr, 0 },
            { "AnalogMode",   "Analog Mode",        "",     0.0,    2.0,    0.0,   true,  false, 0, kAnalogModeChoices, 3 },
            { "AnalogHiss",   "Analog Hiss",        "",     0.0,    1.0,    0.0,   false, true,  0, nullptr, 0 },
            { "FilterType",   "Filter Type",        "",     0.0,    7.0,    0.0,   true,  false, 0, kFilterTypeChoices, 8 },
            { "SeqOn",        "Seq On",             "",     0.0,    1.0,    1.0,   false, true,  0, nullptr, 0 },
            { "LimOn",        "Limiter On",         "",     0.0,    1.0,    0.0,   false, true,  0, nullptr, 0 },
            { "LimThr",       "Limiter Threshold",  " dB",  -20.0,  0.0,    -1.0,  false, false, 1, nullptr, 0 },
            { "LimCeil",      "Limiter Ceiling",    " dB",  -6.0,   0.0,    -0.2,  false, false, 1, nullptr, 0 },
            { "Attack",       "Attack",             "",     0.0,    1.0,    0.0,   false, false, 2, nullptr, 0 },
            { "MidiMap",      "MIDI Map",           "",     0.0,    3.0,    0.0,   true,  false, 0, kMidiMapChoices, 4 },
            // The FX tail, before the skin selector so the skin keeps its spot
            // as the last parameter of the list.
            { "FxDelMix",     "Delay Mix",          "",     0.0,    1.0,    0.0,   false, false, 2, nullptr, 0 },
            { "FxDelDiv",     "Delay Div",          "",     0.0,    4.0,    2.0,   true,  false, 0, kDelayDivChoices, 5 },
            { "FxDelFb",      "Delay Fb",           "",     0.0,    0.9,    0.35,  false, false, 2, nullptr, 0 },
            { "FxDelTone",    "Delay Tone",         "",     0.0,    1.0,    0.65,  false, false, 2, nullptr, 0 },
            { "FxRevMix",     "Reverb Mix",         "",     0.0,    1.0,    0.0,   false, false, 2, nullptr, 0 },
            { "FxRevDecay",   "Reverb Decay",       "",     0.0,    1.0,    0.5,   false, false, 2, nullptr, 0 },
            { "Oversample",   "Oversample",         "",     0.0,    2.0,    0.0,   true,  false, 0, kOversampleChoices, 3 },
            { "UiSkin",       "Skin",               "",     0.0,    3.0,    0.0,   true,  false, 0, kSkinChoices, 4 },
            { "Start",        "Start Mode",         "",     0.0,    1.0,    0.0,   true,  false, 0, kStartModeChoices, 2 }
        };

        uint32_t hashId (const char* text)
        {
            // FNV-1a: the same text always gives the same number, on every
            // platform and every build.
            uint32_t hash = 2166136261u;
            for (const char* p = text; *p != 0; ++p)
            {
                hash ^= static_cast<uint32_t> (static_cast<unsigned char> (*p));
                hash *= 16777619u;
            }
            return hash;
        }
    }

    int paramCount()
    {
        return kParamCount;
    }

    const ParamDef& paramDef (int index)
    {
        return kParams[index >= 0 && index < kParamCount ? index : 0];
    }

    int paramIndexForId (const char* id)
    {
        if (id == nullptr)
            return -1;

        for (int i = 0; i < kParamCount; ++i)
            if (std::strcmp (kParams[i].id, id) == 0)
                return i;

        return -1;
    }

    uint32_t paramId (int index)
    {
        return hashId (paramDef (index).id);
    }

    int paramIndexForHostId (uint32_t id)
    {
        for (int i = 0; i < kParamCount; ++i)
            if (paramId (i) == id)
                return i;

        return -1;
    }
}
