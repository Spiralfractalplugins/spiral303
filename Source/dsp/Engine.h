#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "../core/CoreInclude.h"

#include "../PluginParameters.h"
#include "ResonantFilter.h"
#include "Oversampler.h"
#include "Saturation.h"
#include "Sequence.h"
#include "WaveOsc.h"

#include "Patch.h"
#include "MidiMap.h"

/** Slowly wandering analog imperfections: detune, cutoff drift, level flicker and
    clock jitter, one strength parameter to rule them all. Updated per audio block
    and interpolated so nothing zippers. */
class Drift
{
public:
    void prepare (double sampleRateHz) { sampleRate = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz; }

    void reset() { time = 0.0; for (int i = 0; i < 5; ++i) { value[i] = 0.0; targetState[i] = 0.0; slope[i] = 0.0; walkState[i] = 0.0; } }

    void setStrength (float w)
    {
        strength = w;
        // Cache the strength products, NOT the `1.0 + x` part: the render path
        // is chaotic (resonant feedback), and folding the constant into the
        // product changes the last ULP and with it the offline renders.
        kPitch = strength * 0.018;      // strength * 0.018 * value[0]
        kCutoff = strength * 0.10;      // 1.0 + kCutoff * value[1]
        kLevel = strength * 0.05;       // 1.0 + kLevel * value[2]
        kJitter = strength * 14.0;      // kJitter * value[3]
        kResonance = strength * 0.035;  // 1.0 + kResonance * value[4]
    }

    /** Call once at the start of a block. */
    void advance (int numSamples)
    {
        const double n = static_cast<double> (juce::jmax (1, numSamples));
        nextTarget (0, 0.62 * std::sin (0.37 * time) + 0.38 * walk (0), n);
        nextTarget (1, 0.55 * std::sin (0.11 * time + 1.7) + 0.45 * walk (1), n);
        nextTarget (2, 0.50 * std::sin (0.23 * time + 4.1) + 0.50 * walk (2), n);
        nextTarget (3, walk (3), n);
        nextTarget (4, 0.40 * std::sin (0.19 * time + 2.4) + 0.60 * walk (4), n);
        time += static_cast<double> (numSamples) / sampleRate;
        // Keep the phase accumulator in range: sin() of a large argument loses
        // precision and the pitch starts to wander after a few hours of audio.
        if (time > 628.0)
            time = std::fmod (time, 628.0);
    }

    double pitchOct()      noexcept { return kPitch * value[0]; }   // up to ~±31 cents
    double cutoffMul()     noexcept { return 1.0 + kCutoff * value[1]; }
    double level()         noexcept { return 1.0 + kLevel * value[2]; }
    double jitterSamples() noexcept { return kJitter * value[3]; }
    double resonanceMul()  noexcept { return 1.0 + kResonance * value[4]; } // analog filter-Q micro drift

private:
    void nextTarget (int index, double target, double n)
    {
        targetState[static_cast<size_t> (index)] = target;
        slope[static_cast<size_t> (index)] = (target - value[static_cast<size_t> (index)]) / n;
    }

    double walk (int index)
    {
        double& w = walkState[static_cast<size_t> (index)];
        w += rng.nextDouble() * 0.04 - 0.02;
        w = std::max (-1.0, std::min (1.0, w * 0.998));
        return w;
    }

    /** Called from the sample loop; slides each channel toward its block target.
        Unrolled: the loop loaded `slope` from memory twice per element and the
        bound-checking got in the way of `v += slope`, which is the whole body. */
    void tick() noexcept
    {
        auto slide = [] (double& v, double slope, double target)
        {
            v += slope;
            if ((slope > 0.0 && v > target) || (slope < 0.0 && v < target))
                v = target;
        };
        slide (value[0], slope[0], targetState[0]);
        slide (value[1], slope[1], targetState[1]);
        slide (value[2], slope[2], targetState[2]);
        slide (value[3], slope[3], targetState[3]);
        slide (value[4], slope[4], targetState[4]);
    }

    double sampleRate = 48000.0, time = 0.0, strength = 0.5;
    double kPitch = 0.5 * 0.018, kCutoff = 0.5 * 0.10, kLevel = 0.5 * 0.05,
           kJitter = 0.5 * 14.0, kResonance = 0.5 * 0.035; // match strength = 0.5
    double value[5] {}, targetState[5] {}, slope[5] {}, walkState[5] {};
    juce::Random rng { 0x5eed1a };

    friend class TbEngine;
};

/** Gate ramp: a linear fade with a floor on both times, so the envelope always
    reaches zero (and one) exactly instead of chopping the waveform or leaving a
    tail — that is what keeps the sector edges from clicking. */
class GateRamp
{
public:
    void prepare (double sampleRateHz) { sampleRate = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz; }

    void setTimes (float attackMs, float releaseMs)
    {
        openRate = rate (attackMs);
        closeRate = rate (releaseMs);
    }

    void open()  { opening = true; }
    void close() { opening = false; }

    float process()
    {
        const float rate = opening ? openRate : closeRate;
        value += opening ? rate : -rate;
        value = juce::jlimit (0.0f, 1.0f, value);
        return value;
    }

private:
    float rate (float ms) const
    {
        // Below ~1.5 ms the fade is shorter than a low note's period and clicks.
        const float clamped = juce::jmax (1.5f, ms);
        return 1.0f / (clamped * 0.001f * static_cast<float> (sampleRate));
    }

    double sampleRate = 48000.0;
    float openRate = 1.0f / (2.0f * 0.001f * 48000.0f), closeRate = 1.0f / (14.0f * 0.001f * 48000.0f);
    float value = 1.0f;
    bool opening = true;
};

/** Mono 303 voice: oscillator, contour envelope, resonant filter and slide. */
class TbVoice
{
public:
    /** Accent chain: consecutive accented steps charge the accent further, the way
        the 303's accent capacitor does. Each extra accent in a run adds this much to
        the accent level and the filter push; the run is capped. A run of one is the
        plain accent, so single accents keep their calibrated level exactly. */
    static constexpr float kAccentChainStep = 0.25f;
    static constexpr int kAccentChainMax = 4;

    static float accentChainGain (int run) noexcept
    {
        const int r = std::min (std::max (run, 1), kAccentChainMax);
        return 1.0f + kAccentChainStep * static_cast<float> (r - 1);
    }

    void prepare (double sampleRateHz)
    {
        const double sr = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz;
        osc.prepare (sampleRateHz);
        filter.prepare (sampleRateHz);
        sampleRate = sampleRateHz;
        edgeRate = static_cast<float> (1.0 / std::max (1.0, kEdgeSeconds * sr));
        edgeFallRate = static_cast<float> (1.0 / std::max (1.0, kEdgeFallSeconds * sr));
    }

    void setHypersaw (bool on) { osc.setHypersaw (on); }
    void setSaturation (Saturation* s) { saturation = s; }

    void noteOn (double freqHz, double prevFreqHz, const Patch& patch, bool accented, double glideSamples,
                 double velNorm = 1.0, int accentRun = 1)
    {
        if (active && edge > 0.0f)
        {
            // Something is still sounding. Walk it down to zero and fire this note
            // when the level gets there: re-attacking straight over a live tail
            // jumps the whole remaining amplitude in one sample, and a fast player
            // hears that as a click.
            queued.freqHz = freqHz;
            queued.prevFreqHz = prevFreqHz;
            queued.glideSamples = glideSamples;
            queued.velNorm = velNorm;
            queued.accented = accented;
            queued.accentRun = accentRun;
            queued.patch = patch;
            queued.waiting = true;
            falling = true;
            return;
        }
        startNote (freqHz, prevFreqHz, patch, accented, glideSamples, velNorm, accentRun);
    }

    /** Let whatever is sounding fade out and stay out (transport jumps). */
    void fadeOut() { queued.waiting = false; falling = true; }

private:
    struct QueuedNote
    {
        Patch patch;
        double freqHz = 0.0, prevFreqHz = 0.0, glideSamples = 0.0, velNorm = 1.0;
        bool accented = false, waiting = false;
        int accentRun = 1;
    };

    void startNote (double freqHz, double prevFreqHz, const Patch& patch, bool accented,
                    double glideSamples, double velNorm, int accentRun = 1)
    {
        const float chain = accentChainGain (accentRun);
        const float accentBoost = accented ? (1.0f + patch.accent * 0.6f) * chain : 1.0f;
        // Per-step velocity: a floor of 0.3 keeps very soft notes audible,
        // full velocity is exactly the legacy level.
        const float velScale = static_cast<float> (0.3 + 0.7 * std::max (0.0, std::min (1.0, velNorm)));
        osc.setMorph (patch.wave);
        osc.setFrequency (static_cast<float> (freqHz));
        filter.setModel (patch.filterModel);
        filter.setResonance (patch.res);
        filter.setSlope (patch.tone);

        if (glideSamples > 2.0 && prevFreqHz > 0.0 && freqHz > 0.0)
        {
            glideRate = std::pow (freqHz / prevFreqHz, 1.0 / glideSamples);
            gliding = true;
            lastFreq = prevFreqHz;
        }
        else
        {
            gliding = false;
            lastFreq = freqHz;
        }
        targetFreq = freqHz;

        // Volume 1.0 and a full accent still land under a host's 0 dBFS ceiling.
        // The original design kept a "Volume" knob at 0.7, which every factory
        // preset also carried, so the folded constant keeps those levels intact.
        static constexpr float voiceLevel = 0.7f;
        envStart = voiceLevel * accentBoost * 0.5f * velScale;
        const double contourSeconds = std::max (0.02, static_cast<double> (patch.contour))
                                      * patch.stepSamples * 2.0 / sampleRate;
        envDecay = std::exp (-1.0 / std::max (1.0e-4, contourSeconds * sampleRate));
        // Attack: 0 = the instant legacy attack; otherwise the envelope slides
        // up linearly to its peak, opening the filter while it rises.
        attackRemaining = patch.attackSeconds > 0.0
                              ? patch.attackSeconds * sampleRate : 0.0;
        attackStep = attackRemaining > 1.0 ? envStart / attackRemaining : 0.0;
        envValue = attackRemaining > 0.0 ? 0.0f : envStart;
        filterDepth = patch.env * (accented ? (1.0f + patch.accent * 0.9f) * chain : 1.0f)
                      * static_cast<float> (0.5 + 0.5 * std::max (0.0, std::min (1.0, velNorm)));
        held = false;
        active = true;
        // The note leaves from absolute zero. The filter keeps its state: the
        // resonant tail is what makes a running line sing, and the fade covers
        // the gap instead of chopping it.
        edge = 0.0f;
        falling = false;
    }

public:
    /** A tie extends the previous note instead of retriggering it. */
    void tie() { held = true; }

    bool isActive() const { return active; }
    float levelForScope() const { return envValue; }
    float oscPhase() const { return osc.getPhase(); }

    float process (const Patch& patch, Drift& drift)
    {
        if (! active)
            return 0.0f;

        // The edge fade: a ramp in each direction. While it sits at one the
        // product below is bit-identical to the bare envelope; the ramp only
        // exists for the samples around an attack and a cut.
        if (falling)
        {
            edge -= edgeFallRate;
            if (edge <= 0.0f)
            {
                edge = 0.0f;
                falling = false;
                if (queued.waiting)
                {
                    const QueuedNote next = queued;
                    queued.waiting = false;
                    startNote (next.freqHz, next.prevFreqHz, next.patch, next.accented,
                               next.glideSamples, next.velNorm, next.accentRun);
                }
                else
                {
                    envValue = 0.0f;
                    active = false;
                }
            }
        }
        else if (edge < 1.0f)
        {
            edge = std::min (1.0f, edge + edgeRate);
        }

        if (gliding)
        {
            lastFreq *= glideRate;
            if ((glideRate > 1.0 && lastFreq >= targetFreq) || (glideRate <= 1.0 && lastFreq <= targetFreq))
            {
                lastFreq = targetFreq;
                gliding = false;
            }
        }

        const double f = std::max (1.0, std::min (patch.sampleRate * 0.22,
                                                  lastFreq * std::exp2 (drift.pitchOct())));
        osc.setFrequency (static_cast<float> (f));
        osc.setMorph (patch.wave);
        filter.setModel (patch.filterModel);
        const float resMul = (patch.warmth > 0.0f) ? static_cast<float> (drift.resonanceMul()) : 1.0f;
        filter.setResonance (juce::jlimit (0.0f, 1.0f, patch.res * resMul));

        if (! held)
        {
            if (attackRemaining > 0.0)
            {
                // Rising edge of the attack: open the level up to its peak.
                envValue = juce::jmin (envStart, envValue + static_cast<float> (attackStep));
                attackRemaining -= 1.0;
            }
            else
            {
                envValue *= envDecay;
            }
            if (envValue < 1.0e-4f)
            {
                envValue = 0.0f;
                // A queued note still has to be fired: the fade owns the silence.
                if (! queued.waiting)
                    active = false;
            }
        }

        const float norm = envValue / std::max (1.0e-5f, envStart);
        const float cutoff = patch.cutoff * static_cast<float> (drift.cutoffMul())
                             + norm * norm * filterDepth * 5200.0f;

        float out = filter.process (osc.process(), std::max (30.0f, cutoff));
        return out * envValue * shapedEdge() * static_cast<float> (drift.level());
    }

private:
    /** Half-cosine of the ramp position. A linear ramp is a corner in the
        amplitude curve and corners are broadband: the shaped version leaves the
        fade with zero slope at both ends, so the same milliseconds move far less
        energy into the band the ear hears as a click. Exactly one at the ends,
        which keeps the steady-state render bit-identical. */
    float shapedEdge() const
    {
        if (edge <= 0.0f) return 0.0f;
        if (edge >= 1.0f) return 1.0f;
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * edge);
    }

    WaveOsc osc;
    ResonantFilter filter;
    Saturation* saturation = nullptr;
    double sampleRate = 48000.0;

    /** Long enough that the fade is not a step, short enough to keep the fastest
        line's attack. Same reasoning as the gate ramp's 1.5 ms floor. The cut is
        quicker than the rise: the outgoing note only has to get out of the way,
        and every millisecond of that gap is a hole the filter's free ring falls
        into, which thins the note more than the fade-in does. */
    static constexpr double kEdgeSeconds = 0.002, kEdgeFallSeconds = 0.001;
    float edge = 0.0f,
          edgeRate = 1.0f / static_cast<float> (kEdgeSeconds * 48000.0),
          edgeFallRate = 1.0f / static_cast<float> (kEdgeFallSeconds * 48000.0);
    bool falling = false;
    QueuedNote queued;

    double lastFreq = 110.0, targetFreq = 110.0, glideRate = 1.0;
    bool gliding = false;

    float envValue = 0.0f, envStart = 0.5f, envDecay = 0.999f, filterDepth = 0.5f;
    double attackRemaining = 0.0, attackStep = 0.0;
    bool held = false, active = false;
};

/** The pattern clock: runs the sequence in 16th-note steps, applies swing, analog
    jitter and the circular gate, and renders the single voice. */
class TbEngine
{
public:
    void prepare (double sampleRateHz)
    {
        voice.prepare (sampleRateHz);
        drift.prepare (sampleRateHz);
        gate.prepare (sampleRateHz);
        baseSampleRate = sampleRateHz > 1.0 ? sampleRateHz : 48000.0;
        saturator.prepare (baseSampleRate * oversampleFactor);
        osOuter.reset();
        osInner.reset();
        // The voice's saturation pointer goes BEFORE the resonant filter, so a
        // closed filter silences the whole signal (saturation included).
        voice.setSaturation (&saturator);

        // One-pole highpass at ~18 Hz: the classic AC coupling in front of the output.
        dcCoef = static_cast<float> (std::exp (-2.0 * juce::MathConstants<double>::pi * 18.0 / sampleRateHz));
    }

    /** Pattern start on the host's step grid: the local sample counter is set
        so the next step boundary lands exactly on the host's next step, and the
        line starts from step 1. The "analog" jitter stays on the trigger delay
        afterwards - never on where the line begins. */
    void resyncToHostBeat (double midiBeatPosition, const Patch& patch)
    {
        if (patch.stepsPerBeat <= 0)
            return; // cannot anchor triplet-ish divisions to the ppq grid

        reset();
        stepCounter = 0;
        const double beatFrac = midiBeatPosition - std::floor (midiBeatPosition);
        double stepFrac = beatFrac * patch.stepsPerBeat;
        stepFrac -= std::floor (stepFrac);
        // The local counter counts up and a step fires when it reaches the step
        // length, so put it exactly where the host clock is inside the current
        // step. Being exactly on a boundary fires the line immediately.
        fractional = stepFrac <= 0.0 ? patch.stepSamples : stepFrac * patch.stepSamples;
    }

    void reset()
    {
        voice.fadeOut();
        patternIndex = 0;
        stepCounter = 0;
        fractional = 0.0;
        lastMidiBeat = -1.0;
        lastPlayedFreq = 0.0;
        pendingTrigger = false;
        dcPrevious = 0.0f;
        dcState = 0.0f;
        drift.reset();
        accentChain = 0;
        pendingRun = 1;
        oversampleFactor = 1;
        osOuter.reset(); osInner.reset();
    }

    /** Renders the running sequencer into the buffer (adds to it). */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const Patch& patch,
                  bool running, double midiBeatPosition)
    {
        const int numSamples = buffer.getNumSamples();
        // MIDI-map CC latches shade the whole block through a local copy. A
        // knob's value latches across blocks (hardware knobs are absolute),
        // but switching maps must reset the latch or an old hardware move
        // keeps hijacking the parameters.
        Patch effective = patch;
        ccLatch.setMap (effective.midiMap);
        MidiMapTables::applyCcOverrides (ccLatch, effective);
        const Patch& patched = effective;
        setOversampling (patched.oversample);

        const double stepSamples = (std::isfinite (patched.stepSamples) && patched.stepSamples > 1.0)
                                      ? std::max (4.0, patched.stepSamples) : 48000.0 * 0.125;

        drift.setStrength (patched.warmth);
        voice.setHypersaw (patched.analogMode == 2);
        drift.advance (numSamples);
        saturator.setType (static_cast<Saturation::Type> (juce::jlimit (0, 11, patched.satType)));
        saturator.setDrive (patched.drive);
        saturator.setMix (patched.satMix);
        cutoffOverride = patched.cutoff;
        resonanceOverride = patched.res;

        // Restart the pattern when the host transport jumped backwards or when
        // playback starts: both moments must begin the line on the host's own
        // step grid, not on wherever the local sample counter happened to sit.
        const bool startOnset = running && ! wasRunning && midiBeatPosition >= 0.0;
        const bool jumped = running && midiBeatPosition >= 0.0 && lastMidiBeat >= 0.0
                            && std::fabs (midiBeatPosition - lastMidiBeat) > 0.5;
        if (startOnset || jumped)
            resyncToHostBeat (midiBeatPosition, patched);
        if (midiBeatPosition >= 0.0)
            lastMidiBeat = midiBeatPosition;
        else if (startOnset)
            lastMidiBeat = -1.0;
        wasRunning = running;

        applyMidi (patched, midi, running);

        float* ch = buffer.getWritePointer (0);
        double position = fractional;

        // Block invariants, hoisted out of the sample loop. Bit-exact: the same
        // expressions evaluated once instead of once per sample.
        const bool gateActive = patched.gate != nullptr;
        const float gateWet = gateActive ? juce::jlimit (0.0f, 1.0f, patched.gateMix) : 0.0f;
        const float gateDry = 1.0f - gateWet;
        const bool hissActive = patched.hiss > 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            while (position >= stepSamples && running)
            {
                position -= stepSamples;
                onStepBoundary (patched, true);
            }

            // Keep the sample clock ticking when stopping so playback can resume, but
            // do not advance steps or the gate while no host is rolling.
            if (! running)
                position = std::min (position, stepSamples);

            if (pendingTrigger && i >= pendingDelay)
            {
                firePendingNote (patched);
                pendingTrigger = false;
            }

            for (int e = 0; e < liveCount; ++e)
            {
                LiveNote& ln = liveNotes[static_cast<size_t> (e)];
                if (! ln.fired && i >= ln.offset)
                {
                    ln.fired = true;
                    Patch withOverrides = patched;
                    withOverrides.cutoff = cutoffOverride;
                    withOverrides.res = resonanceOverride;
                    voice.noteOn (ln.freqHz, 0.0, withOverrides, ln.accented, 0.0, ln.velNorm);
                }
            }

            // The voice renders while the sequencer runs and while live MIDI plays;
            // it returns silence by itself once its envelope has decayed.
            float sample = voice.process (patched, drift);

            if (hissActive)
                sample += static_cast<float> (hissValue() * patched.hiss * 0.0012);

            sample = saturateSample (sample);

            if (gateActive)
                sample *= gateDry + gateWet * gate.process();

            // The 303 is AC-coupled; without this the rectified saw from the
            // saturator rides out as DC and the speakers buzz.
            const float blocked = dcCoef * (dcState + sample - dcPrevious);
            dcPrevious = sample;
            dcState = blocked;

            ch[i] += blocked;
            position += 1.0;
            drift.tick();
        }

        fractional = position;

        // Engine currently renders a single mono stream; mirror it onto every
        // channel so the plugin plays back in stereo on the host's side.
        if (buffer.getNumChannels() > 1)
        {
            for (int c = 1; c < buffer.getNumChannels(); ++c)
                juce::FloatVectorOperations::copy (buffer.getWritePointer (c),
                                                  buffer.getWritePointer (0),
                                                  numSamples);
        }
    }

    void setGateTimes (float attackMs, float releaseMs) { gate.setTimes (attackMs, releaseMs); }

    /** Oversampling covers the saturator: the stage that makes new harmonics when
        the drive is up, and the one that aliases. The ladder filter keeps running
        at the host rate. Off by default, and off takes the original path exactly. */
    void setOversampling (int factor)
    {
        const int f = factor == 4 ? 4 : (factor == 2 ? 2 : 1);
        if (f == oversampleFactor)
            return;
        oversampleFactor = f;
        // The saturator is fed at the rate it runs at, so its time constants (tape,
        // drive slew) follow the new rate. Changing it resets its state.
        saturator.prepare (baseSampleRate * f);
        osOuter.reset();
        osInner.reset();
    }

    float saturateSample (float x)
    {
        if (oversampleFactor == 2)
            return osOuter.process (x, [this] (float v) { return saturator.process (v); });
        if (oversampleFactor == 4)
            return osOuter.process (x, [this] (float v)
            {
                return osInner.process (v, [this] (float w) { return saturator.process (w); });
            });
        return saturator.process (x);
    }

    /** Step index the pattern is on, for the GUI playhead. */
    int getPatternIndex() const { return patternIndex; }
    int getStepCounter() const { return stepCounter; }
    /** Gate sector the wheel is currently over; only meaningful while a gate is engaged. */
    int getGateSector() const { return gateSector; }
    float scopeLevel() const { return voice.levelForScope(); }
    float scopePhase() const { return voice.oscPhase(); }

private:
    void applyMidi (const Patch& patch, juce::MidiBuffer& midi, bool running)
    {
        liveCount = 0;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
            {
                if (running)
                    baseNoteShift = msg.getNoteNumber() - defaultBaseNote;
                else if (liveCount < kMaxLiveNotes)
                {
                    LiveNote& ln = liveNotes[static_cast<size_t> (liveCount)];
                    ln.offset = juce::jmax (0, metadata.samplePosition);
                    ln.freqHz = midiToFreq (msg.getNoteNumber());
                    ln.accented = msg.getVelocity() >= 100;
                    ln.velNorm = static_cast<double> (msg.getVelocity()) / 127.0;
                    ln.fired = false;
                    ++liveCount;
                }
            }
            else if (msg.isController())
            {
                const auto target = MidiMapTables::targetForController (patch.midiMap, msg.getControllerNumber());
                if (target != MidiMapTables::CcNone)
                    ccLatch.store (target, msg.getControllerValue() / 127.0f);
            }
        }
        midi.clear();
    }

    /** Latches the note of the step that just began; swing and jitter delay when it fires. */
    void onStepBoundary (const Patch& patch, bool running)
    {
        if (! running)
            return;
        const Sequence* seq = patch.sequence;
        if (seq == nullptr || seq->numSteps() <= 0)
            return;

        const int i = patternIndex % seq->numSteps();

        pendingTrigger = false;
        pendingDelay = 0;

        if (running)
        {
            if (seq->isNote (i))
            {
                pendingTrigger = true;
                pendingFreq = midiToFreq (seq->midiNote (i) + baseNoteShift);
                pendingAccent = seq->hasAccent (i);
                // A run of accents charges up; a plain note or a rest ends it.
                accentChain = pendingAccent ? std::min (accentChain + 1, TbVoice::kAccentChainMax) : 0;
                pendingRun = std::max (1, accentChain);
                pendingVel = seq->velocityOf (i);
                pendingGlideSamples = seq->hasSlide (i)
                    ? patch.stepSamples * (0.05 + patch.glide * 0.95) : 0.0;

                if (patternIndex % 2 == 1)
                    pendingDelay += static_cast<int> (patch.swingSamples);
                pendingDelay += static_cast<int> (std::abs (drift.jitterSamples()));
            }
            else if (seq->isTie (i) && lastPlayedFreq > 0.0)
            {
                voice.tie();
            }
            else
            {
                lastPlayedFreq = 0.0;
                accentChain = 0;
            }
        }

        if (patch.gate != nullptr)
        {
            const int nSectors = patch.gate != nullptr ? jmax (1, patch.gate->numSectors) : 16;
            const double ratio = std::max (0.25, patch.gateRatio);
            const int raw = static_cast<int> (static_cast<double> (stepCounter) / ratio) % nSectors;
            const int rot = ((patch.gate != nullptr ? patch.gate->rotation : 0) % nSectors
                             + nSectors) % nSectors;
            gateSector = (raw + rot) % nSectors;
            patch.gate->isClosed (gateSector) ? gate.close() : gate.open();
        }

        ++stepCounter;
        patternIndex = (patternIndex + 1) % Sequence::kMaxSteps;
    }

    void firePendingNote (const Patch& patch)
    {
        Patch withOverrides = patch;
        withOverrides.cutoff = cutoffOverride;
        withOverrides.res = resonanceOverride;

        voice.noteOn (pendingFreq, pendingGlideSamples > 2.0 ? lastPlayedFreq : 0.0,
                      withOverrides, pendingAccent, pendingGlideSamples, pendingVel, pendingRun);
        lastPlayedFreq = pendingFreq;
    }

    double midiToFreq (int note) const { return 440.0 * std::exp2 ((jlimit (12, 108, note) - 69) / 12.0); }

    /** Filtered white noise: a one-pole lowpass around 4 kHz, so the floor hisses
        like tape instead of whistling at the aliasing tones of a synthesized sine. */
    float hissValue()
    {
        const double w = hissRng.nextDouble() * 2.0 - 1.0;
        hissState = 0.85 * hissState + 0.15 * w;
        return static_cast<float> (hissState * 1.8);
    }

    struct LiveNote
    {
        int offset = 0;
        double freqHz = 0.0;
        bool accented = false, fired = false;
        double velNorm = 1.0;
    };

    TbVoice voice;
    Drift drift;
    GateRamp gate;
    Saturation saturator;
    Oversampler2x osOuter, osInner;
    int oversampleFactor = 1;
    double baseSampleRate = 48000.0;

    static constexpr int kMaxLiveNotes = 8;
    LiveNote liveNotes[kMaxLiveNotes];
    int liveCount = 0;
    double hissState = 0.0;
    juce::Random hissRng { 0x4b15e };
    float dcCoef = 0.9976f, dcPrevious = 0.0f, dcState = 0.0f;

    double fractional = 0.0, lastMidiBeat = -1.0, lastPlayedFreq = 0.0;
    bool wasRunning = false;
    int patternIndex = 0, stepCounter = 0, gateSector = 0;
    int baseNoteShift = 0;
    float hostCutoff = -1.0f, hostResonance = -1.0f;
    float cutoffOverride = 800.0f, resonanceOverride = 0.5f;

    bool pendingTrigger = false, pendingAccent = false;
    int pendingDelay = 0;
    int accentChain = 0, pendingRun = 1;
    double pendingFreq = 0.0, pendingGlideSamples = 0.0, pendingVel = 1.0;

    /** MIDI-map latch: the last seen controller values, kept across blocks
        because hardware knobs are absolute. */
    struct CcSlot { float value = 0.0f; bool seen = false; };
    MidiMapTables::Latch ccLatch;

    static constexpr int defaultBaseNote = 36;
};
