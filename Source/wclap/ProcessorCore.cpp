#include "ProcessorCore.h"

#include <cmath>

namespace spiral303
{
    namespace
    {
        const double kClockDivisionSteps[6] = { 4.0, 8.0 / 3.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0 };
        const double kGateDivisionSteps[4] = { 1.0, 2.0, 0.5, 2.0 / 3.0 };

        /** The same C minor line the Init preset carries, for a first open. */
        const char* defaultSequenceText()
        {
            return "v1:36:2:640,128,388,128,642,132,389,1,647,128,385,128,642,388,131,132";
        }

        float decibelsToGain (float decibels)
        {
            // JUCE's Decibels::decibelsToGain, which the native build uses.
            return decibels > -100.0f ? std::pow (10.0f, decibels * 0.05f) : 0.0f;
        }
    }

    ProcessorCore::ProcessorCore()
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            values[i] = static_cast<float> (paramDef (i).defaultValue);
            pendingValues[i] = values[i];
        }

        sequenceCurrent = Sequence::decode (juce::String (defaultSequenceText()));
        sequenceAudio = sequenceCurrent;
        tempoBpmValue.store (static_cast<double> (values[kBpm]), std::memory_order_relaxed);
    }

    void ProcessorCore::prepare (double newSampleRate)
    {
        sampleRate = (newSampleRate > 1.0 && std::isfinite (newSampleRate)) ? newSampleRate : 48000.0;
        engine.prepare (sampleRate);
        midiClock.prepare();
        fatAnalog.prepare (sampleRate, 2);
        fxDelay.prepare (sampleRate);
        fxReverb.prepare (sampleRate);
        limiter.prepare (sampleRate);
    }

    void ProcessorCore::reset()
    {
        engine.reset();
        fatAnalog.reset();
        fxDelay.reset();
        fxReverb.reset();
        limiter.reset();
        meterPeakValue.store (0.0f, std::memory_order_relaxed);
        meterRmsValue.store (0.0f, std::memory_order_relaxed);
        limiterReduction.store (1.0f, std::memory_order_relaxed);
    }

    //==============================================================================
    void ProcessorCore::setParam (int index, float value)
    {
        if (index < 0 || index >= kParamCount)
            return;

        const std::lock_guard<std::mutex> lock (paramLock);
        pendingValues[index] = value;
        pendingDirty.store (true, std::memory_order_release);
    }

    void ProcessorCore::setParamImmediate (int index, float value)
    {
        if (index < 0 || index >= kParamCount)
            return;

        const std::lock_guard<std::mutex> lock (paramLock);
        values[index] = value;
        // The pending copy has to follow, or the next block would restore the
        // older value the interface left there and lose this one.
        pendingValues[index] = value;
    }

    float ProcessorCore::paramValue (int index) const    {
        if (index < 0 || index >= kParamCount)
            return 0.0f;

        const std::lock_guard<std::mutex> lock (paramLock);
        return values[index];
    }

    float ProcessorCore::uiParamValue (int index) const
    {
        if (index < 0 || index >= kParamCount)
            return 0.0f;

        const std::lock_guard<std::mutex> lock (paramLock);
        return pendingValues[index];
    }

    //==============================================================================
    void ProcessorCore::setSequence (const Sequence& sequence)
    {
        {
            const std::lock_guard<std::mutex> lock (stateLock);
            sequenceCurrent = sequence;
        }
        sequenceDirty.store (true, std::memory_order_release);
    }

    Sequence ProcessorCore::getSequence() const
    {
        const std::lock_guard<std::mutex> lock (stateLock);
        return sequenceCurrent;
    }

    void ProcessorCore::setGate (const GatePattern& gate)
    {
        {
            const std::lock_guard<std::mutex> lock (stateLock);
            gateCurrent = gate;
        }
        gateDirty.store (true, std::memory_order_release);
    }

    GatePattern ProcessorCore::getGate() const
    {
        const std::lock_guard<std::mutex> lock (stateLock);
        return gateCurrent;
    }

    //==============================================================================
    void ProcessorCore::applyPreset (const PresetDef& preset)
    {
        for (const auto& value : preset.values)
        {
            const int index = paramIndexForId (value.paramId.c_str());
            if (index >= 0)
                setParam (index, value.rawValue);
        }

        if (! preset.sequence.empty())
            setSequence (Sequence::decode (juce::String (preset.sequence)));

        if (! preset.gate.empty())
            setGate (GatePattern::decode (juce::String (preset.gate)));

        presetName = preset.name;
        note = preset.comment;
    }

    void ProcessorCore::setPresetNote (const std::string& text)
    {
        note = text;
    }

    PluginState ProcessorCore::getState() const
    {
        PluginState state;

        {
            const std::lock_guard<std::mutex> lock (paramLock);
            for (int i = 0; i < kParamCount; ++i)
                state.values[i] = values[i];
        }

        {
            const std::lock_guard<std::mutex> lock (stateLock);
            state.sequence = sequenceCurrent.encode().raw();
            state.gate = gateCurrent.encode().raw();
        }

        state.presetNote = note;
        return state;
    }

    bool ProcessorCore::setState (const void* data, size_t size)
    {
        PluginState state;
        if (! decodeState (data, size, state))
            return false;

        {
            const std::lock_guard<std::mutex> lock (paramLock);
            for (int i = 0; i < kParamCount; ++i)
            {
                values[i] = state.values[i];
                pendingValues[i] = state.values[i];
            }
        }
        pendingDirty.store (false, std::memory_order_release);
        tempoBpmValue.store (static_cast<double> (state.values[kBpm]), std::memory_order_relaxed);

        if (! state.sequence.empty())
            setSequence (Sequence::decode (juce::String (state.sequence)));

        if (! state.gate.empty())
            setGate (GatePattern::decode (juce::String (state.gate)));

        note = state.presetNote;
        presetName.clear();
        return true;
    }

    //==============================================================================
    void ProcessorCore::syncCustomStateAudioThread()
    {
        if (sequenceDirty.exchange (false, std::memory_order_acq_rel))
        {
            const std::lock_guard<std::mutex> lock (stateLock);
            sequenceAudio = sequenceCurrent;
        }

        if (gateDirty.exchange (false, std::memory_order_acq_rel))
        {
            const std::lock_guard<std::mutex> lock (stateLock);
            gateAudio = std::make_shared<GatePattern> (gateCurrent);
        }
    }

    Patch ProcessorCore::buildPatch (double rate, double tempo) const
    {
        Patch patch;
        patch.sampleRate = (rate > 1.0 && std::isfinite (rate)) ? rate : 48000.0;

        patch.wave = paramValue (kWave);
        patch.cutoff = paramValue (kCutoff);
        patch.res = paramValue (kRes);
        patch.env = std::max (Patch::kEnvFloor, paramValue (kEnv));
        patch.contour = paramValue (kContour);
        patch.accent = paramValue (kAccent);
        patch.glide = paramValue (kGlide);
        patch.drive = paramValue (kDrive);
        patch.satMix = paramValue (kSatMix);
        patch.tone = paramValue (kTone);

        const bool analogEnabled = paramValue (kAnalogOn) >= 0.5f;
        patch.warmth = analogEnabled ? paramValue (kWarmth) : 0.0f;
        patch.hiss = analogEnabled && paramValue (kAnalogHiss) >= 0.5f ? paramValue (kWarmth) : 0.0f;

        patch.satType = static_cast<int> (juce::jlimit (0.0f, 11.0f, paramValue (kSatType)));
        patch.analogMode = static_cast<int> (juce::jlimit (0.0f, 2.0f, paramValue (kAnalogMode)));
        patch.filterModel = static_cast<int> (juce::jlimit (0.0f, 7.0f, paramValue (kFilterType)));

        const int divIndex = static_cast<int> (juce::jlimit (0.0f, 5.0f, paramValue (kClockDiv)));
        const double bpm = juce::jlimit (20.0, 400.0, tempo);
        const double sixteenth = 60.0 / bpm / 4.0;
        patch.stepSamples = std::max (4.0, sixteenth * kClockDivisionSteps[static_cast<size_t> (divIndex)] * rate);
        patch.swingSamples = patch.stepSamples * 0.5 * paramValue (kSwing);

        {
            // Steps per quarter for the host-grid anchoring; 0 when the division
            // does not fit a whole number of steps into a beat.
            const double rawStepsPerBeat = (sixteenth * rate) / patch.stepSamples * 4.0;
            const int wholeSteps = static_cast<int> (std::lround (rawStepsPerBeat));
            patch.stepsPerBeat = std::abs (rawStepsPerBeat - wholeSteps) < 0.01
                                     ? static_cast<double> (wholeSteps) : -1.0;
        }

        const int gateIndex = static_cast<int> (juce::jlimit (0.0f, 3.0f, paramValue (kGateDiv)));
        patch.gateRatio = kGateDivisionSteps[static_cast<size_t> (gateIndex)];
        patch.gateMix = paramValue (kGateMix);

        // FX tail: the same expressions the native buildPatch runs, so a preset
        // sounds identical in either build.
        patch.fxDelMix = paramValue (kFxDelMix);
        patch.fxDelaySeconds = 60.0 / bpm * ParamCurves::delayDivBeats (
            static_cast<int> (juce::jlimit (0.0f, 4.0f, paramValue (kFxDelDiv))));
        patch.fxDelFb = juce::jlimit (0.0f, 0.9f, paramValue (kFxDelFb));
        patch.fxDelToneHz = ParamCurves::delayToneToHz (paramValue (kFxDelTone));
        patch.fxRevMix = paramValue (kFxRevMix);
        patch.fxRevDecay = juce::jlimit (0.0f, 1.0f, paramValue (kFxRevDecay));
        {
            static const int kOversampleFactors[3] = { 1, 2, 4 };
            patch.oversample = kOversampleFactors[static_cast<int> (juce::jlimit (0.0f, 2.0f, paramValue (kOversample)))];
        }

        patch.attackSeconds = ParamCurves::attackToSeconds (paramValue (kAttack));
        patch.midiMap = static_cast<int> (juce::jlimit (0.0f, 3.0f, paramValue (kMidiMap)));

        patch.sequence = &sequenceAudio;
        patch.gate = (gateActive() && gateAudio != nullptr) ? gateAudio : nullptr;
        return patch;
    }

    void ProcessorCore::updateTransport (bool& running, double& bpm, double& beatPosition,
                                         const HostTransport* transport)
    {
        bpm = static_cast<double> (paramValue (kBpm));
        beatPosition = -1.0;

        const int syncIndex = static_cast<int> (juce::jlimit (0.0f, 2.0f, paramValue (kSyncMode)));
        const int startModeIndex = static_cast<int> (juce::jlimit (0.0f, 1.0f, paramValue (kStartMode)));
        const bool manualPlaying = paramValue (kManualPlay) >= 0.5f;
        midiClock.beforeBlock (syncIndex, startModeIndex);

        if (syncIndex == 0)
        {
            // clap_process_t::transport carries the host's transport for this
            // block: whether it rolls, its tempo, and the song position in
            // beats. The PLAY button and the BPM knob are the manual mode's.
            if (transport != nullptr)
            {
                if (transport->bpm > 20.0 && transport->bpm < 400.0)
                    bpm = transport->bpm;
                running = transport->playing;
                if (transport->playing && transport->hasBeats)
                    beatPosition = transport->beats;
            }
            else
            {
                // A free-running host names no transport, so the clock has
                // nothing to hold on to.
                running = false;
            }
            // The host owns the run line here: the note trigger stands down.
            midiClock.dropTrigger();
        }
        else if (syncIndex == 1)
        {
            midiClock.evaluateManual (manualPlaying, bpm, running, bpm, beatPosition);
        }
        else
        {
            midiClock.evaluateClock (bpm, running, bpm, beatPosition);
        }

        // SEQ OFF turns this into a plain MIDI instrument.
        if (paramValue (kSeqOn) < 0.5f)
        {
            running = false;
            beatPosition = -1.0;
        }

        tempoBpmValue.store (bpm, std::memory_order_relaxed);
    }

    //==============================================================================
    void ProcessorCore::process (float* const* outputChannels, int numChannels, int numSamples,
                                 juce::MidiBuffer& midi, const HostTransport* transport)
    {
        if (numChannels <= 0 || numSamples <= 0 || outputChannels == nullptr)
            return;

        // The main thread's parameter changes land here, at the block boundary.
        {
            const std::lock_guard<std::mutex> lock (paramLock);
            if (pendingDirty.exchange (false, std::memory_order_acq_rel))
            {
                for (int i = 0; i < kParamCount; ++i)
                    values[i] = pendingValues[i];
            }
        }

        syncCustomStateAudioThread();

        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        buffer.clear();

        const float gain = decibelsToGain (paramValue (kMasterGain));

        // The incoming stream speaks before the transport decides: Start/Stop
        // and the 24-PPQ clock steer the Midi Clock sync, and a note-on while
        // stopped can raise the run line (Start mode = Note Trigger). The
        // engine consumes the same buffer afterwards.
        const int syncIndex = static_cast<int> (juce::jlimit (0.0f, 2.0f, paramValue (kSyncMode)));
        const int startModeIndex = static_cast<int> (juce::jlimit (0.0f, 1.0f, paramValue (kStartMode)));
        midiClock.setSampleRate (sampleRate);
        for (const auto& metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isMidiStart())
                midiClock.start();
            else if (msg.isMidiContinue())
                midiClock.cont();
            else if (msg.isMidiStop())
            {
                midiClock.halt();
                midiClock.dropTrigger();
            }
            else if (msg.isMidiClock())
                midiClock.tick (metadata.samplePosition);
            else if (msg.isNoteOn())
                midiClock.noteOn (startModeIndex != 0 && syncIndex != 0,
                                  midiClock.clockLineUp (syncIndex,
                                      paramValue (kManualPlay) >= 0.5f),
                                  static_cast<double> (paramValue (kBpm)));
        }
        midiClock.advance (numSamples, sampleRate);

        // The transport first: with Sync on Host its tempo is the clock's tempo,
        // and the patch is built on it.
        bool running = true;
        double bpm = 130.0, beats = -1.0;
        updateTransport (running, bpm, beats, transport);

        Patch patch = buildPatch (sampleRate, bpm);
        engine.setGateTimes (paramValue (kGateAtk), paramValue (kGateRel));

        sequencerRunningValue.store (running, std::memory_order_relaxed);

        engine.process (buffer, midi, patch, running, beats);

        const int numSteps = sequenceAudio.numSteps();
        playingStepValue.store (running && numSteps > 0
                                    ? engine.getPatternIndex() % numSteps : -1,
                                std::memory_order_relaxed);
        playingGateSectorValue.store (gateActive() && patch.gate != nullptr
                                          ? engine.getGateSector() : -1,
                                      std::memory_order_relaxed);

        // FX tail, in the same order the native processBlock applies it:
        // the delay first, then the room; both mix-0-bypassed by default so
        // the calibrated renders stay bit-exact.
        if (patch.fxDelMix > 0.0f)
        {
            fxDelay.setParameters (patch.fxDelaySeconds, patch.fxDelFb,
                                   patch.fxDelToneHz, patch.fxDelMix);
            float* fxChannels[2] { nullptr, nullptr };
            const int fxCount = numChannels < 2 ? numChannels : 2;
            for (int c = 0; c < fxCount; ++c)
                fxChannels[c] = buffer.getWritePointer (c);
            fxDelay.process (fxChannels, numChannels, numSamples);
        }
        if (patch.fxRevMix > 0.0f)
        {
            fxReverb.setDecay (patch.fxRevDecay);
            const float* rvRead[2] { nullptr, nullptr };
            float* rvWrite[2] { nullptr, nullptr };
            const int rvCount = numChannels < 2 ? numChannels : 2;
            for (int c = 0; c < rvCount; ++c)
            {
                rvRead[c] = buffer.getReadPointer (c);
                rvWrite[c] = buffer.getWritePointer (c);
            }
            // Mono tail fed from the mid channel: the stereo sides keep their
            // width and the reverb never doubles the engine's own stereo.
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = rvCount >= 2
                    ? 0.5f * (rvRead[0][i] + rvRead[1][i])
                    : rvRead[0][i];
                const float wet = fxReverb.processSample (mid) * patch.fxRevMix;
                for (int c = 0; c < rvCount; ++c)
                    rvWrite[c][i] += wet;
            }
        }

        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* ch = buffer.getWritePointer (channel);
            for (int i = 0; i < numSamples; ++i)
                ch[i] *= gain;
        }

        const bool analogEnabled = paramValue (kAnalogOn) >= 0.5f;
        const int analogMode = static_cast<int> (paramValue (kAnalogMode));
        if (analogEnabled && (analogMode == 1 || analogMode == 2) && numChannels >= 2)
        {
            const float warmth = paramValue (kWarmth);
            fatAnalog.setAmount (analogMode == 2 ? juce::jmax (0.75f, warmth) : warmth);
            fatAnalog.process (buffer);
        }

        // The master limiter: linked across the channels, and the meter reads
        // what actually leaves the plugin.
        const bool limiterActive = paramValue (kLimiterOn) >= 0.5f;
        limiter.setThreshold (decibelsToGain (paramValue (kLimiterThreshold)));
        limiter.setCeiling (decibelsToGain (paramValue (kLimiterCeiling)));
        if (! limiterActive)
            limiter.reset();

        float peakAcc = 0.0f, rmsAcc = 0.0f, gainReduction = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float linkLevel = 0.0f;
            if (limiterActive)
            {
                for (int channel = 0; channel < numChannels; ++channel)
                    linkLevel = std::max (linkLevel, std::fabs (buffer.getReadPointer (channel)[i]));
            }

            const float applied = limiterActive ? limiter.processLevel (linkLevel) : 1.0f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                float& sample = buffer.getWritePointer (channel)[i];
                sample = limiterActive ? limiter.applyCeiling (sample * applied) : sample;
                const float magnitude = std::fabs (sample);
                peakAcc = std::max (peakAcc, magnitude);
                rmsAcc += sample * sample;
            }

            gainReduction = std::min (gainReduction, applied);
        }

        // The peak is a running maximum: the interface polls it far less often
        // than the blocks arrive, so a plain store would lose the transients.
        float currentPeak = meterPeakValue.load (std::memory_order_relaxed);
        while (peakAcc > currentPeak
               && ! meterPeakValue.compare_exchange_weak (currentPeak, peakAcc, std::memory_order_relaxed))
        {
        }

        meterRmsValue.store (numChannels * numSamples > 0
                                 ? std::sqrt (rmsAcc / static_cast<float> (numChannels * numSamples))
                                 : 0.0f,
                             std::memory_order_relaxed);
        limiterReduction.store (gainReduction, std::memory_order_relaxed);

        // Out to the host.
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* source = buffer.getReadPointer (channel);
            float* destination = outputChannels[channel];
            for (int i = 0; i < numSamples; ++i)
                destination[i] = source[i];
        }
    }

    float ProcessorCore::takeMeterPeak()
    {
        return meterPeakValue.exchange (0.0f, std::memory_order_relaxed);
    }
}
