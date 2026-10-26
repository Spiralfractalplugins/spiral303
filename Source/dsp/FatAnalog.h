#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "../core/CoreInclude.h"

#include "Saturation.h"

/** Stereo-only thickener inspired by analog Moog-style circuit variations:
    independent detune via modulated delays, channel-dependent saturation,
    Haas micro-delays and low-level analog noise. Mono buffers are left
    untouched. */
class FatAnalog
{
public:
    void prepare (double sampleRateHz, int numChannels)
    {
        sr = sampleRateHz <= 1.0 ? 48000.0 : sampleRateHz;
        nCh = juce::jmax (1, numChannels);

        const int maxDelay = static_cast<int> (std::ceil (sr * 0.025)) + 8;
        delaySize = juce::jmax (64, maxDelay);
        delay.assign (static_cast<size_t> (delaySize), 0.0f);
        writePos = 0;

        noiseState = 0x9e3779b9u;
        lfoL = 0.0f;
        lfoR = juce::MathConstants<float>::pi * 0.37f;

        satL.reset();
        satR.reset();
        satL.setType (Saturation::Type::Triode);
        satR.setType (Saturation::Type::Drive);
        satL.prepare (sr);
        satR.prepare (sr);

        updateAmount (0.4f);
    }

    void reset()
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        writePos = 0;
        satL.reset();
        satR.reset();
    }

    void setAmount (float amount0to1)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount0to1);
        updateAmount (amount);
    }

    /** In-place. Stereo-only; single-channel buffers are passed through. */
    void process (juce::AudioBuffer<float>& buffer)
    {
        if (buffer.getNumChannels() < 2)
            return;

        const int n = buffer.getNumSamples();
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);

        const float incL = juce::MathConstants<float>::twoPi * (0.13f + amount * 0.11f) / static_cast<float> (sr);
        const float incR = juce::MathConstants<float>::twoPi * (0.19f + amount * 0.08f) / static_cast<float> (sr);

        const float depthSamples = (0.0015f + 0.0035f * amount) * static_cast<float> (sr);
        const float noiseAmount = (0.00015f + 0.00055f * amount);
        const float microLeft = sr * 0.0022f;
        const float microRight = sr * 0.0057f;

        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (left[i] + right[i]);
            writePos = (writePos + 1) % delaySize;
            delay[static_cast<size_t> (writePos)] = x;

            lfoL += incL;
            lfoR += incR;
            const float modL = std::sin (lfoL) * depthSamples;
            const float modR = std::sin (lfoR) * depthSamples;

            const float dl = readDelay (microLeft + modL);
            const float dr = readDelay (microRight - modR);

            // /2^31 over an unsigned value spans [0, 2): recentre on zero so the
            // noise adds no DC offset.
            const float nl = (static_cast<float> (nextNoise()) / 2147483647.0f - 1.0f) * (noiseAmount * 0.1f);
            const float nr = (static_cast<float> (nextNoise()) / 2147483647.0f - 1.0f) * (noiseAmount * 0.1f);

            float wetL = satL.process (dl);
            float wetR = satR.process (dr);

            // Keep the original mid energy and blend a much quieter widened side image.
            const float mid = x;
            const float side = 0.05f * (wetL - wetR);
            const float outL = mid + side + nl;
            const float outR = mid - side + nr;

            left[i] = outL;
            right[i] = outR;
        }
    }

private:
    void updateAmount (float a)
    {
        satL.setDrive (0.12f + 0.28f * a);
        satL.setMix (0.35f + 0.25f * a);
        satR.setDrive (0.18f + 0.33f * a);
        satR.setMix (0.40f + 0.20f * a);
    }

    float readDelay (float delaySamples) const
    {
        const float d = juce::jlimit (1.0f, static_cast<float> (delaySize - 2), delaySamples);
        const float pos = static_cast<float> (writePos) - d;
        const int i0 = static_cast<int> (std::floor (pos));
        const float frac = pos - static_cast<float> (i0);

        const int idx0 = (i0 % delaySize + delaySize) % delaySize;
        const int idx1 = ((i0 + 1) % delaySize + delaySize) % delaySize;
        return delay[static_cast<size_t> (idx0)] * (1.0f - frac)
             + delay[static_cast<size_t> (idx1)] * frac;
    }

    unsigned int nextNoise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return noiseState;
    }

    double sr = 48000.0;
    int nCh = 2;
    int delaySize = 1024;
    std::vector<float> delay;
    int writePos = 0;

    float amount = 0.4f;
    float lfoL = 0.0f, lfoR = 0.0f;
    unsigned int noiseState = 1u;

    Saturation satL, satR;
};
