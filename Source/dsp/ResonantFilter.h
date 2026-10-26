#pragma once

#include <algorithm>
#include <cmath>

/** Virtual-analog lowpass family. The interface stays stable no matter which model
    is selected, so the 303 voice, the host parameters and the tests all keep
    working; only `setModel()` swaps the maths inside. */
class ResonantFilter
{
public:
    enum class Model { Classic = 0, DiodeLadder, MoogLadder, TptSvf, SallenKey, OnePole, Vowel, Diode303 };

    /** Vowel: the formant bank below. Cutoff walks the voice through a vowel
        trajectory, resonance sets the formant bandwidths. */
    void setMorph (float) noexcept { }

    ResonantFilter() = default;

    void prepare (double sampleRateHz) { resetAll(); sampleRate = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz; gainSlew = 1.0f - std::exp (-1.0 / (0.002 * sampleRate)); }

    void setModel (Model m)
    {
        if (m != model)
        {
            model = m;
            resetAll();
        }
    }

    void setModel (int index)
    {
        index = std::max (0, std::min (7, index));
        setModel (static_cast<Model> (index));
    }

    Model getModel() const { return model; }

    void setResonance (float q0to1) noexcept { resonance = std::min (1.0f, std::max (0.0f, q0to1)); }
    void setSlope (float s) noexcept { slope = std::min (1.0f, std::max (0.0f, s)); }

    void reset() { resetAll(); }

    float process (float in, float cutoffHz) noexcept
    {
        cutoffHz = std::max (30.0f, cutoffHz);
        const float f = std::min (0.45f, std::max (1.0e-5f, cutoffHz / static_cast<float> (sampleRate)));

        // Loudness calibration: scale this model toward the Classic anchor so
        // switching models does not jump the volume. Applied at the output (the
        // calibration ratio was measured there); ramped over ~2 ms, and once it
        // settles the applied gain equals the target exactly, so renders stay
        // bit-exact in the steady state.
        const float target = modelGainAt (model, cutoffHz, resonance);
        if (gainApplied != target)
        {
            gainApplied += gainSlew * (target - gainApplied);
            if (std::abs (gainApplied - target) < 1.0e-4f * (1.0f + std::abs (target)))
                gainApplied = target;
        }

        float out;
        switch (model)
        {
            case Model::Classic:     out = processClassic (in, cutoffHz, f); break;
            case Model::DiodeLadder: out = processDiode (in, f); break;
            case Model::MoogLadder:  out = processMoog (in, f); break;
            case Model::TptSvf:      out = processTpt (in, f); break;
            case Model::SallenKey:   out = processSallen (in, f); break;
            case Model::OnePole:     out = processOnePole (in, f); break;
            case Model::Vowel:       out = processVowel (in, cutoffHz); break;
            case Model::Diode303:   out = processDiode303 (in, f); break;
            default:                 out = processClassic (in, cutoffHz, f); break;
        }
        // Level contract for the fitted gain: leveled models never exceed the
        // same ±4 envelope the models themselves clamp to (the anchor never
        // enters this branch), so level matching cannot push the chain past the
        // output headroom the master gain expects.
        return std::min (4.0f, std::max (-4.0f, out * gainApplied));
    }

    static float section (float in, float f, float q, float& lp, float& bp, float& hp)
    {
        const float F = std::tanh (pi * f);
        const float damp = 1.0f / std::max (0.51f, q);

        hp = (in - lp) * (1.0f / (1.0f + damp * F));
        bp = bp + damp * F * (hp - bp);
        bp = std::min (4.0f, std::max (-4.0f, bp));
        lp = lp + F * bp;
        lp = std::min (4.0f, std::max (-4.0f, lp));
        return lp;
    }

    /** Loudness of one model relative to the Classic anchor: RMS ratio measured
        by full offline renders (see the filter loudness test) over a dense
        log-spaced cutoff grid and five resonance knots, bilinearly interpolated.
        The field has fine structure in the resonant pockets (the anchor ridge
        passes over the saw's harmonics), so the grid has to be dense enough for
        the interpolation to mostly ride the teeth. The anchor row is all ones,
        so it stays untouched. */
    static float modelGainAt (Model m, float cutoffHz, float res) noexcept
    {
        static constexpr float kCuts[28] = { 200.0f, 230.0f, 264.5f, 304.2f, 349.8f, 402.3f,
            462.6f, 532.0f, 611.8f, 703.6f, 809.1f, 930.5f, 1070.1f, 1230.6f, 1415.1f,
            1627.4f, 1871.5f, 2152.3f, 2475.1f, 2846.4f, 3273.3f, 3764.3f, 4328.9f, 4978.3f,
            5725.0f, 6583.8f, 7571.4f, 8707.1f };
        static constexpr float kTable[7][28][5] = {
            {   // Classic (anchor) - level-matching reference
                { 1, 1, 1, 1, 1 },  // 200.0 Hz
                { 1, 1, 1, 1, 1 },  // 230.0 Hz
                { 1, 1, 1, 1, 1 },  // 264.5 Hz
                { 1, 1, 1, 1, 1 },  // 304.2 Hz
                { 1, 1, 1, 1, 1 },  // 349.8 Hz
                { 1, 1, 1, 1, 1 },  // 402.3 Hz
                { 1, 1, 1, 1, 1 },  // 462.6 Hz
                { 1, 1, 1, 1, 1 },  // 532.0 Hz
                { 1, 1, 1, 1, 1 },  // 611.8 Hz
                { 1, 1, 1, 1, 1 },  // 703.6 Hz
                { 1, 1, 1, 1, 1 },  // 809.1 Hz
                { 1, 1, 1, 1, 1 },  // 930.5 Hz
                { 1, 1, 1, 1, 1 },  // 1070.1 Hz
                { 1, 1, 1, 1, 1 },  // 1230.6 Hz
                { 1, 1, 1, 1, 1 },  // 1415.1 Hz
                { 1, 1, 1, 1, 1 },  // 1627.4 Hz
                { 1, 1, 1, 1, 1 },  // 1871.5 Hz
                { 1, 1, 1, 1, 1 },  // 2152.3 Hz
                { 1, 1, 1, 1, 1 },  // 2475.1 Hz
                { 1, 1, 1, 1, 1 },  // 2846.4 Hz
                { 1, 1, 1, 1, 1 },  // 3273.3 Hz
                { 1, 1, 1, 1, 1 },  // 3764.3 Hz
                { 1, 1, 1, 1, 1 },  // 4328.9 Hz
                { 1, 1, 1, 1, 1 },  // 4978.3 Hz
                { 1, 1, 1, 1, 1 },  // 5725.0 Hz
                { 1, 1, 1, 1, 1 },  // 6583.8 Hz
                { 1, 1, 1, 1, 1 },  // 7571.4 Hz
                { 1, 1, 1, 1, 1 },  // 8707.1 Hz
            },
            {   // Diode ladder
                { 49.085f, 76.078f, 36.442f, 8.428f, 2.528f },  // 200.0 Hz
                { 32.797f, 50.304f, 38.975f, 11.263f, 3.098f },  // 230.0 Hz
                { 22.218f, 31.553f, 39.399f, 15.206f, 4.082f },  // 264.5 Hz
                { 15.342f, 20.155f, 37.622f, 17.630f, 5.940f },  // 304.2 Hz
                { 10.857f, 13.525f, 27.635f, 17.359f, 8.798f },  // 349.8 Hz
                { 7.902f, 9.392f, 15.579f, 17.527f, 9.856f },  // 402.3 Hz
                { 5.940f, 6.664f, 8.945f, 18.498f, 9.166f },  // 462.6 Hz
                { 4.619f, 4.920f, 6.028f, 11.969f, 10.147f },  // 532.0 Hz
                { 3.718f, 3.850f, 4.983f, 6.696f, 14.621f },  // 611.8 Hz
                { 3.094f, 3.207f, 4.422f, 5.099f, 13.066f },  // 703.6 Hz
                { 2.654f, 2.845f, 4.017f, 5.708f, 9.982f },  // 809.1 Hz
                { 2.339f, 2.659f, 4.104f, 7.556f, 8.095f },  // 930.5 Hz
                { 2.108f, 2.563f, 4.195f, 6.861f, 7.207f },  // 1070.1 Hz
                { 1.935f, 2.491f, 4.012f, 5.669f, 10.464f },  // 1230.6 Hz
                { 1.802f, 2.415f, 3.882f, 6.250f, 9.716f },  // 1415.1 Hz
                { 1.698f, 2.344f, 3.781f, 5.957f, 7.410f },  // 1627.4 Hz
                { 1.613f, 2.281f, 3.653f, 5.760f, 9.027f },  // 1871.5 Hz
                { 1.543f, 2.224f, 3.531f, 5.571f, 7.962f },  // 2152.3 Hz
                { 1.485f, 2.171f, 3.421f, 5.403f, 7.993f },  // 2475.1 Hz
                { 1.436f, 2.124f, 3.332f, 5.214f, 7.697f },  // 2846.4 Hz
                { 1.394f, 2.082f, 3.260f, 5.075f, 7.666f },  // 3273.3 Hz
                { 1.358f, 2.046f, 3.196f, 4.958f, 7.362f },  // 3764.3 Hz
                { 1.327f, 2.016f, 3.139f, 4.854f, 7.142f },  // 4328.9 Hz
                { 1.302f, 1.990f, 3.086f, 4.735f, 6.960f },  // 4978.3 Hz
                { 1.280f, 1.967f, 3.031f, 4.622f, 6.821f },  // 5725.0 Hz
                { 1.261f, 1.948f, 2.980f, 4.526f, 6.662f },  // 6583.8 Hz
                { 1.245f, 1.933f, 2.934f, 4.437f, 6.502f },  // 7571.4 Hz
                { 1.239f, 1.928f, 2.917f, 4.403f, 6.445f },  // 8707.1 Hz
            },
            {   // Moog ladder
                { 1.998f, 0.276f, 0.136f, 0.032f, 0.009f },  // 200.0 Hz
                { 1.660f, 0.285f, 0.304f, 0.065f, 0.018f },  // 230.0 Hz
                { 1.395f, 1.226f, 2.714f, 0.140f, 0.038f },  // 264.5 Hz
                { 1.182f, 1.484f, 0.521f, 0.252f, 0.086f },  // 304.2 Hz
                { 1.003f, 1.387f, 0.572f, 0.379f, 0.198f },  // 349.8 Hz
                { 0.844f, 0.262f, 0.479f, 0.577f, 0.344f },  // 402.3 Hz
                { 0.693f, 0.262f, 0.398f, 0.902f, 0.474f },  // 462.6 Hz
                { 0.545f, 0.258f, 0.367f, 0.799f, 0.723f },  // 532.0 Hz
                { 0.414f, 0.259f, 0.385f, 0.562f, 1.277f },  // 611.8 Hz
                { 0.344f, 0.259f, 0.398f, 0.455f, 1.017f },  // 703.6 Hz
                { 0.294f, 0.257f, 0.368f, 0.454f, 0.651f },  // 809.1 Hz
                { 0.281f, 0.257f, 0.359f, 0.552f, 0.518f },  // 930.5 Hz
                { 0.273f, 0.254f, 0.358f, 0.514f, 0.526f },  // 1070.1 Hz
                { 0.271f, 0.252f, 0.348f, 0.444f, 0.732f },  // 1230.6 Hz
                { 0.270f, 0.251f, 0.342f, 0.467f, 0.631f },  // 1415.1 Hz
                { 0.270f, 0.249f, 0.332f, 0.446f, 0.513f },  // 1627.4 Hz
                { 0.270f, 0.248f, 0.323f, 0.432f, 0.588f },  // 1871.5 Hz
                { 0.270f, 0.246f, 0.316f, 0.417f, 0.530f },  // 2152.3 Hz
                { 0.270f, 0.246f, 0.309f, 0.404f, 0.522f },  // 2475.1 Hz
                { 0.271f, 0.245f, 0.303f, 0.390f, 0.493f },  // 2846.4 Hz
                { 0.272f, 0.244f, 0.298f, 0.377f, 0.485f },  // 3273.3 Hz
                { 0.273f, 0.243f, 0.293f, 0.367f, 0.461f },  // 3764.3 Hz
                { 0.274f, 1.181f, 2.464f, 0.358f, 0.444f },  // 4328.9 Hz
                { 0.274f, 1.376f, 2.716f, 0.350f, 0.430f },  // 4978.3 Hz
                { 0.275f, 1.482f, 2.810f, 4.411f, 0.419f },  // 5725.0 Hz
                { 0.275f, 1.549f, 2.852f, 4.518f, 6.632f },  // 6583.8 Hz
                { 0.276f, 1.596f, 2.871f, 4.546f, 6.756f },  // 7571.4 Hz
                { 0.276f, 1.611f, 2.876f, 4.537f, 6.738f },  // 8707.1 Hz
            },
            {   // TPT SVF
                { 1.436f, 0.929f, 0.318f, 0.067f, 0.019f },  // 200.0 Hz
                { 1.369f, 1.021f, 0.636f, 0.173f, 0.045f },  // 230.0 Hz
                { 1.309f, 0.998f, 0.890f, 0.296f, 0.072f },  // 264.5 Hz
                { 1.259f, 0.950f, 0.997f, 0.290f, 0.072f },  // 304.2 Hz
                { 1.217f, 0.948f, 1.433f, 0.814f, 0.390f },  // 349.8 Hz
                { 1.183f, 0.964f, 1.291f, 1.348f, 0.727f },  // 402.3 Hz
                { 1.155f, 0.964f, 0.970f, 1.552f, 0.650f },  // 462.6 Hz
                { 1.133f, 0.966f, 1.021f, 1.952f, 1.613f },  // 532.0 Hz
                { 1.113f, 0.974f, 1.055f, 1.162f, 2.127f },  // 611.8 Hz
                { 1.095f, 0.985f, 1.154f, 1.112f, 2.171f },  // 703.6 Hz
                { 1.079f, 0.994f, 1.103f, 1.152f, 1.475f },  // 809.1 Hz
                { 1.065f, 0.999f, 1.111f, 1.429f, 1.168f },  // 930.5 Hz
                { 1.054f, 1.001f, 1.140f, 1.396f, 1.251f },  // 1070.1 Hz
                { 1.044f, 1.000f, 1.137f, 1.257f, 1.842f },  // 1230.6 Hz
                { 1.035f, 1.000f, 1.144f, 1.376f, 1.671f },  // 1415.1 Hz
                { 1.028f, 1.000f, 1.140f, 1.360f, 1.418f },  // 1627.4 Hz
                { 1.022f, 1.002f, 1.136f, 1.365f, 1.697f },  // 1871.5 Hz
                { 1.017f, 1.003f, 1.131f, 1.355f, 1.586f },  // 2152.3 Hz
                { 1.013f, 1.005f, 1.127f, 1.353f, 1.614f },  // 2475.1 Hz
                { 1.009f, 1.007f, 1.125f, 1.335f, 1.570f },  // 2846.4 Hz
                { 1.006f, 1.008f, 1.121f, 1.318f, 1.580f },  // 3273.3 Hz
                { 1.003f, 1.007f, 1.114f, 1.300f, 1.526f },  // 3764.3 Hz
                { 1.001f, 1.006f, 1.105f, 1.282f, 1.488f },  // 4328.9 Hz
                { 0.999f, 1.003f, 1.094f, 1.260f, 1.455f },  // 4978.3 Hz
                { 0.997f, 1.001f, 1.083f, 1.235f, 1.424f },  // 5725.0 Hz
                { 0.996f, 0.999f, 1.071f, 1.213f, 1.390f },  // 6583.8 Hz
                { 0.995f, 0.998f, 1.059f, 1.193f, 1.356f },  // 7571.4 Hz
                { 0.995f, 0.998f, 1.055f, 1.184f, 1.343f },  // 8707.1 Hz
            },
            {   // Sallen-Key
                { 2.319f, 1.119f, 0.391f, 0.078f, 0.021f },  // 200.0 Hz
                { 2.155f, 1.171f, 0.722f, 0.187f, 0.047f },  // 230.0 Hz
                { 1.998f, 1.131f, 1.138f, 0.383f, 0.091f },  // 264.5 Hz
                { 1.854f, 1.081f, 1.608f, 0.624f, 0.175f },  // 304.2 Hz
                { 1.728f, 1.066f, 1.822f, 1.021f, 0.472f },  // 349.8 Hz
                { 1.619f, 1.066f, 1.569f, 1.650f, 0.874f },  // 402.3 Hz
                { 1.526f, 1.058f, 1.304f, 2.521f, 1.149f },  // 462.6 Hz
                { 1.447f, 1.049f, 1.225f, 2.341f, 1.883f },  // 532.0 Hz
                { 1.380f, 1.049f, 1.310f, 1.676f, 3.370f },  // 611.8 Hz
                { 1.322f, 1.050f, 1.365f, 1.396f, 2.793f },  // 703.6 Hz
                { 1.272f, 1.051f, 1.289f, 1.418f, 1.836f },  // 809.1 Hz
                { 1.229f, 1.049f, 1.277f, 1.763f, 1.496f },  // 930.5 Hz
                { 1.193f, 1.044f, 1.288f, 1.678f, 1.559f },  // 1070.1 Hz
                { 1.162f, 1.038f, 1.266f, 1.473f, 2.215f },  // 1230.6 Hz
                { 1.136f, 1.032f, 1.255f, 1.574f, 1.950f },  // 1415.1 Hz
                { 1.114f, 1.028f, 1.233f, 1.523f, 1.616f },  // 1627.4 Hz
                { 1.096f, 1.025f, 1.212f, 1.497f, 1.887f },  // 1871.5 Hz
                { 1.080f, 1.023f, 1.193f, 1.462f, 1.728f },  // 2152.3 Hz
                { 1.066f, 1.021f, 1.176f, 1.435f, 1.725f },  // 2475.1 Hz
                { 1.054f, 1.020f, 1.163f, 1.397f, 1.652f },  // 2846.4 Hz
                { 1.044f, 1.018f, 1.150f, 1.364f, 1.641f },  // 3273.3 Hz
                { 1.036f, 1.015f, 1.136f, 1.334f, 1.570f },  // 3764.3 Hz
                { 1.028f, 1.012f, 1.122f, 1.308f, 1.520f },  // 4328.9 Hz
                { 1.022f, 1.009f, 1.109f, 1.280f, 1.479f },  // 4978.3 Hz
                { 1.017f, 1.006f, 1.095f, 1.251f, 1.442f },  // 5725.0 Hz
                { 1.013f, 1.004f, 1.080f, 1.225f, 1.404f },  // 6583.8 Hz
                { 1.010f, 1.002f, 1.067f, 1.202f, 1.366f },  // 7571.4 Hz
                { 1.008f, 1.001f, 1.062f, 1.193f, 1.352f },  // 8707.1 Hz
            },
            {   // One pole
                { 1.092f, 2.283f, 1.359f, 0.368f, 0.123f },  // 200.0 Hz
                { 1.083f, 2.296f, 2.243f, 0.766f, 0.238f },  // 230.0 Hz
                { 1.072f, 2.159f, 3.455f, 1.595f, 0.487f },  // 264.5 Hz
                { 1.060f, 2.036f, 4.972f, 2.829f, 1.096f },  // 304.2 Hz
                { 1.049f, 1.982f, 5.443f, 4.243f, 2.517f },  // 349.8 Hz
                { 1.040f, 1.956f, 4.509f, 6.462f, 4.341f },  // 402.3 Hz
                { 1.034f, 1.918f, 3.695f, 9.937f, 5.940f },  // 462.6 Hz
                { 1.028f, 1.881f, 3.382f, 8.798f, 8.983f },  // 532.0 Hz
                { 1.023f, 1.858f, 3.533f, 6.157f, 15.899f },  // 611.8 Hz
                { 1.018f, 1.838f, 3.595f, 4.960f, 12.579f },  // 703.6 Hz
                { 1.013f, 1.819f, 3.320f, 4.899f, 8.025f },  // 809.1 Hz
                { 1.009f, 1.797f, 3.225f, 5.934f, 6.342f },  // 930.5 Hz
                { 1.005f, 1.773f, 3.197f, 5.519f, 6.433f },  // 1070.1 Hz
                { 1.002f, 1.749f, 3.096f, 4.754f, 8.933f },  // 1230.6 Hz
                { 1.000f, 1.728f, 3.027f, 4.987f, 7.691f },  // 1415.1 Hz
                { 0.998f, 1.710f, 2.935f, 4.739f, 6.235f },  // 1627.4 Hz
                { 0.996f, 1.696f, 2.852f, 4.583f, 7.133f },  // 1871.5 Hz
                { 0.995f, 1.683f, 2.775f, 4.408f, 6.417f },  // 2152.3 Hz
                { 0.993f, 1.672f, 2.707f, 4.266f, 6.294f },  // 2475.1 Hz
                { 0.992f, 1.662f, 2.652f, 4.100f, 5.936f },  // 2846.4 Hz
                { 0.991f, 1.652f, 2.602f, 3.961f, 5.825f },  // 3273.3 Hz
                { 0.990f, 1.642f, 2.554f, 3.843f, 5.518f },  // 3764.3 Hz
                { 0.989f, 1.632f, 2.511f, 3.744f, 5.304f },  // 4328.9 Hz
                { 0.988f, 1.624f, 2.471f, 3.647f, 5.132f },  // 4978.3 Hz
                { 0.988f, 1.616f, 2.433f, 3.553f, 4.985f },  // 5725.0 Hz
                { 0.987f, 1.610f, 2.397f, 3.470f, 4.837f },  // 6583.8 Hz
                { 0.987f, 1.606f, 2.362f, 3.397f, 4.694f },  // 7571.4 Hz
                { 0.986f, 1.605f, 2.350f, 3.368f, 4.641f },  // 8707.1 Hz
            },
            {   // Diode 303 (asymmetric diode ladder), measured against the Classic anchor
                { 34.267f, 55.344f, 26.370f, 6.194f, 1.923f },  // 200.0 Hz
                { 23.736f, 36.926f, 28.892f, 8.231f, 2.320f },  // 230.0 Hz
                { 16.327f, 23.331f, 29.544f, 11.022f, 3.030f },  // 264.5 Hz
                { 11.388f, 14.970f, 27.817f, 12.730f, 4.260f },  // 304.2 Hz
                { 8.162f, 10.055f, 19.926f, 12.482f, 6.053f },  // 349.8 Hz
                { 6.054f, 7.017f, 11.281f, 12.563f, 6.814f },  // 402.3 Hz
                { 4.668f, 5.069f, 6.663f, 13.354f, 6.668f },  // 462.6 Hz
                { 3.738f, 3.861f, 4.687f, 9.045f, 7.853f },  // 532.0 Hz
                { 3.099f, 3.142f, 4.076f, 5.567f, 13.096f },  // 611.8 Hz
                { 2.647f, 2.733f, 3.898f, 5.041f, 13.673f },  // 703.6 Hz
                { 2.318f, 2.526f, 3.809f, 5.693f, 9.605f },  // 809.1 Hz
                { 2.072f, 2.429f, 3.903f, 6.924f, 6.880f },  // 930.5 Hz
                { 1.885f, 2.365f, 3.872f, 6.024f, 6.401f },  // 1070.1 Hz
                { 1.740f, 2.292f, 3.694f, 5.389f, 10.216f },  // 1230.6 Hz
                { 1.624f, 2.212f, 3.624f, 5.825f, 8.442f },  // 1415.1 Hz
                { 1.530f, 2.140f, 3.505f, 5.462f, 7.094f },  // 1627.4 Hz
                { 1.450f, 2.077f, 3.352f, 5.362f, 8.062f },  // 1871.5 Hz
                { 1.383f, 2.019f, 3.212f, 5.127f, 7.462f },  // 2152.3 Hz
                { 1.325f, 1.965f, 3.086f, 4.947f, 7.385f },  // 2475.1 Hz
                { 1.275f, 1.917f, 2.990f, 4.743f, 7.077f },  // 2846.4 Hz
                { 1.232f, 1.875f, 2.917f, 4.571f, 6.991f },  // 3273.3 Hz
                { 1.195f, 1.838f, 2.849f, 4.406f, 6.629f },  // 3764.3 Hz
                { 1.164f, 1.803f, 2.774f, 4.248f, 6.305f },  // 4328.9 Hz
                { 1.137f, 1.768f, 2.694f, 4.079f, 5.992f },  // 4978.3 Hz
                { 1.114f, 1.730f, 2.607f, 3.898f, 5.710f },  // 5725.0 Hz
                { 1.095f, 1.688f, 2.513f, 3.710f, 5.411f },  // 6583.8 Hz
                { 1.079f, 1.643f, 2.412f, 3.518f, 5.078f },  // 7571.4 Hz
                { 1.073f, 1.624f, 2.370f, 3.440f, 4.943f },  // 8707.1 Hz
            }
        };

        if (m == Model::Vowel)
            return vowelGainAt (cutoffHz, res);
        // Diode303 is the seventh table row; the six before it keep their order.
        const int mi = m == Model::Diode303 ? 6 : std::min (5, std::max (0, static_cast<int> (m)));
        if (mi == 0)
            return 1.0f;

        res = std::min (1.0f, std::max (0.0f, res));
        const int ri = std::min (3, static_cast<int> (res * 4.0f));
        const float fr = res * 4.0f - static_cast<float> (ri);

        int ci = 0;
        while (ci < 26 && cutoffHz > kCuts[ci + 1])
            ++ci;
        float fc = (cutoffHz - kCuts[ci]) / (kCuts[ci + 1] - kCuts[ci]);
        fc = std::min (1.0f, std::max (0.0f, fc));

        const auto gains = kTable[mi];
        float lo = gains[ci][ri] + (gains[ci][ri + 1] - gains[ci][ri]) * fr;
        float hi = gains[ci + 1][ri] + (gains[ci + 1][ri + 1] - gains[ci + 1][ri]) * fr;
        // Cap the fitted gain: the largest cells sit in near-silent pockets
        // (the Moog ladder hovers on the edge of self-oscillation there), where
        // stretching the level 20-100x would slam the output and swing wildly
        // with warmth drift. Leaving the model quieter than the anchor is the
        // safe trade; the cap also bounds churn under parameter storms.
        return std::min (8.0f, lo + (hi - lo) * fc);
    }

private:
    static constexpr float pi = 3.14159265358979f;

    void resetAll()
    {
        lp1 = bp1 = hp1 = lp2 = bp2 = hp2 = 0.0f;
        for (int i = 0; i < 4; ++i) ladder[i] = 0.0f;
        svfZ1 = svfZ2 = 0.0f;
        skZ1 = skZ2 = 0.0f;
        poleZ = 0.0f;
        for (int i = 0; i < 3; ++i)
            vowelBank[i] = VowelSection();
        vowelTiltZ = 0.0f;
        vowelTiltG = 0.0f;
        vowelTiltStep = 0.0f;
        vowelCoeffInit = false;
        vowelTick = 0;
    }

    float processClassic (float in, float /*cutoffHz*/, float f)
    {
        const float q1 = 0.5f + resonance * resonance * 18.0f;
        float out = section (in, f, q1, lp1, bp1, hp1);
        const float q2 = 0.5f + resonance * resonance * 6.0f;
        const float lpBoth = section (out, f, q2, lp2, bp2, hp2);
        return lp1 + slope * (lpBoth - lp1);
    }

    float processDiode (float in, float f)
    {
        const float resK = resonance * 3.2f;
        const float x = in - resK * ladder[3];
        for (int i = 0; i < 4; ++i)
        {
            const float src = (i == 0) ? std::tanh (x) : ladder[i - 1];
            ladder[i] += f * (std::tanh (src) - std::tanh (ladder[i]));
            ladder[i] = std::max (-3.0f, std::min (3.0f, ladder[i]));
        }
        return ladder[3] / (1.0f + 0.15f * resonance * resonance);
    }

    /** The 303's own diode ladder, as far as a four-stage model can carry it: the
        diode pair is asymmetric (its two halves do not bend alike), so every stage
        makes even harmonics on top of the odd ones, and the feedback sags as the
        cutoff climbs, which is why the 303's resonance is loudest in the low
        register. The symmetric DiodeLadder above has neither trait. */
    static float diodePair (float v) noexcept
    {
        static const float kBias = std::tanh (0.12f);
        return std::tanh (1.1f * v + 0.12f) - kBias;
    }

    float processDiode303 (float in, float f)
    {
        const float sag = std::max (0.1f, 1.0f - 2.0f * f);
        const float resK = resonance * 3.4f * sag;
        float src = in - resK * ladder[3];
        for (int i = 0; i < 4; ++i)
        {
            ladder[i] += f * (diodePair (src) - diodePair (ladder[i]));
            ladder[i] = std::max (-3.0f, std::min (3.0f, ladder[i]));
            src = ladder[i];
        }
        return ladder[3] / (1.0f + 0.15f * resonance * resonance);
    }

    float processMoog (float in, float f)
    {
        const float g = std::tanh (pi * f);
        const float resK = resonance * 3.6f;
        const float x = in - resK * ladder[3];
        for (int i = 0; i < 4; ++i)
        {
            const float src = (i == 0) ? x : ladder[i - 1];
            ladder[i] += g * (src - std::tanh (ladder[i]));
            ladder[i] = std::max (-3.0f, std::min (3.0f, ladder[i]));
            // Anti-lock: the 303's resonance can push a stage past +/-2.5, where
            // tanh's derivative collapses to ~0.07 and the stage stops following
            // the knobs. Folding the rail back to +/-2.0 keeps the gradient alive
            // so the filter keeps tracking; the clamp only bites in saturation,
            // so the calibrated (unsaturated) regime is unchanged.
            if (std::fabs (ladder[i]) > 2.5f)
                ladder[i] = std::copysign (2.0f, ladder[i]);
        }
        return ladder[3] / (1.0f + 0.12f * resonance * resonance);
    }

    float processTpt (float in, float f)
    {
        const float g = std::tanh (pi * f);
        const float Q = 0.5f + resonance * resonance * 18.0f;
        const float k = 1.0f / Q;
        // Zero-delay feedback solved for both tapped integrators at once: the
        // feedback taps the new outputs y1 = z1 + g*v3 and y2 = z2 + g*y1, which
        // collapses to this single factor and keeps the passband gain at ~1.
        // (Dropping the (k+g)*z1 damping term made this model ~40x quieter.)
        const float a = 1.0f / (1.0f + g * (g + k));
        const float v3 = a * (in - svfZ2 - (k + g) * svfZ1);
        svfZ1 += g * v3;
        // lowpass output comes from the second integrator reunion.
        float out = svfZ2 + g * svfZ1;
        svfZ2 = out;
        return out;
    }

    float processSallen (float in, float f)
    {
        const float g = std::tanh (pi * f);
        const float Q = 0.3f + resonance * 3.5f;
        const float k = 1.0f / Q;
        const float a = 1.0f / (1.0f + g * (g + k));
        const float v3 = a * (in - skZ2 - (k + g) * skZ1);
        skZ1 += g * v3;
        float out = skZ2 + g * skZ1;
        skZ2 = out;
        return out;
    }

    /** ---- Vowel: a talking formant bank -----------------------------------
        What makes a vowel recognisable is the pair F1/F2 (tongue height and
        front-back position) and how narrow those resonances are. So this model
        is not a lowpass with a bump: it is a source pole in series with three
        parallel bandpass resonators tuned to F1..F3, whose centres slide along a
        five-point vowel trajectory. Cutoff sets the position on that trajectory,
        which is why one knob both opens the filter and moves the mouth - and why
        the note envelope articulates on its own. */
    struct VowelSection
    {
        float coef[5]   { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // b0, b1, b2, a1, a2
        float step[5]   { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float target[5] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float gain = 0.0f, gainStep = 0.0f, gainTarget = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    /** Formant centres (Hz) at oo, oh, ah, eh, ee - adult male, the F1/F2 pair
        that carries the vowel identity and F3 for the vocal colour. */
    static constexpr float kVowelF[5][3] = {
        { 300.0f,  730.0f, 2200.0f },  // oo
        { 450.0f, 1020.0f, 2350.0f },  // oh
        { 730.0f, 1250.0f, 2600.0f },  // ah
        { 610.0f, 1900.0f, 2550.0f },  // eh
        { 280.0f, 2250.0f, 3000.0f },  // ee
    };
    /** Nominal -3 dB formant bandwidths (Hz). Real formants are far narrower
        than the resonance bumps of a synth filter, which is exactly what reads
        as a voice rather than as a swept lowpass. */
    static constexpr float kVowelBW[3] = { 80.0f, 90.0f, 120.0f };
    /** Relative envelope levels of F1..F3 in radiated speech. Multiplied by the
        centre frequency below, because the oscillator already loses 6 dB per
        octave and the resonator has to make that back up, or the upper formants
        - the ones that identify the vowel - disappear. The mid range sits a
        little above a male voice, which is what makes the line intelligible as
        a talky acid lead rather than as a swept lowpass. */
    static constexpr float kVowelSpeech[3] = { 1.00f, 0.80f, 0.50f };
    static constexpr float kVowelRefHz = 300.0f;
    /** Cutoff range (Hz) that walks the trajectory from oo to ee. */
    static constexpr float kVowelLoHz = 150.0f, kVowelHiHz = 4200.0f;
    static constexpr int kVowelCoefInterval = 16;

    static float vowelPosition (float cutoffHz) noexcept
    {
        const float lo = std::log (kVowelLoHz), hi = std::log (kVowelHiHz);
        const float c = std::max (kVowelLoHz, std::min (kVowelHiHz, cutoffHz));
        return std::min (1.0f, std::max (0.0f, (std::log (c) - lo) / (hi - lo)));
    }

    /** Level matching for the bank, measured against the Classic anchor over
        full offline renders exactly like modelGainAt: fourteen cutoff knots a
        third of an octave apart, nine resonance knots (narrow formants fall off
        steeply), interpolated geometrically, 12x cap. */
    static float vowelGainAt (float cutoffHz, float res) noexcept
    {
        static constexpr float kCuts[14] = { 150.0f, 200.0f, 260.0f, 340.0f, 440.0f, 570.0f, 740.0f,
                                             960.0f, 1250.0f, 1600.0f, 2100.0f, 2700.0f, 3600.0f, 4200.0f };
        static constexpr float kTable[14][9] = {
            { 1.332f, 1.539f, 1.407f, 0.749f, 0.339f, 0.159f, 0.087f, 0.060f, 0.074f },  // 150.0 Hz
            { 1.729f, 2.095f, 2.803f, 2.342f, 1.376f, 0.727f, 0.376f, 0.221f, 0.152f },  // 200.0 Hz
            { 1.989f, 2.360f, 3.441f, 4.595f, 3.715f, 2.491f, 1.590f, 0.918f, 0.575f },  // 260.0 Hz
            { 2.230f, 2.587f, 3.493f, 5.467f, 7.399f, 6.513f, 4.880f, 3.900f, 2.922f },  // 340.0 Hz
            { 2.293f, 2.614f, 3.367f, 4.597f, 7.352f, 10.460f, 10.971f, 8.747f, 7.977f },  // 440.0 Hz
            { 2.238f, 2.517f, 3.141f, 4.143f, 5.367f, 8.300f, 13.185f, 16.689f, 17.396f },  // 570.0 Hz
            { 2.166f, 2.409f, 2.922f, 3.768f, 4.968f, 6.090f, 8.261f, 14.166f, 22.074f },  // 740.0 Hz
            { 2.088f, 2.294f, 2.709f, 3.380f, 4.353f, 5.759f, 7.130f, 8.256f, 11.911f },  // 960.0 Hz
            { 2.014f, 2.190f, 2.525f, 3.058f, 3.824f, 4.800f, 6.311f, 8.082f, 9.806f },  // 1250.0 Hz
            { 1.955f, 2.114f, 2.402f, 2.855f, 3.506f, 4.419f, 5.526f, 7.221f, 10.137f },  // 1600.0 Hz
            { 1.894f, 2.037f, 2.280f, 2.656f, 3.195f, 3.933f, 4.907f, 6.395f, 7.938f },  // 2100.0 Hz
            { 1.859f, 1.991f, 2.204f, 2.526f, 2.989f, 3.624f, 4.477f, 5.678f, 7.094f },  // 2700.0 Hz
            { 1.897f, 2.024f, 2.218f, 2.503f, 2.914f, 3.489f, 4.292f, 5.471f, 6.731f },  // 3600.0 Hz
            { 1.919f, 2.042f, 2.224f, 2.484f, 2.851f, 3.352f, 4.027f, 4.957f, 5.885f },  // 4200.0 Hz
        };
        res = std::min (1.0f, std::max (0.0f, res));
        const float rf = res * 8.0f;
        const int ri = std::min (7, static_cast<int> (rf));
        const float fr = rf - static_cast<float> (ri);

        int ci = 0;
        while (ci < 12 && cutoffHz > kCuts[ci + 1])
            ++ci;
        const float fc = std::min (1.0f, std::max (0.0f,
                        (cutoffHz - kCuts[ci]) / (kCuts[ci + 1] - kCuts[ci])));

        // Interpolate geometrically: the field spans two decades and a linear
        // mean between two knots overshoots badly in the singing pockets.
        const float r0 = kTable[ci][ri] + (kTable[ci][ri + 1] - kTable[ci][ri]) * fr;
        const float r1 = kTable[ci + 1][ri] + (kTable[ci + 1][ri + 1] - kTable[ci + 1][ri]) * fr;
        return std::min (12.0f, std::max (0.05f, std::exp (std::log (r0)
                                                           + (std::log (r1) - std::log (r0)) * fc)));
    }

    /** Formants, bandwidths, per-section levels and the source pole for the
        cutoff/resonance pair currently in effect. */
    void buildVowelTarget (float cutoffHz)
    {
        const float sr = static_cast<float> (std::max (8000.0, sampleRate));
        const float pos = vowelPosition (cutoffHz) * 4.0f;
        const int k = std::min (3, std::max (0, static_cast<int> (pos)));
        const float fr = pos - static_cast<float> (k);
        // Resonance only sets the width: twice the nominal bandwidth when fully
        // counter-clockwise (a dull, muffled voice), a little over a third of it
        // at maximum (a thin, ringing one).
        const float bwMul = 2.0f - 1.55f * resonance;

        for (int i = 0; i < 3; ++i)
        {
            // Interpolate in log-frequency: formants move like pitches do.
            const float fa = std::log (kVowelF[k][i]), fb = std::log (kVowelF[k + 1][i]);
            const float hz = std::max (60.0f, std::min (sr * 0.45f, std::exp (fa + (fb - fa) * fr)));
            const float q = std::min (40.0f, std::max (0.6f, hz / (kVowelBW[i] * bwMul)));
            const float om = 2.0f * pi * std::min (0.45f, std::max (1.0e-5f, hz / sr));
            const float s = std::sin (om), co = std::cos (om);
            const float alpha = s / (2.0f * q);
            const float den = 1.0f + alpha;
            VowelSection& sec = vowelBank[i];
            // Peak-normalised bandpass (0 dB at the centre) times the speech
            // envelope level, pre-compensated for the source's 6 dB per octave.
            sec.target[0] = alpha / den;
            sec.target[1] = 0.0f;
            sec.target[2] = -alpha / den;
            sec.target[3] = -2.0f * co / den;
            sec.target[4] = (1.0f - alpha) / den;
            sec.gainTarget = kVowelSpeech[i] * (hz / kVowelRefHz);
        }
        // The series pole is the lowpass half of the voice: the glottal source
        // falls off above the third formant. It follows the same knob, so
        // sweeping up opens the mouth and the brightness together.
        const float tiltHz = std::max (900.0f, std::min (6500.0f, 2.4f * std::max (kVowelLoHz, std::min (kVowelHiHz, cutoffHz))));
        vowelTiltTargetG = std::tanh (pi * std::min (0.45f, std::max (1.0e-5f, tiltHz / sr)));
    }

    /** Recompute the bank every kVowelCoefInterval samples and glide the
        coefficients (and each section's level) between the updates; landing
        exactly on the target keeps a stationary cutoff bit-exact. */
    void trackVowel (float cutoffHz)
    {
        if (vowelTick == 0)
        {
            buildVowelTarget (cutoffHz);
            const float inv = 1.0f / static_cast<float> (kVowelCoefInterval);
            for (int i = 0; i < 3; ++i)
            {
                VowelSection& sec = vowelBank[i];
                if (! vowelCoeffInit)
                {
                    for (int c = 0; c < 5; ++c)
                        sec.coef[c] = sec.target[c];
                    sec.gain = sec.gainTarget;
                }
                for (int c = 0; c < 5; ++c)
                    sec.step[c] = (sec.target[c] - sec.coef[c]) * inv;
                sec.gainStep = (sec.gainTarget - sec.gain) * inv;
            }
            if (! vowelCoeffInit)
            {
                vowelTiltG = vowelTiltTargetG;
                vowelCoeffInit = true;
            }
            vowelTiltStep = (vowelTiltTargetG - vowelTiltG) * inv;
            vowelTick = kVowelCoefInterval;
        }

        for (int i = 0; i < 3; ++i)
        {
            VowelSection& sec = vowelBank[i];
            for (int c = 0; c < 5; ++c)
                sec.coef[c] += sec.step[c];
            sec.gain += sec.gainStep;
        }
        vowelTiltG += vowelTiltStep;

        if (--vowelTick == 0)
        {
            for (int i = 0; i < 3; ++i)
            {
                VowelSection& sec = vowelBank[i];
                for (int c = 0; c < 5; ++c)
                {
                    sec.coef[c] = sec.target[c];
                    sec.step[c] = 0.0f;
                }
                sec.gain = sec.gainTarget;
                sec.gainStep = 0.0f;
            }
            vowelTiltG = vowelTiltTargetG;
            vowelTiltStep = 0.0f;
        }
    }

    float processVowel (float in, float cutoffHz)
    {
        trackVowel (cutoffHz);

        vowelTiltZ += vowelTiltG * (in - vowelTiltZ);
        vowelTiltZ = std::max (-4.0f, std::min (4.0f, vowelTiltZ));

        float out = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            VowelSection& sec = vowelBank[i];
            float y = sec.coef[0] * vowelTiltZ + sec.coef[1] * sec.x1 + sec.coef[2] * sec.x2
                    - sec.coef[3] * sec.y1 - sec.coef[4] * sec.y2;
            // Narrow formants ring for a long time; bound each resonator the way
            // the other models bound their own feedback paths.
            y = std::max (-4.0f, std::min (4.0f, y));
            sec.x2 = sec.x1;
            sec.x1 = vowelTiltZ;
            sec.y2 = sec.y1;
            sec.y1 = y;
            out += y * sec.gain;
        }
        return std::max (-4.0f, std::min (4.0f, out));
    }

    float processOnePole (float in, float f)
    {
        const float g = std::tanh (pi * f);
        const float fb = resonance * 2.4f * poleZ;
        poleZ += g * (in - fb - poleZ);
        // At full resonance the loop gain exceeds 1; without a bound the single
        // pole would run away (it is self-oscillation by design, so a clipper
        // rather than the other filters' soft tanh).
        poleZ = std::max (-4.0f, std::min (4.0f, poleZ));
        return poleZ;
    }

    double sampleRate = 48000.0;
    Model model = Model::Classic;
    float resonance = 0.5f, slope = 1.0f;
    float gainApplied = 1.0f;
    float gainSlew = 0.0082f;
    float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f;
    float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f;
    float ladder[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float svfZ1 = 0.0f, svfZ2 = 0.0f, skZ1 = 0.0f, skZ2 = 0.0f, poleZ = 0.0f;
    VowelSection vowelBank[3];
    float vowelTiltZ = 0.0f, vowelTiltG = 0.0f, vowelTiltTargetG = 0.0f, vowelTiltStep = 0.0f;
    int vowelTick = 0;
    bool vowelCoeffInit = false;
};
