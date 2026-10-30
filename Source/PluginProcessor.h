#pragma once

#include <atomic>
#include <memory>
#include <set>
#include <vector>

#include <JuceHeader.h>

#include "PluginParameters.h"
#include "Presets.h"
#include "dsp/Engine.h"
#include "dsp/FatAnalog.h"
#include "dsp/Delay.h"
#include "dsp/Reverb.h"
#include "dsp/Limiter.h"
#include "dsp/Saturation.h"
#include "dsp/Sequence.h"
#include "dsp/MidiClockTransport.h"

class PluginProcessor : public juce::AudioProcessor,
                        private juce::ValueTree::Listener,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maxExpectedBlockLength) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spiral303"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // GUI-facing API (message thread)
    Sequence getSequence() const;
    void setSequence (const Sequence&);
    GatePattern getGate() const;
    void setGate (const GatePattern&);
    int getGateVersion() const { return gateVersion.load(); }
    int getSequenceVersion() const { return seqVersion.load(); }

    /** Step the sequencer is on, for the playhead; -1 when stopped. */
    int getPlayingStep() const { return playingStep.load(); }
    /** Gate sector the wheel is over, or -1 while the gate layer is bypassed. */
    int getPlayingGateSector() const { return playingGateSector.load(); }
    bool isSequencerRunning();
    bool gateActive() const;

    juce::AudioProcessorValueTreeState& state() { return valueTreeState; }
    double getTempoBpm() const { return tempoBpm.load(); }
    void setTempoBpm (double bpm) { tempoBpm.store (juce::jlimit (20.0, 400.0, bpm)); }
    /** Reads the host playhead's tempo into tempoBpm (when Sync = Host). Safe to call
        from both the audio and the GUI thread; PlayHead access is thread-safe. */
    void syncHostTempo();
    /** Length of one 16th step in ms at the current tempo and clock division. */
    double getStepLengthMs() const;
    /** Gate sector length in ms. */
    double getGateLengthMs() const;

    /** Renders the current pattern (synth, distortion and gate) for the editor and
        for the WAV export. Message thread only. Returns the number of samples written. */
    int renderPatternPreview (std::vector<float>& out, int numBars) const;

    double getTuneHz() const;
    float getWaveForScope() const { return pWave != nullptr ? pWave->get() : 0.5f; }
    float getCutoffHz() const { return pCutoff != nullptr ? pCutoff->get() : 900.0f; }
    float getResonance() const { return pRes != nullptr ? pRes->get() : 0.5f; }
    float getEnvAmount() const { return pEnv != nullptr ? pEnv->get() : 0.5f; }
    float getSlope() const { return pTone != nullptr ? pTone->get() : 1.0f; }
    int getFilterModel() const { return pFilterType != nullptr ? pFilterType->getIndex() : 0; }

    bool isProgramDirty() const { return programDirty.load(); }

    /** The user preset that is currently loaded (empty when a factory program
        or an edited state is showing), so the editor can name it in the menu
        and on the preset button. Message thread only. */
    juce::String currentUserPresetName() const { return loadedUserPreset; }
    bool isUserPresetLoaded() const { return loadedUserPreset.isNotEmpty(); }
    const juce::String& getPresetNote() const { return presetNote; }
    void setPresetNote (const juce::String& s) { presetNote = s; programDirty = true; }

    /** Post-limiter meter readouts, written from the audio thread.
        The peak is accumulated with a compare-exchange max rather than stored per
        block: the GUI polls 30 times a second while a 512-sample block is ~10 ms,
        so a plain store lets the last block of every poll erase the transients the
        earlier ones caught. */
    void pushMeterPeak (float v)
    {
        float current = meterPeak.load (std::memory_order_relaxed);
        while (v > current && ! meterPeak.compare_exchange_weak (current, v, std::memory_order_relaxed))
        {
        }
    }
    /** Read the accumulated peak and start the next window; GUI thread only. */
    float takeMeterPeak() { return meterPeak.exchange (0.0f, std::memory_order_relaxed); }
    float getMeterRms()  const { return meterRms.load (std::memory_order_relaxed); }
    /** The limiter's last applied gain (1 = not engaging) in linear units. */
    float getLimiterGain() const { return limiterReduction.load (std::memory_order_relaxed); }

    //==============================================================================
    // User presets stored on disk (message thread only)
    juce::StringArray getUserPresetNames() const;
    bool isFactoryPresetHidden (const juce::String& name) const;
    juce::File hiddenFactoryPresetFile();
    std::set<juce::String>& hiddenFactoryNames();
    // keep matched - no-op    int visibleFactoryCount() const;
    int factoryIndexFromVisible (int) const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    /** Renames a user preset on disk, carrying the state that is already saved
        rather than the live one, so renaming a dirty preset cannot quietly write
        the pending edits under a new name. Refuses an empty or already-taken
        destination; the comparison is case-insensitive because the bank is sorted
        that way. */
    bool renameUserPreset (const juce::String& from, const juce::String& to);
    static juce::File userPresetFolder();

    /** Reads a .sp303preset (or any JSON in that format) dropped onto the window. */
    bool loadPresetFile (const juce::File&);

    juce::AudioParameterFloat* findFloatParam (const juce::String& id);

    //==============================================================================
    // Serial step entry (REC). The audio thread captures the events of the
    // incoming stream; the message thread writes them into the pattern at the
    // record cursor (the editor's timer drains the queue).

    struct RecordEvent { int kind = 0; int note = 0; int velocity = 0; };

    bool isRecording() const { return recording.load (std::memory_order_relaxed); }
    /** Step the record cursor sits on (-1 while the entry is off). */
    int recordCursorStep() const { return isRecording() ? recordCursor.load (std::memory_order_relaxed) : -1; }
    void setRecording (bool on);
    /** Message-thread only: writes the captured events into the pattern. */
    void drainRecordQueue();

private:
    void applyProgram (int index);
    void applyPreset (const PresetDef&);
    PresetDef currentPreset (const juce::String& name) const;
    void syncCustomStateAudioThread();
    void publishSequence (const Sequence&);
    void publishGate (const GatePattern&);
    Patch buildPatch (double sampleRate) const;
    /** Tempo and beat position from the host, or the manual controls. */
    void updateTransport (bool& running, double& bpm, double& beatPosition);

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree == valueTreeState.state)
            programDirty = true;
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { programDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { programDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { programDirty = true; }
    void valueTreeParentChanged (juce::ValueTree&) override { programDirty = true; }

    void parameterChanged (const juce::String&, float) override
    {
        programDirty = true;
    }

    void listenToParameters();
    void stopListeningToParameters();

    std::atomic<bool> programDirty { false };

    juce::AudioProcessorValueTreeState valueTreeState;

    juce::AudioParameterFloat* pCutoff = nullptr;
    juce::AudioParameterFloat* pWave = nullptr;
    juce::AudioParameterFloat* pRes = nullptr;
    juce::AudioParameterFloat* pEnv = nullptr;
    juce::AudioParameterFloat* pContour = nullptr;
    juce::AudioParameterFloat* pAccent = nullptr;
    juce::AudioParameterFloat* pGlide = nullptr;
    juce::AudioParameterFloat* pDrive = nullptr;
    juce::AudioParameterFloat* pSatMix = nullptr;
    juce::AudioParameterFloat* pTone = nullptr;
    juce::AudioParameterFloat* pWarmth = nullptr;
    juce::AudioParameterFloat* pSwing = nullptr;
    juce::AudioParameterFloat* pBpm = nullptr;
    juce::AudioParameterFloat* pGateMix = nullptr;
    juce::AudioParameterFloat* pGateAtk = nullptr;
    juce::AudioParameterFloat* pGateRel = nullptr;
    juce::AudioParameterFloat* pMasterGain = nullptr;
    juce::AudioParameterChoice* pSatType = nullptr;
    juce::AudioParameterBool* pAnalogOn = nullptr;
    juce::AudioParameterChoice* pAnalogMode = nullptr;
    juce::AudioParameterBool* pAnalogHiss = nullptr;
    juce::AudioParameterChoice* pFilterType = nullptr;
    juce::AudioParameterFloat* pLimiterThr = nullptr;
    juce::AudioParameterFloat* pLimiterCeil = nullptr;
    juce::AudioParameterBool* pLimiterOn = nullptr;
    juce::AudioParameterFloat* pAttack = nullptr;
    juce::AudioParameterChoice* pMidiMap = nullptr;
    // FX tail: appended before the skin selector, so the skin keeps its spot as
    // the last parameter of the list (AUv2 index stability).
    juce::AudioParameterFloat* pFxDelMix = nullptr;
    juce::AudioParameterChoice* pFxDelDiv = nullptr;
    juce::AudioParameterFloat* pFxDelFb = nullptr;
    juce::AudioParameterFloat* pFxDelTone = nullptr;
    juce::AudioParameterFloat* pFxRevMix = nullptr;
    juce::AudioParameterFloat* pFxRevDecay = nullptr;
    juce::AudioParameterChoice* pOversample = nullptr;

    FatAnalog fatAnalog;
    FxDelay fxDelay;
    FxReverb fxReverb;
    Limiter limiter;
    std::atomic<float> meterPeak { 0.0f }, meterRms { 0.0f }, limiterReduction { 1.0f };
    juce::AudioParameterChoice* pClockDiv = nullptr;
    juce::AudioParameterChoice* pSyncMode = nullptr;
    juce::AudioParameterChoice* pGateDiv = nullptr;
    juce::AudioParameterBool* pGateOn = nullptr;
    juce::AudioParameterBool* pManualPlay = nullptr;
    juce::AudioParameterBool* pSeqOn = nullptr;
    juce::AudioParameterChoice* pStartMode = nullptr;

    /** MIDI clock status and the note trigger, audio-thread only (the scanner
        runs before the transport decides this block's run line). */
    MidiClockTransport midiClock;

    /** Serial step entry: the audio thread pushes under this lock, the message
        thread swaps the batch out on every editor tick. */
    juce::CriticalSection recordCs;
    std::vector<RecordEvent> recordQueue;
    std::atomic<bool> recording { false };
    std::atomic<int> recordCursor { 0 };

    // Sequence and gate live outside the parameter tree; mirrored under a lock,
    // adopted by the audio thread at the start of each block.
    mutable juce::CriticalSection customCs;
    Sequence seqCurrent;
    GatePattern gateCurrent;
    std::atomic<bool> seqDirty { true };
    std::atomic<bool> gateDirty { true };
    std::atomic<int> seqVersion { 0 };
    std::atomic<int> gateVersion { 0 };
    std::atomic<int> playingStep { -1 };
    std::atomic<bool> sequencerRunning { false };
    std::atomic<int> playingGateSector { -1 };
    std::atomic<double> tempoBpm { 130.0 };
    std::atomic<int> currentProgramIdx { 0 };
    juce::String loadedUserPreset, presetNote;

    TbEngine engine;
    Sequence seqAudio;
    std::shared_ptr<const GatePattern> gateAudio;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
