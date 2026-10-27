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
                {44.201f, 62.394f, 55.382f, 17.675f, 7.389f},  // 200.0 Hz
                {28.693f, 44.812f, 50.273f, 24.416f, 10.057f},  // 230.0 Hz
                {19.213f, 26.916f, 42.849f, 32.096f, 14.117f},  // 264.5 Hz
                {13.575f, 17.157f, 35.695f, 31.060f, 17.775f},  // 304.2 Hz
                {9.875f, 11.981f, 24.636f, 23.434f, 28.555f},  // 349.8 Hz
                {7.368f, 8.633f, 14.355f, 19.181f, 39.859f},  // 402.3 Hz
                {5.657f, 6.291f, 8.453f, 19.004f, 29.951f},  // 462.6 Hz
                {4.444f, 4.694f, 5.903f, 12.858f, 20.456f},  // 532.0 Hz
                {3.542f, 3.756f, 5.007f, 6.950f, 19.087f},  // 611.8 Hz
                {2.988f, 3.171f, 4.444f, 5.109f, 15.630f},  // 703.6 Hz
                {2.586f, 2.825f, 4.030f, 5.549f, 13.584f},  // 809.1 Hz
                {2.296f, 2.636f, 4.077f, 7.232f, 14.261f},  // 930.5 Hz
                {2.082f, 2.534f, 4.206f, 7.086f, 13.799f},  // 1070.1 Hz
                {1.916f, 2.459f, 4.036f, 5.874f, 12.151f},  // 1230.6 Hz
                {1.785f, 2.380f, 3.891f, 6.059f, 10.522f},  // 1415.1 Hz
                {1.684f, 2.310f, 3.755f, 6.121f, 9.998f},  // 1627.4 Hz
                {1.600f, 2.248f, 3.608f, 5.803f, 9.219f},  // 1871.5 Hz
                {1.532f, 2.193f, 3.467f, 5.587f, 8.100f},  // 2152.3 Hz
                {1.471f, 2.138f, 3.340f, 5.328f, 8.281f},  // 2475.1 Hz
                {1.422f, 2.094f, 3.248f, 5.144f, 7.783f},  // 2846.4 Hz
                {1.380f, 2.054f, 3.185f, 4.985f, 7.442f},  // 3273.3 Hz
                {1.344f, 2.023f, 3.132f, 4.856f, 7.397f},  // 3764.3 Hz
                {1.314f, 1.999f, 3.088f, 4.777f, 7.147f},  // 4328.9 Hz
                {1.289f, 1.979f, 3.050f, 4.671f, 6.876f},  // 4978.3 Hz
                {1.267f, 1.962f, 3.009f, 4.557f, 6.730f},  // 5725.0 Hz
                {1.251f, 1.947f, 2.973f, 4.468f, 6.592f},  // 6583.8 Hz
                {1.235f, 1.931f, 2.935f, 4.383f, 6.406f},  // 7571.4 Hz
                {1.224f, 1.920f, 2.906f, 4.319f, 6.281f},  // 8707.1 Hz
            },
            {   // Moog ladder
                {1.998f, 0.276f, 0.189f, 0.039f, 0.016f},  // 200.0 Hz
                {1.660f, 0.285f, 0.418f, 0.091f, 0.023f},  // 230.0 Hz
                {1.395f, 1.226f, 3.781f, 0.195f, 0.050f},  // 264.5 Hz
                {1.182f, 1.484f, 0.727f, 0.333f, 0.100f},  // 304.2 Hz
                {1.003f, 1.387f, 0.779f, 0.464f, 0.271f},  // 349.8 Hz
                {0.844f, 0.262f, 0.645f, 0.807f, 0.572f},  // 402.3 Hz
                {0.693f, 0.262f, 0.544f, 1.344f, 0.659f},  // 462.6 Hz
                {0.545f, 0.258f, 0.499f, 1.211f, 0.857f},  // 532.0 Hz
                {0.414f, 0.259f, 0.530f, 0.816f, 1.574f},  // 611.8 Hz
                {0.344f, 0.259f, 0.563f, 0.634f, 1.479f},  // 703.6 Hz
                {0.294f, 0.257f, 0.515f, 0.623f, 0.991f},  // 809.1 Hz
                {0.281f, 0.257f, 0.489f, 0.737f, 0.781f},  // 930.5 Hz
                {0.273f, 0.254f, 0.492f, 0.729f, 0.810f},  // 1070.1 Hz
                {0.271f, 0.330f, 0.479f, 0.630f, 0.945f},  // 1230.6 Hz
                {0.270f, 0.520f, 0.467f, 0.626f, 0.830f},  // 1415.1 Hz
                {0.270f, 1.100f, 0.458f, 0.634f, 0.787f},  // 1627.4 Hz
                {0.270f, 0.248f, 0.443f, 0.599f, 0.794f},  // 1871.5 Hz
                {0.270f, 0.420f, 0.421f, 0.573f, 0.721f},  // 2152.3 Hz
                {0.270f, 0.400f, 0.404f, 0.550f, 0.760f},  // 2475.1 Hz
                {0.271f, 0.330f, 0.258f, 0.535f, 0.695f},  // 2846.4 Hz
                {0.272f, 0.244f, 0.937f, 0.518f, 0.654f},  // 3273.3 Hz
                {0.273f, 0.243f, 0.016f, 0.554f, 0.644f},  // 3764.3 Hz
                {0.274f, 1.181f, 2.499f, 0.224f, 0.495f},  // 4328.9 Hz
                {0.274f, 1.376f, 2.722f, 2.104f, 0.970f},  // 4978.3 Hz
                {0.275f, 1.482f, 2.831f, 4.433f, 0.016f},  // 5725.0 Hz
                {0.275f, 1.549f, 2.876f, 4.496f, 6.626f},  // 6583.8 Hz
                {0.276f, 1.596f, 2.892f, 4.496f, 6.661f},  // 7571.4 Hz
                {0.276f, 1.611f, 2.895f, 4.480f, 6.620f},  // 8707.1 Hz
            },
            {   // TPT SVF
                {1.409f, 0.917f, 0.328f, 0.060f, 0.020f},  // 200.0 Hz
                {1.347f, 1.009f, 0.652f, 0.180f, 0.059f},  // 230.0 Hz
                {1.291f, 0.987f, 0.897f, 0.295f, 0.077f},  // 264.5 Hz
                {1.244f, 0.945f, 1.005f, 0.281f, 0.057f},  // 304.2 Hz
                {1.205f, 0.950f, 1.455f, 0.779f, 0.414f},  // 349.8 Hz
                {1.172f, 0.972f, 1.283f, 1.294f, 0.995f},  // 402.3 Hz
                {1.147f, 0.979f, 0.986f, 1.684f, 0.754f},  // 462.6 Hz
                {1.126f, 0.984f, 1.048f, 2.105f, 1.521f},  // 532.0 Hz
                {1.106f, 0.994f, 1.083f, 1.171f, 1.913f},  // 611.8 Hz
                {1.089f, 1.005f, 1.191f, 1.163f, 2.323f},  // 703.6 Hz
                {1.073f, 1.011f, 1.151f, 1.205f, 1.631f},  // 809.1 Hz
                {1.059f, 1.012f, 1.152f, 1.460f, 1.308f},  // 930.5 Hz
                {1.049f, 1.011f, 1.178f, 1.454f, 1.454f},  // 1070.1 Hz
                {1.040f, 1.005f, 1.167f, 1.321f, 1.763f},  // 1230.6 Hz
                {1.031f, 1.001f, 1.172f, 1.382f, 1.656f},  // 1415.1 Hz
                {1.023f, 0.999f, 1.158f, 1.427f, 1.610f},  // 1627.4 Hz
                {1.020f, 1.002f, 1.142f, 1.404f, 1.701f},  // 1871.5 Hz
                {1.015f, 1.000f, 1.126f, 1.382f, 1.589f},  // 2152.3 Hz
                {1.011f, 1.000f, 1.112f, 1.354f, 1.696f},  // 2475.1 Hz
                {1.008f, 1.000f, 1.107f, 1.330f, 1.609f},  // 2846.4 Hz
                {1.005f, 1.002f, 1.104f, 1.301f, 1.546f},  // 3273.3 Hz
                {1.002f, 1.003f, 1.097f, 1.277f, 1.533f},  // 3764.3 Hz
                {1.001f, 1.004f, 1.091f, 1.261f, 1.484f},  // 4328.9 Hz
                {0.999f, 1.004f, 1.085f, 1.244f, 1.438f},  // 4978.3 Hz
                {0.997f, 1.003f, 1.077f, 1.220f, 1.407f},  // 5725.0 Hz
                {0.996f, 1.002f, 1.070f, 1.199f, 1.375f},  // 6583.8 Hz
                {0.995f, 1.000f, 1.061f, 1.179f, 1.336f},  // 7571.4 Hz
                {0.995f, 1.000f, 1.054f, 1.163f, 1.308f},  // 8707.1 Hz
            },
            {   // Sallen-Key
                {2.242f, 1.102f, 0.399f, 0.071f, 0.022f},  // 200.0 Hz
                {2.095f, 1.154f, 0.739f, 0.190f, 0.054f},  // 230.0 Hz
                {1.951f, 1.116f, 1.174f, 0.414f, 0.108f},  // 264.5 Hz
                {1.818f, 1.072f, 1.636f, 0.632f, 0.184f},  // 304.2 Hz
                {1.699f, 1.063f, 1.826f, 0.995f, 0.562f},  // 349.8 Hz
                {1.595f, 1.070f, 1.575f, 1.759f, 1.350f},  // 402.3 Hz
                {1.506f, 1.067f, 1.323f, 2.918f, 1.502f},  // 462.6 Hz
                {1.428f, 1.062f, 1.253f, 2.549f, 1.976f},  // 532.0 Hz
                {1.363f, 1.063f, 1.339f, 1.749f, 3.416f},  // 611.8 Hz
                {1.306f, 1.065f, 1.396f, 1.456f, 3.102f},  // 703.6 Hz
                {1.257f, 1.063f, 1.330f, 1.450f, 2.003f},  // 809.1 Hz
                {1.216f, 1.057f, 1.315f, 1.791f, 1.678f},  // 930.5 Hz
                {1.181f, 1.048f, 1.325f, 1.754f, 1.818f},  // 1070.1 Hz
                {1.151f, 1.039f, 1.295f, 1.549f, 2.121f},  // 1230.6 Hz
                {1.125f, 1.030f, 1.278f, 1.575f, 1.933f},  // 1415.1 Hz
                {1.104f, 1.025f, 1.245f, 1.588f, 1.814f},  // 1627.4 Hz
                {1.087f, 1.022f, 1.212f, 1.531f, 1.885f},  // 1871.5 Hz
                {1.072f, 1.017f, 1.185f, 1.485f, 1.727f},  // 2152.3 Hz
                {1.059f, 1.015f, 1.158f, 1.432f, 1.804f},  // 2475.1 Hz
                {1.050f, 1.013f, 1.143f, 1.389f, 1.690f},  // 2846.4 Hz
                {1.040f, 1.012f, 1.131f, 1.345f, 1.602f},  // 3273.3 Hz
                {1.033f, 1.011f, 1.119f, 1.308f, 1.574f},  // 3764.3 Hz
                {1.026f, 1.011f, 1.106f, 1.284f, 1.512f},  // 4328.9 Hz
                {1.021f, 1.009f, 1.096f, 1.259f, 1.458f},  // 4978.3 Hz
                {1.016f, 1.007f, 1.086f, 1.232f, 1.420f},  // 5725.0 Hz
                {1.012f, 1.005f, 1.077f, 1.208f, 1.386f},  // 6583.8 Hz
                {1.009f, 1.003f, 1.067f, 1.186f, 1.345f},  // 7571.4 Hz
                {1.006f, 1.002f, 1.059f, 1.169f, 1.315f},  // 8707.1 Hz
            },
            {   // One pole
                {1.078f, 2.257f, 1.403f, 0.413f, 0.298f},  // 200.0 Hz
                {1.072f, 2.272f, 2.322f, 0.931f, 0.656f},  // 230.0 Hz
                {1.064f, 2.138f, 3.586f, 2.064f, 1.477f},  // 264.5 Hz
                {1.055f, 2.023f, 5.084f, 3.400f, 2.984f},  // 304.2 Hz
                {1.047f, 1.978f, 5.458f, 4.662f, 7.433f},  // 349.8 Hz
                {1.038f, 1.962f, 4.501f, 6.954f, 15.426f},  // 402.3 Hz
                {1.032f, 1.930f, 3.716f, 11.187f, 15.433f},  // 462.6 Hz
                {1.026f, 1.896f, 3.412f, 9.481f, 14.016f},  // 532.0 Hz
                {1.021f, 1.872f, 3.553f, 6.374f, 17.282f},  // 611.8 Hz
                {1.016f, 1.848f, 3.610f, 5.136f, 13.001f},  // 703.6 Hz
                {1.011f, 1.822f, 3.358f, 4.963f, 8.936f},  // 809.1 Hz
                {1.007f, 1.794f, 3.258f, 5.877f, 6.342f},  // 930.5 Hz
                {1.003f, 1.765f, 3.241f, 5.674f, 6.433f},  // 1070.1 Hz
                {1.000f, 1.738f, 3.129f, 4.927f, 8.446f},  // 1230.6 Hz
                {0.998f, 1.714f, 3.045f, 4.921f, 7.524f},  // 1415.1 Hz
                {0.996f, 1.694f, 2.932f, 4.880f, 6.916f},  // 1627.4 Hz
                {0.992f, 1.681f, 2.831f, 4.644f, 7.039f},  // 1871.5 Hz
                {0.994f, 1.670f, 2.740f, 4.452f, 6.379f},  // 2152.3 Hz
                {0.992f, 1.658f, 2.660f, 4.242f, 6.557f},  // 2475.1 Hz
                {0.991f, 1.650f, 2.605f, 4.076f, 6.070f},  // 2846.4 Hz
                {0.991f, 1.642f, 2.562f, 3.908f, 5.691f},  // 3273.3 Hz
                {0.990f, 1.637f, 2.516f, 3.768f, 5.531f},  // 3764.3 Hz
                {0.990f, 1.630f, 2.475f, 3.673f, 5.270f},  // 4328.9 Hz
                {0.989f, 1.625f, 2.442f, 3.583f, 5.047f},  // 4978.3 Hz
                {0.989f, 1.618f, 2.412f, 3.491f, 4.895f},  // 5725.0 Hz
                {0.988f, 1.613f, 2.387f, 3.413f, 4.762f},  // 6583.8 Hz
                {0.988f, 1.607f, 2.359f, 3.344f, 4.607f},  // 7571.4 Hz
                {0.988f, 1.605f, 2.338f, 3.293f, 4.496f},  // 8707.1 Hz
            },
            {   // Diode 303 (asymmetric diode ladder), measured against the Classic anchor
                {30.516f, 48.849f, 33.520f, 8.273f, 3.637f},  // 200.0 Hz
                {21.168f, 31.696f, 31.962f, 12.046f, 5.011f},  // 230.0 Hz
                {14.951f, 20.158f, 28.638f, 16.548f, 7.314f},  // 264.5 Hz
                {10.678f, 13.387f, 24.840f, 16.301f, 9.743f},  // 304.2 Hz
                {7.818f, 9.264f, 18.013f, 13.050f, 15.985f},  // 349.8 Hz
                {5.876f, 6.635f, 10.558f, 12.061f, 23.864f},  // 402.3 Hz
                {4.583f, 4.886f, 6.493f, 14.317f, 18.094f},  // 462.6 Hz
                {3.592f, 3.770f, 4.736f, 9.557f, 12.304f},  // 532.0 Hz
                {3.005f, 3.109f, 4.138f, 5.665f, 13.399f},  // 611.8 Hz
                {2.588f, 2.721f, 3.917f, 5.051f, 12.730f},  // 703.6 Hz
                {2.277f, 2.514f, 3.807f, 5.395f, 10.824f},  // 809.1 Hz
                {2.044f, 2.410f, 3.868f, 6.666f, 6.880f},  // 930.5 Hz
                {1.866f, 2.344f, 3.871f, 6.147f, 6.401f},  // 1070.1 Hz
                {1.725f, 2.275f, 3.687f, 5.488f, 9.460f},  // 1230.6 Hz
                {1.611f, 2.201f, 3.592f, 5.648f, 8.167f},  // 1415.1 Hz
                {1.521f, 2.133f, 3.468f, 5.537f, 7.649f},  // 1627.4 Hz
                {1.442f, 2.070f, 3.324f, 5.307f, 7.867f},  // 1871.5 Hz
                {1.377f, 2.014f, 3.189f, 5.111f, 7.201f},  // 2152.3 Hz
                {1.320f, 1.963f, 3.073f, 4.899f, 7.542f},  // 2475.1 Hz
                {1.270f, 1.915f, 2.988f, 4.731f, 7.160f},  // 2846.4 Hz
                {1.230f, 1.873f, 2.919f, 4.562f, 6.811f},  // 3273.3 Hz
                {1.193f, 1.838f, 2.850f, 4.398f, 6.658f},  // 3764.3 Hz
                {1.162f, 1.803f, 2.774f, 4.246f, 6.306f},  // 4328.9 Hz
                {1.136f, 1.768f, 2.693f, 4.074f, 5.976f},  // 4978.3 Hz
                {1.113f, 1.728f, 2.603f, 3.889f, 5.701f},  // 5725.0 Hz
                {1.095f, 1.690f, 2.517f, 3.714f, 5.420f},  // 6583.8 Hz
                {1.077f, 1.639f, 2.404f, 3.502f, 5.049f},  // 7571.4 Hz
                {1.065f, 1.597f, 2.309f, 3.330f, 4.752f},  // 8707.1 Hz
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
