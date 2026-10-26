#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "CoreOnly.h"
#include "ParamTable.h"
#include "PresetCore.h"
#include "StateXml.h"

#include "../dsp/Engine.h"
#include "../dsp/FatAnalog.h"
#include "../dsp/Limiter.h"
#include "../dsp/Saturation.h"
#include "../dsp/Sequence.h"
#include "../dsp/Delay.h"
#include "../dsp/Reverb.h"

namespace spiral303
{
    /** The plugin without a host around it: the parameters, the pattern, the
        clock and the audio path. This is the native processor's logic without
        JUCE's parameter tree, so that the WebAssembly build behaves the same. */
    class ProcessorCore
    {
    public:
        ProcessorCore();

        void prepare (double sampleRate);
        void reset();

        //==============================================================================
        // Parameters. setParam() is for the main thread (the interface, a
        // preset load); the value lands in the audio path at the next block.

        void setParam (int index, float value);
        /** For the audio thread: a value that arrives as a host event lands in
            the running patch straight away, the way the native build applies it
            at the start of the block. */
        void setParamImmediate (int index, float value);
        float paramValue (int index) const;

        //==============================================================================
        // The pattern and the gate.

        void setSequence (const Sequence& sequence);
        Sequence getSequence() const;
        void setGate (const GatePattern& gate);
        GatePattern getGate() const;

        //==============================================================================
        // Presets and state.

        void applyPreset (const PresetDef& preset);
        const std::string& currentPresetName() const { return presetName; }
        const std::string& presetNote() const { return note; }
        void setPresetNote (const std::string& text);

        PluginState getState() const;
        bool setState (const void* data, size_t size);

        //==============================================================================
        // Audio.

        /** Renders one block into the given channel pointers. The MIDI buffer
            carries the incoming notes and controllers. */
        void process (float* const* outputChannels, int numChannels, int numSamples,
                      juce::MidiBuffer& midi);

        //==============================================================================
        // What the interface shows.

        float takeMeterPeak();
        float meterRms() const { return meterRmsValue.load (std::memory_order_relaxed); }
        float limiterGain() const { return limiterReduction.load (std::memory_order_relaxed); }
        int playingStep() const { return playingStepValue.load (std::memory_order_relaxed); }
        int playingGateSector() const { return playingGateSectorValue.load (std::memory_order_relaxed); }
        bool sequencerRunning() const { return sequencerRunningValue.load (std::memory_order_relaxed); }
        bool gateActive() const { return paramValue (kGateOn) >= 0.5f; }

    private:
        Patch buildPatch (double sampleRate) const;
        void updateTransport (bool& running, double& bpm, double& beatPosition) const;
        void syncCustomStateAudioThread();

        mutable std::mutex paramLock;
        float values[kParamCount] {};
        float pendingValues[kParamCount] {};
        std::atomic<bool> pendingDirty { false };

        mutable std::mutex stateLock;
        Sequence sequenceCurrent;
        GatePattern gateCurrent;
        Sequence sequenceAudio;
        std::shared_ptr<const GatePattern> gateAudio;
        std::atomic<bool> sequenceDirty { true };
        std::atomic<bool> gateDirty { true };

        std::string presetName;
        std::string note;

        TbEngine engine;
        FatAnalog fatAnalog;
        FxDelay fxDelay;
        FxReverb fxReverb;
        Limiter limiter;

        std::atomic<float> meterPeakValue { 0.0f };
        std::atomic<float> meterRmsValue { 0.0f };
        std::atomic<float> limiterReduction { 1.0f };
        std::atomic<int> playingStepValue { -1 };
        std::atomic<int> playingGateSectorValue { -1 };
        std::atomic<bool> sequencerRunningValue { false };

        double sampleRate = 48000.0;
    };
}
