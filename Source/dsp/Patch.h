#pragma once

#include <memory>

#include "Sequence.h"

/** Everything the voice and the clock need, assembled once per audio block.
    Kept in its own header so the MIDI-map tables can patch it without pulling
    in the whole engine. */
struct Patch
{
    double sampleRate = 48000.0;

    float wave = 0.5f;        // 0 sine, 0.5 saw, 1 square
    float cutoff = 800.0f;    // Hz, before envelope modulation
    float res = 0.5f;         // 0..1
    float env = 0.5f;         // how far the contour opens the filter
    float contour = 0.5f;     // decay length as a fraction of the step
    float accent = 0.75f;     // extra level and filter depth on accented steps
    float glide = 0.35f;      // slide time as a fraction of the step
    double attackSeconds = 0.0; // attack ramp length in seconds; 0 = instant

    double stepsPerBeat = 4.0; // host-grid steps per quarter; <= 0 skips anchoring
    int midiMap = 0;           // 0 off, 1 Launch Control, 2 Launch Control XL, 3 Push 2

    float drive = 0.0f;
    float satMix = 0.0f;
    int satType = 0;
    int analogMode = 0;
    int filterModel = 0;     // 0 Classic, 1 Diode Ladder, 2 Moog Ladder, 3 TPT SVF, 4 Sallen-Key, 5 One-Pole, 6 Vowel, 7 Diode 303
    float tone = 1.0f;        // 0 = 12 dB/oct single section, 1 = 24 dB/oct cascade
    float warmth = 0.5f;      // analog emulation strength

    double stepSamples = 48000.0 * 0.125;   // one clock step (a 16th at the current tempo)
    double swingSamples = 0.0;              // delay added to even-numbered steps
    double gateRatio = 1.0;                 // a gate sector lasts this many clock steps
    float gateMix = 1.0f;                   // dry/wet of the gate effect (0 = bypassed)
    float hiss = 0.0f;                      // analog noise floor strength (0 = off)

    // FX tail (delay, then the reverb). Zero mixes = bypass; the processors skip
    // the whole stage then, so every calibrated render of the bank stays bit-exact.
    float fxDelMix = 0.0f;                  // delay dry/wet (0 = bypass)
    double fxDelaySeconds = 0.75;           // echo time straight from BPM and the divider
    float fxDelFb = 0.35f;                  // feedback, already capped at 0.9
    double fxDelToneHz = 3000.0;            // darkening lowpass of the feedback path
    float fxRevMix = 0.0f;                  // reverb send level (0 = bypass)
    float fxRevDecay = 0.5f;                // room decay, 0..1

    // Oversampling of the saturator: 1 off, 2 or 4 times the host rate.
    int oversample = 1;

    const Sequence* sequence = nullptr;
    std::shared_ptr<const GatePattern> gate; // null = gate effect off
};
