#pragma once

#include <array>
#include <cmath>

#include "../core/CoreInclude.h"

/** Multi-mode saturator with drive and parallel mix.

    Mode 0 is the original tanh soft clipper. The other modes are taken from
    the geo VST chain and reworked so that `drive = 0` is an exact
    pass-through while `drive = 1` pushes the stage hard. `mix` blends the wet
    signal in parallel, like the original stage.

    The Tape* modes implement a Jiles-Atherton magnetic hysteresis core
    (Chowdhury, DAFx 2019) followed by a first-order play-head loss lowpass
    and a DC blocker. `drive` scales the record-head magnetic field, and every
    tape variant keeps exact bypass at `drive = 0`. */
class Saturation
{
public:
    enum Type { Tanh = 0, Drive = 1, Triode = 2, Fold = 3, MasterClip = 4,
                Tape = 5, TapeWarm = 6, Cassette = 7, Reel = 8, TapeHot = 9,
                Clean = 10, Crunch = 11 };

    void prepare (double sampleRateHz)
    {
        const double srLocal = sampleRateHz > 1.0 ? sampleRateHz : 48000.0;
        sr = srLocal;
        streaming = true;
        driveSlewed = false;

        driveState = {};
        triodeState = {};
        foldState = {};
        tapeState = {};
        crunchState = {};
        driveNorm = 0.0f;
        makeupGain = 1.0f;
        driveTarget = driveNorm;
        mixCur = mix;
        driveSlewCoef = 1.0f - std::exp (-1.0 / (0.004 * srLocal));

        // One-pole DC blocker coefficients (the complement of a 20 Hz lowpass).
        triodeBlockCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 20.0f
                                         / static_cast<float> (sr));
        // Cathode-style follower timings.
        triodeAttackCoef = 1.0f - std::exp (-1.0 / (0.012 * sr));
        triodeReleaseCoef = 1.0f - std::exp (-1.0 / (0.26 * sr));

        driveBlockCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 20.0f
                                         / static_cast<float> (sr));
        // Crunch shares the same 20 Hz one-pole DC blocker timing.
        crunchBlockCoef = driveBlockCoef;

        srFold = sr;
        srTape = sr;

        refreshTapeCoefficients();
    }

    void reset() noexcept
    {
        driveState = {};
        triodeState = {};
        foldState = {};
        tapeState = {};
        crunchState = {};
    }

    void setType (Type t) noexcept
    {
        if (type != t)
        {
            const Type previous = type;
            type = t;
            // Apply the constants of the newly selected tape variant. For the
            // non-tape modes these calls are no-ops beyond storing nothing.
            applyTapeConstants (t);
            // Tape-to-tape switches keep the magnetisation state: zeroing it would
            // drop the output to silence and back, which is the click being faded.
            if (isTapeType (previous) != isTapeType (t))
                reset();
            // Recompute the sample-rate dependent coefficients for the new mode.
            refreshTapeCoefficients();
            refreshMakeup();
            // Depth values are per mode now (each mode applies its own onset
            // easing to the raw drive), so reinterpret the stored drive knob.
            driveNorm = driveTarget;
            applyDrive (driveNorm);
            mixCur = mix;
            // Crossfade the additive wet component into the new curve over a few
            // milliseconds; switching mid-note would otherwise jump and click.
            // The old component is frozen here: blending against a value that is
            // overwritten every sample would collapse the fade to a few samples.
            fadeFrom = lastB;
            fadeTotal = juce::jmax (1, static_cast<int> (0.008 * sr));
            fadeLeft = fadeTotal;
        }
    }

    void setDrive (float drive0to1)
    {
        // The knob's target: the audio path slews toward it over a few
        // milliseconds, so parameter automation does not step the level at
        // block boundaries (see process).
        driveTarget = juce::jlimit (0.0f, 1.0f, drive0to1);
        if (streaming)
            return;
        // Direct apply until the audio path takes over (as soon as process()
        // is invoked, every setDrive call only arms the slew target).
        driveNorm = driveCur = driveTarget;
        applyDrive (driveNorm);
        mixCur = mix;
    }

    void setMix (float m)
    {
        mix = juce::jlimit (0.0f, 1.0f, m);
        if (! streaming)
            mixCur = mix;
    }

    float process (float x)
    {
        // Slew the knob-sensitive scalars over a few milliseconds. Steady
        // state assigns the target exactly, so offline renders stay bit-exact;
        // only parameter movement is smoothed.
        if (driveCur != driveTarget)
        {
            driveCur += driveSlewCoef * (driveTarget - driveCur);
            if (std::abs (driveCur - driveTarget) < 1.0e-4f * (1.0f + driveTarget))
                driveCur = driveTarget;
            driveNorm = driveCur;
            applyDrive (driveCur);
        }
        driveSlewed = true;
        if (mixCur != mix)
        {
            if (std::abs (mixCur - mix) < 1.0e-3f)
                mixCur = mix;
            else
                mixCur += driveSlewCoef * (mix - mixCur);
        }

        float wet = x;

        switch (type)
        {
            case Clean:
                wet = x;
                break;

            case Crunch:
            {
                // Port of stm::SaturatorCrispy (ssModules, MIT): a hard input
                // clamp, a square-law even stage blended in, and a sign-
                // preserving power curve on top. The odd stage starts out
                // exactly linear (power = 1), so drive = 0 stays a pass-through
                // and the knob walks the power from there. The stageless curve
                // runs the small-signal gain up with the power, so everything
                // loudness seats itself through the makeup row.
                if (crunchIdle)
                {
                    wet = x;
                    break;
                }

                const float u = juce::jlimit (-1.0f, 1.0f, x);

                // Even stage: a quadratic path raises even harmonics.
                const float even = u * u;
                float y = even * crunchEvenMix + u * (1.0f - crunchEvenMix);

                // Odd stage: the same-shape power curve Crispy applies.
                const float ay = std::abs (y);
                const float bowed = 1.0f - std::pow (1.0f - ay, crunchPower);
                y = y >= 0.0f ? bowed : -bowed;

                crunchState.dc += crunchBlockCoef * (y - crunchState.dc);
                wet = juce::jlimit (-4.0f, 4.0f, y - crunchState.dc);
                break;
            }

            case Tanh:
                // Exact pass-through at drive 0: tanh(1x) is not literally x, and
                // feeding large outputs (e.g. a level-matched filter model) through
                // it would compress them non-linearly and break the calibration.
                wet = driveNorm <= 0.0f ? x : std::tanh (tanhDrive * x) * tanhMakeUp;
                break;

            case Drive:
            {
                if (driveK <= 0.0f)
                {
                    wet = x;
                    break;
                }

                float u = x;
                if (driveHysteresis > 0.0f)
                {
                    driveState.h += (0.04f + 0.85f * driveHysteresis) * (x - driveState.px);
                    const float band = driveHysteresis * std::abs (x);
                    driveState.h = juce::jlimit (-band, band, driveState.h) * (1.0f - 0.0004f);
                    u = x + driveState.h;
                }
                driveState.px = x;

                const float au = std::abs (u);
                const float y = u * (1.0f + driveK) / (1.0f + driveK * au);

                driveState.x += driveBlockCoef * (y - driveState.x);
                wet = y - driveState.x;
                break;
            }

            case Triode:
            {
                if (triodeIdle)
                {
                    wet = x;
                    break;
                }

                const double b = 0.08 + 1.4 * 0.35; // bias = 0.35
                const double g = 0.8 + 6.0 * 0.5;   // drive = 0.5
                const double v = b + g * static_cast<double> (x);
                const double i = v > 0.0 ? v * std::sqrt (v) : 0.0;
                const double i0 = b * std::sqrt (b);

                double y = (i - i0) / (1.5 * std::sqrt (b) * g);
                y = juce::jlimit (-1.0, 4.0, y);

                {
                    const double a = std::abs (y);
                    triodeState.env += (a > triodeState.env ? triodeAttackCoef : triodeReleaseCoef)
                                       * static_cast<float> (a - triodeState.env);
                    y *= 1.0 - 0.6 * 0.2 * static_cast<double> (triodeState.env);
                }

                triodeState.dc += triodeBlockCoef * (static_cast<float> (y) - triodeState.dc);
                wet = juce::jlimit (-4.0f, 4.0f, static_cast<float> (y) - triodeState.dc);
                wet = (1.0f - triodeDepth) * x + triodeDepth * wet;
                break;
            }

            case Fold:
            {
                if (foldIdle)
                {
                    wet = x;
                    break;
                }

                const float prev = foldState.was;
                foldState.was = x;

                float acc = 0.0f;
                for (int j = 1; j <= 4; ++j)
                {
                    const float u = prev + (x - prev) * (static_cast<float> (j) * 0.25f);
                    acc += fold (u * 2.5f);
                }
                const float y = acc * (0.25f * (1.0f / 0.25f) / 2.5f);
                const float folded = juce::jlimit (-4.0f, 4.0f, y);
                wet = (1.0f - foldDepth) * x + foldDepth * folded;
                break;
            }

            case MasterClip:
            {
                if (clipIdle)
                {
                    wet = x;
                    break;
                }

                const float d = clipDepth;
                const float knee = 0.8f - 0.5f * d;
                const float ceiling = 1.2f;
                const float a = std::abs (x);
                float y = x;
                if (a > knee)
                {
                    const float over = a - knee;
                    const float span = ceiling - knee;
                    const float yy = knee + span * over / (span + over);
                    y = x < 0.0f ? -yy : yy;
                }
                wet = (1.0f - d) * x + d * y;
                break;
            }

            case Tape:
            case TapeWarm:
            case Cassette:
            case Reel:
            case TapeHot:
            {
                if (tapeIdle)
                {
                    wet = x;
                    break;
                }

                const float T = tapeT;
                const float H = tapeGain * x + (x >= 0.0f ? tapeBias : -tapeBias * 0.85f);
                const float Hdelta = H - tapeState.Hprev;

                // RK4 integration of the Jiles-Atherton model, dM/dt.
                const float k1 = T * tapeDerivative (tapeState.Hprev, tapeState.M, tapeState.dHprev);
                const float k2 = T * tapeDerivative (0.5f * (tapeState.Hprev + H), tapeState.M + 0.5f * k1,
                                                  0.5f * (tapeState.dHprev + Hdelta / T));
                const float k3 = T * tapeDerivative (0.5f * (tapeState.Hprev + H), tapeState.M + 0.5f * k2,
                                                  0.5f * (tapeState.dHprev + Hdelta / T));
                const float k4 = T * tapeDerivative (H, tapeState.M + k3, Hdelta / T);

                float Mnew = tapeState.M + (k1 + 2.0f * k2 + 2.0f * k3 + k4) / 6.0f;
                // Guard against runaway magnetisation.
                Mnew = juce::jlimit (-1.0f, 1.0f, Mnew);

                const float hdot = safeDiv (Hdelta, T);
                tapeState.M = Mnew;
                tapeState.Hprev = H;
                tapeState.dHprev = hdot;

                // Play-head loss: first-order lowpass.
                const float lossCoef = tapeLossCoef;
                tapeState.loss += (1.0f - lossCoef) * (Mnew - tapeState.loss);

                // DC blocker (20 Hz) removes the operating-point shift.
                tapeState.dc += tapeBlockCoef * (tapeState.loss - tapeState.dc);
                wet = tapeState.loss - tapeState.dc;
                break;
            }
        }

        // Match the perceived loudness of the modes: each curve has its own
        // makeup gain, ramped linearly with drive so drive = 0 stays an exact
        // pass-through (gain is 1 there for every mode).
        wet *= makeupGain;
        // Blend as dry + additive wet, so a mode change can crossfade just the
        // added component instead of the whole signal.
        float b = (wet - x) * mixCur;
        if (fadeLeft > 0)
        {
            const float w = static_cast<float> (fadeLeft) / static_cast<float> (fadeTotal);
            b = w * fadeFrom + (1.0f - w) * b;
            --fadeLeft;
        }
        lastB = b;
        return x + b;
    }

    Type getType() const noexcept { return type; }

private:
    /** Recomputed per-mode depths from the (slewed) drive value; called from
        setDrive and from the per-sample slew in process. */
    void applyDrive (float d) noexcept
    {
        const float eff = onsetDepth (type, d);

        switch (type)
        {
            case Tanh:
                tanhDrive = 1.0f + 12.0f * d;
                tanhMakeUp = 1.0f / (0.5f + 0.5f * std::sqrt (tanhDrive));
                break;

            case Drive:
                driveK = d <= 0.0f ? 0.0f : eff * 12.0f;
                driveHysteresis = d <= 0.0f ? 0.0f : 0.35f;
                break;

            case Triode:
                triodeDepth = eff;
                triodeIdle = d <= 0.0f;
                break;

            case Fold:
                foldDepth = eff;
                foldIdle = d <= 0.0f;
                break;

            case MasterClip:
                clipDepth = eff;
                clipIdle = d <= 0.0f;
                break;

            case Tape:
            case TapeWarm:
            case Cassette:
            case Reel:
            case TapeHot:
                tapeIdle = d <= 0.0f;
                tapeGain = tapeGainBase * (0.5f + 0.5f * d) + tapeGainDrive * d;
                break;

            case Clean:
                break;

            case Crunch:
                crunchIdle = d <= 0.0f;
                crunchPower = std::pow (1.0f + 2.0f * d, 4.0f);
                crunchEvenMix = 0.45f * d;
                break;
        }

        refreshMakeup();
    }

    /** The knob's effective depth per mode. The blend modes (triode, fold,
        clip) run a curve at a fixed internal setting and only mix it in with a
        depth, so a linear depth makes the character jump during the very first
        part of the turn. An easing with a zero derivative at zero keeps the
        start of the sweep gentle while still reaching full depth at drive 1.
        Drive gets the same treatment: its hysteresis state wakes up immediately
        whenever the knob leaves zero, so the tone changed too early as well. */
    static float onsetDepth (Type t, float d) noexcept
    {
        if (t == Triode || t == Fold || t == MasterClip || t == Drive || t == Crunch)
            return d * d * (3.0f - 2.0f * d); // smoothstep: gentle start, full end
        return d;
    }

    static bool isTapeType (Type t) noexcept
    {
        return t == Tape || t == TapeWarm || t == Cassette || t == Reel || t == TapeHot;
    }

    /** Makeup gain per mode, sampled at drive knots 0, 0.125, 0.25, ... 1 (nine
        columns) and interpolated linearly. Calibrated by offline render (see
        tests): at every knot each mode lands within a few percent of the anchor
        loudness, and the anchor row itself softens the first part of the drive
        travel so turning the knob from zero does not jump the volume. The first
        column is 1.0 everywhere, which keeps the exact pass-through at drive 0,
        and Clean is unity across the whole row - it is a true bypass. */
    static float makeupAt (Type t, float d) noexcept
    {
        static constexpr float kTable[12][9] = {
            { 1.000f, 0.834f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f }, // Tanh (anchor)
            { 1.000f, 0.857f, 0.875f, 0.754f, 0.676f, 0.623f, 0.586f, 0.560f, 0.544f }, // Drive
            { 1.000f, 1.494f, 2.255f, 2.523f, 2.677f, 2.761f, 2.797f, 2.793f, 2.755f }, // Triode
            { 1.000f, 1.526f, 2.386f, 2.630f, 2.498f, 2.173f, 1.868f, 1.664f, 1.573f }, // Fold
            { 1.000f, 1.485f, 2.205f, 2.412f, 2.496f, 2.513f, 2.495f, 2.458f, 2.413f }, // Master clip
            { 1.000f, 0.298f, 0.419f, 0.443f, 0.448f, 0.443f, 0.433f, 0.419f, 0.408f }, // Tape
            { 1.000f, 0.335f, 0.458f, 0.477f, 0.477f, 0.468f, 0.456f, 0.442f, 0.429f }, // Tape warm
            { 1.000f, 0.238f, 0.352f, 0.383f, 0.395f, 0.397f, 0.393f, 0.386f, 0.379f }, // Cassette
            { 1.000f, 0.386f, 0.510f, 0.519f, 0.512f, 0.498f, 0.482f, 0.465f, 0.448f }, // Reel
            { 1.000f, 0.234f, 0.347f, 0.378f, 0.390f, 0.392f, 0.389f, 0.383f, 0.375f }, // Tape hot
            { 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f }, // Clean (bypass)
            { 1.000f, 0.757f, 0.711f, 0.610f, 0.509f, 0.470f, 0.435f, 0.415f, 0.397f }  // Crunch (Crispy port)
        };

        const float* row = kTable[juce::jlimit (0, 11, static_cast<int> (t))];
        const float x = juce::jlimit (0.0f, 1.0f, d) * 8.0f;
        const int i = juce::jmin (7, static_cast<int> (x));
        const float f = x - static_cast<float> (i);
        return row[i] + (row[i + 1] - row[i]) * f;
    }

    void refreshMakeup() noexcept
    {
        if (driveNorm <= 0.0f)
            makeupGain = 1.0f;
        else
            makeupGain = makeupAt (type, driveNorm);
    }

    float fold (float u) const noexcept
    {
        constexpr float pitch = 0.25f;
        const float t = u / (2.0f * pitch);
        const float k = std::floor (t);
        const float r = t - k;
        const float h = (r < 0.5f) ? 2.0f * r : 2.0f - 2.0f * r;
        const float sign = (std::fmod (k, 2.0f) == 0.0f) ? 1.0f : -1.0f;
        return pitch * h * sign;
    }

    static float safeDiv (float a, float b) noexcept
    {
        if (std::abs (b) < 1.0e-9f)
            return 0.0f;
        return a / b;
    }

    static float jilesLangevin (float z) noexcept
    {
        const float az = std::abs (z);
        if (az > 1.0e-4f)
        {
            const float coth = 1.0f / std::tanh (z);
            return coth - 1.0f / z;
        }
        return z / 3.0f;
    }

    static float jilesLangevinDerivative (float z) noexcept
    {
        const float az = std::abs (z);
        if (az > 1.0e-4f)
        {
            const float coth = 1.0f / std::tanh (z);
            return 1.0f / (z * z) - coth * coth + 1.0f;
        }
        return 1.0f / 3.0f;
    }

    float tapeDerivative (float H, float M, float Hdot) const noexcept
    {
        const float Q = (H + tapeAlpha * M) / tapeA;
        const float derivQ = jilesLangevinDerivative (Q);
        const float Man = tapeMs * jilesLangevin (Q);
        const float ManDer = tapeMsOverA * derivQ;

        const int deltaS = (H >= tapeState.Hprev) ? 1 : -1;
        const float diff = Man - M;
        const int deltaM = (deltaS > 0) == (diff > 0.0f) ? 1 : 0;

        const float den1 = tapeOneMinusC * static_cast<float> (deltaS) * tapeK
                          - tapeAlpha * diff;
        const float safeDen1 = (std::abs (den1) < 1.0e-9f)
                               ? (den1 >= 0.0f ? 1.0e-9f : -1.0e-9f) : den1;

        const float A = tapeOneMinusC * static_cast<float> (deltaM) * diff / safeDen1;
        const float denom = 1.0f - tapeCAlphaMsOverA * derivQ;
        const float safeDenom = (std::abs (denom) < 1.0e-9f)
                                ? (denom >= 0.0f ? 1.0e-9f : -1.0e-9f) : denom;

        return (A * Hdot + tapeC * ManDer * Hdot) / safeDenom;
    }

    void applyTapeConstants (Type t) noexcept
    {
        switch (t)
        {
            case Tape:
                tapeA = 0.044f; tapeK = 0.054f; tapeAlpha = 0.0016f; tapeC = 0.17f; tapeMs = 1.0f;
                tapeBias = 0.10f; tapeFc = 16000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 3.0f;
                break;
            case TapeWarm:
                tapeA = 0.050f; tapeK = 0.060f; tapeAlpha = 0.0016f; tapeC = 0.20f; tapeMs = 1.0f;
                tapeBias = 0.08f; tapeFc = 14000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 2.5f;
                break;
            case Cassette:
                tapeA = 0.035f; tapeK = 0.065f; tapeAlpha = 0.0016f; tapeC = 0.15f; tapeMs = 1.0f;
                tapeBias = 0.15f; tapeFc = 10000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 4.0f;
                break;
            case Reel:
                tapeA = 0.060f; tapeK = 0.040f; tapeAlpha = 0.0010f; tapeC = 0.22f; tapeMs = 1.0f;
                tapeBias = 0.05f; tapeFc = 18000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 2.5f;
                break;
            case TapeHot:
                tapeA = 0.030f; tapeK = 0.080f; tapeAlpha = 0.0020f; tapeC = 0.12f; tapeMs = 1.0f;
                tapeBias = 0.20f; tapeFc = 12000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 5.0f;
                break;
            default:
                // Non-tape modes do not use these.
                tapeA = 0.044f; tapeK = 0.054f; tapeAlpha = 0.0016f; tapeC = 0.17f; tapeMs = 1.0f;
                tapeBias = 0.10f; tapeFc = 16000.0f;
                tapeGainBase = 1.0f; tapeGainDrive = 3.0f;
                break;
        }
    }

    void refreshTapeCoefficients() noexcept
    {
        tapeBlockCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 20.0f
                                         / static_cast<float> (srTape));
        const float fc = juce::jlimit (1000.0f, static_cast<float> (srTape) * 0.45f, tapeFc);
        tapeLossCoef = std::exp (-2.0f * juce::MathConstants<float>::pi * fc
                                 / static_cast<float> (srTape));
        // Hoist per-sample constants once per mode/sample rate (bit-identical
        // expressions, only evaluated here instead of in the inner loop).
        tapeT = static_cast<float> (1.0 / srTape);
        tapeOneMinusC = 1.0f - tapeC;
        tapeMsOverA = tapeMs / tapeA;
        tapeCAlphaMsOverA = tapeC * tapeAlpha * tapeMsOverA;
    }

    Type type = Tanh;

    double sr = 48000.0;
    int fadeLeft = 0, fadeTotal = 1;
    float lastB = 0.0f, fadeFrom = 0.0f;

    float mix = 0.0f, mixCur = 0.0f;
    float driveNorm = 0.0f, makeupGain = 1.0f;
    float driveCur = 0.0f, driveTarget = 0.0f, driveSlewCoef = 0.0062f;
    bool driveSlewed = false, streaming = false;

    // Tanh
    float tanhDrive = 1.0f, tanhMakeUp = 1.0f;

    // Drive
    struct DriveState { float x = 0.0f, h = 0.0f, px = 0.0f; };
    DriveState driveState;
    float driveK = 0.0f, driveHysteresis = 0.0f, driveBlockCoef = 0.0028f;

    // Triode
    struct TriodeState { float dc = 0.0f, env = 0.0f; };
    TriodeState triodeState;
    float triodeDepth = 0.0f;
    bool triodeIdle = true;
    float triodeBlockCoef = 0.0028f, triodeAttackCoef = 0.007f, triodeReleaseCoef = 0.00034f;

    // Fold
    struct FoldState { float was = 0.0f; };
    FoldState foldState;
    float foldDepth = 0.0f;
    bool foldIdle = true;
    double srFold = 44100.0;

    // Master clip
    float clipDepth = 0.0f;
    bool clipIdle = true;

    // Tape (Jiles-Atherton)
    struct TapeState { float M = 0.0f, Hprev = 0.0f, dHprev = 0.0f, loss = 0.0f, dc = 0.0f; };
    TapeState tapeState;
    double srTape = 44100.0;
    bool tapeIdle = true;
    float tapeGain = 1.0f, tapeGainBase = 1.0f, tapeGainDrive = 3.0f;
    float tapeBias = 0.10f, tapeFc = 16000.0f;
    float tapeA = 0.044f, tapeK = 0.054f, tapeAlpha = 0.0016f, tapeC = 0.17f, tapeMs = 1.0f;
    float tapeLossCoef = 0.5f, tapeBlockCoef = 0.0028f;
    float tapeT = 0.0f, tapeOneMinusC = 0.83f, tapeMsOverA = 0.0f, tapeCAlphaMsOverA = 0.0f;

    // Crunch (port of stm::SaturatorCrispy)
    struct CrunchState { float dc = 0.0f; };
    CrunchState crunchState;
    float crunchBlockCoef = 0.0028f, crunchPower = 1.0f, crunchEvenMix = 0.0f;
    bool crunchIdle = true;
};
