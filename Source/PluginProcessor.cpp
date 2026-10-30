#include "PluginProcessor.h"

#include <array>

#include "dsp/MidiImport.h"

using namespace juce;

namespace
{
    const StringArray kClockDivisions { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T" };
    const std::array<double, 6> kClockDivisionSteps { 4.0, 8.0 / 3.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0 };
    const std::array<double, 4> kGateDivisionSteps { 1.0, 2.0, 0.5, 2.0 / 3.0 };

    NormalisableRange<float> logRange (float lo, float hi)
    {
        const float l = std::log (lo), h = std::log (hi);
        return { lo, hi,
            [l, h] (float, float, float norm) { return std::exp (l + (h - l) * norm); },
            [l, h] (float, float, float value) { return (std::log (jmax (1e-6f, value)) - l) / (h - l); } };
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        auto attrs = [] (const String& label, int decimals = 2)
        {
            return AudioParameterFloatAttributes()
                .withLabel (label)
                .withStringFromValueFunction ([decimals] (float value, int)
                                             { return String (value, jmax (0, decimals)); })
                .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); });
        };

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::cutoff, "Cutoff", logRange (40.0f, 8000.0f), 900.0f, attrs (" Hz", 0)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::wave, "Wave", NormalisableRange<float> (0.0f, 1.0f, 0.002f), 0.5f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::res, "Resonance", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.62f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::env, "Env Amount", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.55f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::contour, "Contour", NormalisableRange<float> (0.05f, 1.5f, 0.01f), 0.5f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::accent, "Accent", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.75f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::glide, "Glide", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.3f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::drive, "Drive", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.15f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::satMix, "Dist Mix", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.5f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::tone, "Slope", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 1.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::warmth, "Warmth", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.4f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::swing, "Swing", NormalisableRange<float> (0.0f, 0.6f, 0.005f), 0.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::bpm, "Tempo", logRange (20.0f, 400.0f), 130.0f, attrs (" BPM", 1)));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::clockDiv, "Clock", kClockDivisions, 4));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::syncMode, "Sync", StringArray { "Host", "Manual", "Midi Clock" }, 0));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::gateDiv, "Gate Div", StringArray { "16th", "8th", "32nd", "16T" }, 0));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::gateOn, "Gate On", false));
        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::manualPlay, "Manual Play", true));

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::gateMix, "Gate Mix", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 1.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::gateAtk, "Gate Atk", logRange (0.2f, 50.0f), 2.0f, attrs (" ms", 1)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::gateRel, "Gate Rel", logRange (0.5f, 200.0f), 14.0f, attrs (" ms", 1)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::masterGain, "Gain", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, attrs (" dB", 1)));

        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::satType, "Saturation Type",
            StringArray { "Tanh", "Drive (Rational)", "Triode (Tube)",
                          "Fold (Wavefolder)", "Master Clip",
                          "Tape", "Tape Warm", "Cassette", "Reel (Hi-Fi)", "Tape (Hot)",
                          "Clean", "Crunch" }, 0));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::analogOn, "Analog On", true));

        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::analogMode, "Analog Mode",
            StringArray { "Standard", "Moog Thick", "Virus Hyper" }, 0));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::analogHiss, "Analog Hiss", false));

        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::filterType, "Filter Type",
            StringArray { "Classic (SVF)", "Diode Ladder (303)", "Moog Ladder",
                          "TPT SVF", "Sallen-Key", "One-Pole", "Vowel", "Diode 303 (Asym)" }, 0));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::seqOn, "Seq On", true));

        // Appended at the end of the list so existing host automation survives.
        params.push_back (std::make_unique<AudioParameterBool> (
            ParamIDs::limiterOn, "Limiter On", false));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::limiterThreshold, "Limiter Threshold",
            NormalisableRange<float> (-20.0f, 0.0f, 0.1f), -1.0f, attrs (" dB", 1)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::limiterCeiling, "Limiter Ceiling",
            NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -0.2f, attrs (" dB", 1)));
        // Appended at the end of the list so existing host automation survives.
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::attack, "Attack", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::midiMap, "MIDI Map",
            StringArray { "Off", "Launch Control", "Launch Control XL", "Push 2" }, 0));

        // FX tail: appended before the skin selector so the skin keeps its spot
        // as the last parameter of the list (AUv2 index stability). Both mixes
        // default to 0, which means the whole stage is skipped and the
        // calibrated renders of every other test stay bit-exact.
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::fxDelMix, "Delay Mix", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::fxDelDiv, "Delay Div",
            StringArray { "1/4", "1/8", "1/8.", "1/16", "1/8T" }, 2));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::fxDelFb, "Delay Fb", NormalisableRange<float> (0.0f, 0.9f, 0.005f), 0.35f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::fxDelTone, "Delay Tone", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.65f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::fxRevMix, "Reverb Mix", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.0f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParamIDs::fxRevDecay, "Reverb Decay", NormalisableRange<float> (0.0f, 1.0f, 0.005f), 0.5f, attrs ({}, 2)));
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::oversample, "Oversample",
            StringArray { "OS Off", "OS 2x", "OS 4x" }, 0));

        // Last again: the skin is GUI-only state that travels with a preset, and
        // the audio thread never looks at it.
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::uiSkin, "Skin",
            StringArray { "Dark", "Console", "Wood", "Metal" }, 0));

        // The very end of the list: nothing above this gets a new AUv2 index.
        // Start mode = Note Trigger starts the sequencer's run line on the
        // first incoming note; like the MIDI map this one is reachable through
        // the host's automation panel alone (the window has no free cell for it).
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParamIDs::startMode, "Start Mode",
            StringArray { "Transport", "Note Trigger" }, 0));

        return { params.begin(), params.end() };
    }

    String defaultSequenceText()
    {
        // The same C minor line the Init preset carries, for a first-time open.
        return "v1:36:2:640,128,388,128,642,132,389,1,647,128,385,128,642,388,131,132";
    }
}

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      valueTreeState (*this, nullptr, Identifier ("Spiral303"), createLayout())
{
    auto assignParam = [this] (auto*& target, const String& id)
    {
        using TargetType = std::remove_pointer_t<std::remove_reference_t<decltype (target)>>;
        if (auto* p = dynamic_cast<TargetType*> (valueTreeState.getParameter (id)))
            target = p;
    };

    assignParam (pCutoff,     ParamIDs::cutoff);
    assignParam (pWave,       ParamIDs::wave);
    assignParam (pRes,        ParamIDs::res);
    assignParam (pEnv,        ParamIDs::env);
    assignParam (pContour,    ParamIDs::contour);
    assignParam (pAccent,     ParamIDs::accent);
    assignParam (pGlide,      ParamIDs::glide);
    assignParam (pDrive,      ParamIDs::drive);
    assignParam (pSatMix,     ParamIDs::satMix);
    assignParam (pTone,       ParamIDs::tone);
    assignParam (pWarmth,     ParamIDs::warmth);
    assignParam (pSwing,      ParamIDs::swing);
    assignParam (pBpm,        ParamIDs::bpm);
    assignParam (pGateMix,    ParamIDs::gateMix);
    assignParam (pGateAtk,    ParamIDs::gateAtk);
    assignParam (pGateRel,    ParamIDs::gateRel);
    assignParam (pMasterGain, ParamIDs::masterGain);
    assignParam (pLimiterThr, ParamIDs::limiterThreshold);
    assignParam (pLimiterCeil, ParamIDs::limiterCeiling);
    assignParam (pLimiterOn, ParamIDs::limiterOn);
    assignParam (pAttack, ParamIDs::attack);
    assignParam (pMidiMap, ParamIDs::midiMap);
    assignParam (pFxDelMix, ParamIDs::fxDelMix);
    assignParam (pFxDelFb, ParamIDs::fxDelFb);
    assignParam (pFxDelTone, ParamIDs::fxDelTone);
    assignParam (pFxRevMix, ParamIDs::fxRevMix);
    assignParam (pFxRevDecay, ParamIDs::fxRevDecay);
    pOversample = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::oversample));

    pClockDiv = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::clockDiv));
    pSatType = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::satType));
    pSyncMode = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::syncMode));
    pGateDiv = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::gateDiv));
    pGateOn = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::gateOn));
    pAnalogOn = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::analogOn));
    pAnalogMode = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::analogMode));
    pAnalogHiss = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::analogHiss));
    pFilterType = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::filterType));
    pManualPlay = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::manualPlay));
    pSeqOn = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::seqOn));
    pStartMode = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::startMode));
    pFxDelDiv = dynamic_cast<AudioParameterChoice*> (valueTreeState.getParameter (ParamIDs::fxDelDiv));

    seqCurrent = Sequence::decode (defaultSequenceText());

    valueTreeState.state.addListener (this);
    listenToParameters();
}

void PluginProcessor::listenToParameters()
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            valueTreeState.addParameterListener (withId->getParameterID(), this);
}

void PluginProcessor::stopListeningToParameters()
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            valueTreeState.removeParameterListener (withId->getParameterID(), this);
}

PluginProcessor::~PluginProcessor()
{
    stopListeningToParameters();
    valueTreeState.state.removeListener (this);
}

void PluginProcessor::prepareToPlay (double sampleRate, int maxExpectedBlockLength)
{
    engine.prepare (sampleRate);
    midiClock.prepare();
    fatAnalog.prepare (sampleRate, 2);
    fxDelay.prepare (sampleRate);
    fxReverb.prepare (sampleRate);
    limiter.prepare (sampleRate);
    tempoBpm.store (pBpm != nullptr ? static_cast<double> (pBpm->get()) : 130.0);
    (void) maxExpectedBlockLength;
}

void PluginProcessor::releaseResources()
{
    // The peak is now a running maximum, so a stopped engine has to clear it:
    // an offline render reuses processBlock() and would otherwise start from
    // whatever the last live pass left behind.
    meterPeak.store (0.0f, std::memory_order_relaxed);
    meterRms.store (0.0f, std::memory_order_relaxed);
    limiterReduction.store (1.0f, std::memory_order_relaxed);
    // A stopped engine must not carry an echo tail into the next plugin load:
    // an offline render (WAV export) reuses processBlock() and would otherwise
    // start from whatever the last live pass left in the FX buffers.
    fxDelay.reset();
    fxReverb.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == AudioChannelSet::mono();
}

void PluginProcessor::syncCustomStateAudioThread()
{
    if (seqDirty.exchange (false))
    {
        const ScopedLock lock (customCs);
        seqAudio = seqCurrent;
    }
    if (gateDirty.exchange (false))
    {
        const ScopedLock lock (customCs);
        gateAudio = std::make_shared<GatePattern> (gateCurrent);
    }
}

bool PluginProcessor::gateActive() const
{
    return pGateOn != nullptr && pGateOn->get();
}

void PluginProcessor::syncHostTempo ()
{
    const bool hostSync = pSyncMode == nullptr || pSyncMode->getIndex() == 0;
    if (! hostSync)
        return;

    double hostBpm = tempoBpm.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0 && *b < 400.0)
                    hostBpm = *b;

    tempoBpm.store (hostBpm);
}

Patch PluginProcessor::buildPatch (double sampleRate) const
{
    Patch patch;
    patch.sampleRate = (sampleRate > 1.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
    patch.wave = pWave != nullptr ? pWave->get() : 0.5f;
    patch.cutoff = pCutoff != nullptr ? pCutoff->get() : 900.0f;
    patch.res = pRes != nullptr ? pRes->get() : 0.62f;
    patch.env = std::max (Patch::kEnvFloor, pEnv != nullptr ? pEnv->get() : 0.55f);
    patch.contour = pContour != nullptr ? pContour->get() : 0.5f;
    patch.accent = pAccent != nullptr ? pAccent->get() : 0.75f;
    patch.glide = pGlide != nullptr ? pGlide->get() : 0.3f;
    patch.drive = pDrive != nullptr ? pDrive->get() : 0.15f;
    patch.satMix = pSatMix != nullptr ? pSatMix->get() : 0.5f;
    patch.tone = pTone != nullptr ? pTone->get() : 1.0f;
    const bool analogEnabled = pAnalogOn == nullptr || pAnalogOn->get();
    patch.warmth = (analogEnabled && pWarmth != nullptr) ? pWarmth->get() : 0.0f;
    const bool hissEnabled = analogEnabled && pAnalogHiss != nullptr && pAnalogHiss->get();
    patch.hiss = (hissEnabled && pWarmth != nullptr) ? pWarmth->get() : 0.0f;
    patch.satType = pSatType != nullptr ? jlimit (0, 11, pSatType->getIndex()) : 0;
    patch.analogMode = pAnalogMode != nullptr ? jlimit (0, 2, pAnalogMode->getIndex()) : 0;
    patch.filterModel = pFilterType != nullptr ? jlimit (0, 7, pFilterType->getIndex()) : 0;

    const int divIndex = pClockDiv != nullptr ? jlimit (0, 5, pClockDiv->getIndex()) : 4;
    const double bpm = jlimit (20.0, 400.0, tempoBpm.load());
    const double sixteenth = 60.0 / bpm / 4.0;
    patch.stepSamples = std::max (4.0, sixteenth * kClockDivisionSteps[static_cast<size_t> (divIndex)] * sampleRate);
    patch.swingSamples = patch.stepSamples * 0.5 * (pSwing != nullptr ? pSwing->get() : 0.0f);
    // Steps per quarter for the host-grid anchoring; 0 when the division does
    // not fit a whole number of steps into a beat (1/8 triplets and friends).
    {
        const double rawStepsPerBeat = (sixteenth * sampleRate) / patch.stepSamples * 4.0;
        const int wholeSteps = static_cast<int> (std::lround (rawStepsPerBeat));
        patch.stepsPerBeat = std::abs (rawStepsPerBeat - wholeSteps) < 0.01
                                 ? static_cast<double> (wholeSteps) : -1.0;
    }

    const int gateIndex = pGateDiv != nullptr ? jlimit (0, 3, pGateDiv->getIndex()) : 0;
    patch.gateRatio = kGateDivisionSteps[static_cast<size_t> (gateIndex)];
    patch.gateMix = pGateMix != nullptr ? pGateMix->get() : 1.0f;

    // FX tail: the echo time comes from the current tempo and the chosen
    // division, and the tone knob maps onto the same log sweep as the host
    // text form shows. Both builds run the identical expressions.
    patch.fxDelMix = pFxDelMix != nullptr ? pFxDelMix->get() : 0.0f;
    patch.fxDelaySeconds = 60.0 / bpm * ParamCurves::delayDivBeats (
        pFxDelDiv != nullptr ? pFxDelDiv->getIndex() : 2);
    patch.fxDelFb = pFxDelFb != nullptr ? jlimit (0.0f, 0.9f, pFxDelFb->get()) : 0.35f;
    patch.fxDelToneHz = ParamCurves::delayToneToHz (pFxDelTone != nullptr ? pFxDelTone->get() : 0.65f);
    patch.fxRevMix = pFxRevMix != nullptr ? pFxRevMix->get() : 0.0f;
    patch.fxRevDecay = pFxRevDecay != nullptr ? jlimit (0.0f, 1.0f, pFxRevDecay->get()) : 0.5f;
    {
        static const int kOversampleFactors[3] = { 1, 2, 4 };
        patch.oversample = kOversampleFactors[jlimit (0, 2, pOversample != nullptr ? pOversample->getIndex() : 0)];
    }

    // Attack: 0 is the instant legacy attack; the square shape makes higher
    // values grow disproportionately, like a knob that reads in seconds.
    const float attackKnob = pAttack != nullptr ? pAttack->get() : 0.0f;
    patch.attackSeconds = ParamCurves::attackToSeconds (attackKnob);
    patch.midiMap = pMidiMap != nullptr ? jlimit (0, 3, pMidiMap->getIndex()) : 0;
    // REC: while the serial entry is armed the played notes belong to the
    // pattern, not to the transpose of the running line.
    patch.recording = isRecording();

    patch.sequence = &seqAudio;
    patch.gate = gateActive() && gateAudio != nullptr ? gateAudio : nullptr;
    return patch;
}

void PluginProcessor::updateTransport (bool& running, double& bpm, double& beatPosition)
{
    bpm = pBpm != nullptr ? static_cast<double> (pBpm->get()) : 130.0;
    beatPosition = -1.0;

    const int syncIndex = pSyncMode == nullptr ? 0 : pSyncMode->getIndex();
    const int startModeIndex = pStartMode == nullptr ? 0 : pStartMode->getIndex();
    midiClock.beforeBlock (syncIndex, startModeIndex);
    bool hostPlaying = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                if (*b > 20.0 && *b < 400.0)
                    bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (syncIndex == 0)
                beatPosition = pos->getPpqPosition().orFallback (-1.0);
        }
    }

    if (syncIndex == 0)
    {
        // Host-synced and the host is not rolling: the clock stands still, like the 303
        // with its run line stopped, and the next start begins again at step 1.
        running = hostPlaying;
        if (! hostPlaying)
            beatPosition = -1.0;
        // The host owns the run line here: the note trigger stands down.
        midiClock.dropTrigger();
    }
    else if (syncIndex == 1)
    {
        midiClock.evaluateManual (pManualPlay != nullptr && pManualPlay->get(), bpm,
                                  running, bpm, beatPosition);
    }
    else
    {
        midiClock.evaluateClock (bpm, running, bpm, beatPosition);
    }

    // SEQ OFF turns this into a plain MIDI instrument: the sequencer never runs,
    // incoming notes play live in every sync mode.
    const bool seqWanted = pSeqOn == nullptr || pSeqOn->get();
    if (! seqWanted)
    {
        running = false;
        beatPosition = -1.0;
    }

    tempoBpm.store (bpm);
}

//==============================================================================
// Serial step entry (REC).

void PluginProcessor::setRecording (bool on)
{
    if (recording.exchange (on, std::memory_order_relaxed) == on)
        return;

    if (on)
    {
        // The cursor starts where the line is: a playing step keeps the entry
        // in tempo, a stopped clock runs the entry from step 0.
        const int playing = playingStep.load (std::memory_order_relaxed);
        recordCursor.store (playing >= 0 ? playing : 0, std::memory_order_relaxed);
    }

    const juce::ScopedLock lock (recordCs);
    recordQueue.clear();
}

void PluginProcessor::drainRecordQueue()
{
    if (! isRecording())
        return;

    std::vector<RecordEvent> batch;
    {
        const juce::ScopedLock lock (recordCs);
        batch.swap (recordQueue);
    }
    if (batch.empty())
        return;

    Sequence edited = getSequence();
    const int steps = edited.numSteps();
    if (steps <= 0)
        return;

    const auto& offsets = Sequence::scaleOffsets (edited.scaleIndex);
    int cursor = juce::jlimit (0, steps - 1, recordCursor.load (std::memory_order_relaxed));
    bool held = false;   // a note is down and no note-off has come for it

    for (const auto& rec : batch)
    {
        if (rec.kind == 1)
        {
            // A pedal tap: the step reads a rest, cursor moves on, and the legato
            // context is over (a pedal-washed line is not legato).
            if (edited.steps[static_cast<size_t> (cursor)] != Sequence::kRest)
                edited.steps[static_cast<size_t> (cursor)] = Sequence::kRest;
            cursor = (cursor + 1) % steps;
            held = false;
        }
        else if (rec.kind == 0)
        {
            // The snapped degree of the played note inside the grid's own scale
            // (the importer's mapping: ties fall downwards, the fold clips).
            const int degree = MidiImport::nearestDegree (offsets, rec.note - edited.baseMidi);
            // The step keeps the flags it had: the pattern's accent, slide and
            // tie marks are not wiped by playing over them. The accent rides
            // the velocity band the .mid export writes, and a second note
            // played while the first is still sounding slides into the next
            // step - whatever that cell held before.
            juce::uint16 code = static_cast<juce::uint16> (Sequence::kNoteBase
                                 + (degree & 0x7f)
                                 + (edited.hasSlide (cursor) ? Sequence::kSlideFlag : 0)
                                 + (edited.hasAccent (cursor) ? Sequence::kAccentFlag : 0));
            if (held)
                code |= Sequence::kSlideFlag;
            if (rec.velocity >= 100)
                code |= Sequence::kAccentFlag;
            edited.steps[static_cast<size_t> (cursor)] = code;
            edited.setVelocity (cursor, static_cast<float> (rec.velocity) / 127.0f);

            cursor = (cursor + 1) % steps;
            held = true;
        }
        else if (rec.kind == 2)
        {
            held = false;   // the note came up: a later note is its own attack
        }
    }

    recordCursor.store (cursor, std::memory_order_relaxed);
    setSequence (edited);
}

void PluginProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    const ScopedNoDenormals noDenormals;

    const int numSamples = jmax (0, buffer.getNumSamples());
    const double sr = getSampleRate() > 1.0 ? getSampleRate() : 48000.0;

    buffer.clear();

    try
    {
        syncCustomStateAudioThread();

        const float gain = static_cast<float> (Decibels::decibelsToGain (
            pMasterGain != nullptr ? pMasterGain->get() : 0.0));

        syncHostTempo();
        Patch patch = buildPatch (sr);
        engine.setGateTimes (pGateAtk != nullptr ? pGateAtk->get() : 2.0f,
                            pGateRel != nullptr ? pGateRel->get() : 14.0f);

        // The incoming stream speaks before the transport decides: Start/Stop
        // and the 24-PPQ clock steer the Midi Clock sync, and a note-on while
        // stopped can raise the run line (Start mode = Note Trigger). The
        // engine consumes the same buffer afterwards.
        const int startModeIndex = pStartMode == nullptr ? 0 : pStartMode->getIndex();
        const int syncIndex = pSyncMode == nullptr ? 0 : pSyncMode->getIndex();
        midiClock.setSampleRate (sr);
        for (const auto metadata : midiMessages)
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
                                  // the run line, as this block will see it
                                  midiClock.clockLineUp (syncIndex, pManualPlay != nullptr && pManualPlay->get()),
                                  pBpm != nullptr ? static_cast<double> (pBpm->get()) : 130.0);

            // While the serial entry is armed, the same scan captures the
            // events for the pattern: notes (with the accent riding the
            // velocity band, like in the export) and the sustain pedal's
            // tap for a rest. Note-offs ride along too, because the slide
            // flag is legato - a second note while the first is still held.
            if (recording.load (std::memory_order_relaxed))
            {
                const bool noteOnMsg = msg.isNoteOn();
                const bool pedalRest = msg.isController() && msg.getControllerNumber() == 64
                                       && msg.getControllerValue() >= 100;
                if (noteOnMsg || pedalRest || msg.isNoteOff())
                {
                    const juce::ScopedLock lock (recordCs);
                    if (recordQueue.size() < 256)
                    {
                        RecordEvent rec;
                        rec.kind = noteOnMsg ? 0 : (pedalRest ? 1 : 2);
                        rec.note = pedalRest ? 0 : msg.getNoteNumber();
                        rec.velocity = noteOnMsg ? msg.getVelocity() : 0;
                        recordQueue.push_back (rec);
                    }
                }
            }
        }
        midiClock.advance (numSamples, sr);

        bool running = true;
        double bpm = 130.0, beats = -1.0;
        updateTransport (running, bpm, beats);

        sequencerRunning.store (running, std::memory_order_relaxed);

        engine.process (buffer, midiMessages, patch, running, beats);
        // The pattern's own line leaves as MIDI: the outgoing note events built
        // during the render (the incoming buffer was consumed and is empty by
        // now) ride the same buffer back out to the host.
        engine.takeMidiOut (midiMessages);

        const int numSteps = seqAudio.numSteps();
        playingStep.store (running && numSteps > 0 ? engine.getPatternIndex() % numSteps : -1);
        playingGateSector.store (gateActive() && patch.gate != nullptr ? engine.getGateSector() : -1);

        // FX tail: the delay, then the room, both between the engine and the
        // master gain. With everything at its defaults (mix 0) neither stage
        // runs a single sample, so the calibrated renders of every other test
        // stay bit-exact.
        if (patch.fxDelMix > 0.0f)
        {
            fxDelay.setParameters (patch.fxDelaySeconds, patch.fxDelFb,
                                   patch.fxDelToneHz, patch.fxDelMix);
            float* fxChannels[2] { nullptr, nullptr };
            const int fxCount = jmin (2, buffer.getNumChannels());
            for (int c = 0; c < fxCount; ++c)
                fxChannels[c] = buffer.getWritePointer (c);
            fxDelay.process (fxChannels, buffer.getNumChannels(), numSamples);
        }
        if (patch.fxRevMix > 0.0f)
        {
            fxReverb.setDecay (patch.fxRevDecay);
            const float* rvRead[2] { nullptr, nullptr };
            float* rvWrite[2] { nullptr, nullptr };
            const int rvCount = jmin (2, buffer.getNumChannels());
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

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float* ch = buffer.getWritePointer (channel);
            for (int i = 0; i < numSamples; ++i)
                ch[i] *= gain;
        }

        const bool analogEnabled = (pAnalogOn == nullptr) ? true : pAnalogOn->get();
        const int analogMode = (pAnalogMode != nullptr) ? pAnalogMode->getIndex() : 0;
        if (analogEnabled && (analogMode == 1 || analogMode == 2) && buffer.getNumChannels() >= 2 && numSamples > 0)
        {
            const float warmth = (pWarmth != nullptr) ? pWarmth->get() : 0.4f;
            fatAnalog.setAmount (analogMode == 2 ? juce::jmax (0.75f, warmth) : warmth);
            fatAnalog.process (buffer);
        }

        // Master limiter: linked across channels, threshold in dB, and the
        // meter reads what actually leaves the plugin. It is off by default so
        // the calibrated loudness tables stay exact until the user engages it.
        const bool limiterActive = pLimiterOn != nullptr && pLimiterOn->get();
        const float thrDb = pLimiterThr != nullptr ? pLimiterThr->get() : -1.0f;
        const float ceilDb = pLimiterCeil != nullptr ? pLimiterCeil->get() : -0.2f;
        limiter.setThreshold (static_cast<float> (Decibels::decibelsToGain (thrDb)));
        limiter.setCeiling (static_cast<float> (Decibels::decibelsToGain (ceilDb)));
        if (! limiterActive)
            limiter.reset();

        float peakAcc = 0.0f, rmsAcc = 0.0f, gainReduction = 1.0f;
        const int channels = buffer.getNumChannels();
        const float* readPtr[2] { nullptr, nullptr };
        float* writePtr[2] { nullptr, nullptr };
        for (int channel = 0; channel < channels && channel < 2; ++channel)
        {
            readPtr[channel] = buffer.getReadPointer (channel);
            writePtr[channel] = buffer.getWritePointer (channel);
        }
        for (int i = 0; i < numSamples; ++i)
        {
            // The limiter tracks the linked (loudest channel) level and returns
            // the gain to apply to every channel of this sample; the tanh knee
            // under the ceiling clamps anything still escaping.
            float linkLevel = 0.0f;
            if (limiterActive)
            {
                for (int channel = 0; channel < channels; ++channel)
                    linkLevel = std::max (linkLevel, std::fabs (readPtr[channel][i]));
            }
            const float applied = limiterActive ? limiter.processLevel (linkLevel) : 1.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                float& sample = writePtr[channel][i];
                sample = limiterActive ? limiter.applyCeiling (sample * applied) : sample;
                const float magnitude = std::fabs (sample);
                peakAcc = std::max (peakAcc, magnitude);
                rmsAcc += sample * sample;
            }
            gainReduction = std::min (gainReduction, applied);
        }
        pushMeterPeak (peakAcc);
        meterRms.store (channels * numSamples > 0
                            ? std::sqrt (rmsAcc / static_cast<float> (channels * numSamples))
                            : 0.0f, std::memory_order_relaxed);
        limiterReduction.store (gainReduction, std::memory_order_relaxed);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("Spiral303: exception in processBlock: " + String (e.what()));
        sequencerRunning.store (false, std::memory_order_relaxed);
        buffer.clear();
    }
    catch (...)
    {
        juce::Logger::writeToLog ("Spiral303: unknown exception in processBlock");
        sequencerRunning.store (false, std::memory_order_relaxed);
        buffer.clear();
    }
}

//==============================================================================
int PluginProcessor::getNumPrograms()
{
    return static_cast<int> (factoryPresets().size());
}

int PluginProcessor::getCurrentProgram()
{
    return currentProgramIdx;
}

void PluginProcessor::setCurrentProgram (int index)
{
    applyProgram (index);
    currentProgramIdx = jlimit (0, getNumPrograms() - 1, index);
    // A factory program replaces whatever user preset was loaded.
    loadedUserPreset = {};
}

const String PluginProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return {};
    return presets[static_cast<size_t> (index)].name;
}

void PluginProcessor::applyProgram (int index)
{
    const auto& presets = factoryPresets();
    if (index >= 0 && index < static_cast<int> (presets.size()))
        applyPreset (presets[static_cast<size_t> (index)]);
}

void PluginProcessor::applyPreset (const PresetDef& preset)
{
    auto& params = getParameters();

    for (const auto& value : preset.values)
    {
        for (int i = 0; i < params.size(); ++i)
        {
            auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (params[i]);
            if (withId == nullptr || withId->getParameterID() != value.paramId)
                continue;

            if (auto* rap = dynamic_cast<RangedAudioParameter*> (params[i]))
            {
                const float norm = rap->getNormalisableRange().convertTo0to1 (value.rawValue);
                rap->setValueNotifyingHost (jlimit (0.0f, 1.0f, norm));
            }
            break;
        }
    }

    if (preset.sequence.isNotEmpty())
        setSequence (Sequence::decode (preset.sequence));
    if (preset.gate.isNotEmpty())
        setGate (GatePattern::decode (preset.gate));

    programDirty = false;
}

//==============================================================================
void PluginProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = valueTreeState.copyState();
    {
        const ScopedLock lock (customCs);
        state.setProperty (CustomKeys::sequence, seqCurrent.encode(), nullptr);
        state.setProperty (CustomKeys::gate, gateCurrent.encode(), nullptr);
    }
    if (presetNote.isNotEmpty())
        state.setProperty (CustomKeys::presetNote, presetNote, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || xml->getTagName() != valueTreeState.state.getType().toString())
        return;

    valueTreeState.replaceState (ValueTree::fromXml (*xml));

    auto state = valueTreeState.copyState();
    setSequence (Sequence::decode (state.getProperty (
        CustomKeys::sequence, String (defaultSequenceText())).toString()));
    setGate (GatePattern::decode (state.getProperty (CustomKeys::gate, String()).toString()));

    tempoBpm.store (pBpm != nullptr ? static_cast<double> (pBpm->get()) : 130.0);
    programDirty = false;
    // The restored state is whatever the host session holds; it is no longer a
    // named user preset of this instance.
    loadedUserPreset = {};
    if (state.hasProperty (CustomKeys::presetNote))
        presetNote = state[CustomKeys::presetNote].toString();
    else
        presetNote.clear();
}

//==============================================================================
Sequence PluginProcessor::getSequence() const
{
    const ScopedLock lock (customCs);
    return seqCurrent;
}

void PluginProcessor::setSequence (const Sequence& s)
{
    {
        const ScopedLock lock (customCs);
        seqCurrent = s;
    }
    seqDirty = true;
    programDirty = true;
    ++seqVersion;
}

GatePattern PluginProcessor::getGate() const
{
    const ScopedLock lock (customCs);
    return gateCurrent;
}

void PluginProcessor::setGate (const GatePattern& g)
{
    {
        const ScopedLock lock (customCs);
        gateCurrent = g;
    }
    gateDirty = true;
    programDirty = true;
    if (auto* p = dynamic_cast<AudioParameterBool*> (valueTreeState.getParameter (ParamIDs::gateOn)))
    {
        const bool anyClosed = g.closedMask != 0;
        if (p->get() != (anyClosed ? 1.0f : 0.0f))
            p->setValueNotifyingHost (anyClosed ? 1.0f : 0.0f);
    }
    ++gateVersion;
}

bool PluginProcessor::isSequencerRunning()
{
    // Cached from the audio thread. Calling updateTransport() (and thus the host
    // PlayHead) on the GUI/timer thread is not safe in every AU host (e.g. Ableton).
    return sequencerRunning.load (std::memory_order_relaxed);
}

double PluginProcessor::getStepLengthMs() const
{
    const double bpm = jlimit (20.0, 400.0, tempoBpm.load());
    const int divIndex = pClockDiv != nullptr ? jlimit (0, 5, pClockDiv->getIndex()) : 4;
    return 60000.0 / bpm / 4.0 * kClockDivisionSteps[static_cast<size_t> (divIndex)];
}

double PluginProcessor::getGateLengthMs() const
{
    const int gateIndex = pGateDiv != nullptr ? jlimit (0, 3, pGateDiv->getIndex()) : 0;
    return getStepLengthMs() * kGateDivisionSteps[static_cast<size_t> (gateIndex)];
}

double PluginProcessor::getTuneHz() const
{
    return pCutoff != nullptr ? static_cast<double> (pCutoff->get()) : 900.0;
}

int PluginProcessor::renderPatternPreview (std::vector<float>& out, int numBars) const
{
    const double sr = 48000.0;
    Patch local = buildPatch (sr);

    const Sequence seq = getSequence();
    local.sequence = &seq;

    std::shared_ptr<const GatePattern> gatePtr;
    if (gateActive())
    {
        gatePtr = std::make_shared<GatePattern> (getGate());
        local.gate = gatePtr;
    }

    const int steps = jmax (1, seq.numSteps());
    const int totalSamples = static_cast<int> (local.stepSamples * steps * numBars);
    if (totalSamples <= 0 || totalSamples > 48000 * 60)
        return 0;

    AudioBuffer<float> buffer (1, totalSamples);
    buffer.clear();

    TbEngine preview;
    preview.prepare (sr);
    FxDelay previewDelay;
    FxReverb previewReverb;
    previewDelay.prepare (sr);
    previewReverb.prepare (sr);

    MidiBuffer empty;
    const int block = 512;
    for (int offset = 0; offset < totalSamples; offset += block)
    {
        AudioBuffer<float> slice (1, block);
        slice.clear();
        preview.process (slice, empty, local, true, -1.0);
        // The same FX chain and the same order the live path applies after the
        // engine (the FAT-analog stage is not part of the preview, which was
        // always the case), so an exported file carries the knobs as set.
        if (local.fxDelMix > 0.0f)
        {
            previewDelay.setParameters (local.fxDelaySeconds, local.fxDelFb,
                                        local.fxDelToneHz, local.fxDelMix);
            float* fxChannels[1] { slice.getWritePointer (0) };
            previewDelay.process (fxChannels, 1, slice.getNumSamples());
        }
        if (local.fxRevMix > 0.0f)
        {
            previewReverb.setDecay (local.fxRevDecay);
            float* rv = slice.getWritePointer (0);
            for (int i = 0; i < slice.getNumSamples(); ++i)
                rv[i] += previewReverb.processSample (rv[i]) * local.fxRevMix;
        }
        for (int c = 0; c < slice.getNumChannels(); ++c)
            buffer.copyFrom (0, offset, slice.getReadPointer (0),
                             jmin (block, totalSamples - offset));
    }

    const float gain = static_cast<float> (Decibels::decibelsToGain (
        pMasterGain != nullptr ? pMasterGain->get() : 0.0));
    out.assign (static_cast<size_t> (totalSamples), 0.0f);
    for (int i = 0; i < totalSamples; ++i)
        out[static_cast<size_t> (i)] = buffer.getSample (0, i) * gain;
    return totalSamples;
}

AudioParameterFloat* PluginProcessor::findFloatParam (const String& id)
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (withId->getParameterID() == id)
                return dynamic_cast<AudioParameterFloat*> (p);
    return nullptr;
}

//==============================================================================
File PluginProcessor::userPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Spiral303").getChildFile ("Presets");
}

StringArray PluginProcessor::getUserPresetNames() const
{
    StringArray names;
    for (const auto& f : userPresetFolder().findChildFiles (File::findFiles, false, "*.sp303preset"))
    {
        PresetDef preset;
        String error;
        if (PresetFormat::parseText (f.loadFileAsString(), preset, error))
            names.add (f.getFileNameWithoutExtension());
    }
    names.sort (true);
    return names;
}

PresetDef PluginProcessor::currentPreset (const String& name) const
{
    PresetDef preset;
    preset.name = name;
    preset.comment = getPresetNote();

    for (auto* ap : getParameters())
    {
        if (auto* rap = dynamic_cast<RangedAudioParameter*> (ap))
            if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (ap))
                preset.values.push_back ({ withId->getParameterID(),
                                           rap->convertFrom0to1 (ap->getValue()) });
    }

    {
        const ScopedLock lock (customCs);
        preset.sequence = seqCurrent.encode();
        preset.gate = gateCurrent.encode();
    }
    return preset;
}

bool PluginProcessor::saveUserPreset (const String& name)
{
    if (name.trim().isEmpty())
        return false;

    auto safeName = File::createLegalFileName (name.trim());
    if (safeName.isEmpty())
        return false;

    auto folder = userPresetFolder();
    if (! folder.isDirectory() && ! folder.createDirectory())
        return false;

    auto file = folder.getChildFile (safeName + ".sp303preset");
    auto temp = folder.getChildFile (safeName + ".sp303preset.tmp");

    // Write to a temporary file and rename it into place so a crash during save
    // cannot leave a half-written preset behind.
    temp.deleteFile();
    {
        FileOutputStream out (temp);
        if (! out.openedOk())
            return false;

        if (! out.writeText (PresetFormat::toText (currentPreset (safeName)), false, false, nullptr))
        {
            temp.deleteFile();
            return false;
        }
    }

    if (file.existsAsFile())
        file.deleteFile();
    if (! temp.moveFileTo (file))
    {
        if (! temp.copyFileTo (file))
        {
            temp.deleteFile();
            return false;
        }
        temp.deleteFile();
    }
    // The freshly saved file mirrors the current state: it becomes the loaded
    // user preset, and there is nothing left to mark as edited.
    if (file.existsAsFile())
    {
        loadedUserPreset = safeName;
        programDirty = false;
    }
    return file.existsAsFile();
}

bool PluginProcessor::loadUserPreset (const String& name)
{
    const auto safeName = File::createLegalFileName (name.trim());
    auto file = userPresetFolder().getChildFile (safeName + ".sp303preset");
    if (! file.existsAsFile())
        return false;

    PresetDef preset;
    String error;
    if (! PresetFormat::parseText (file.loadFileAsString(), preset, error))
        return false;

    applyPreset (preset);
    programDirty = false;
    // A user preset that loaded keeps its name; also adopt the note. The identity
    // is the file's own name, so the menu, the field and the disk agree.
    loadedUserPreset = safeName;
    presetNote = preset.comment;
    return true;
}

bool PluginProcessor::deleteUserPreset (const String& name)
{
    const auto safeName = File::createLegalFileName (name.trim());
    auto file = userPresetFolder().getChildFile (safeName + ".sp303preset");
    if (! file.existsAsFile() || ! file.deleteFile())
        return false;
    // A deleted preset can no longer be "the loaded one".
    if (loadedUserPreset.equalsIgnoreCase (safeName))
        loadedUserPreset = {};
    return true;
}

bool PluginProcessor::renameUserPreset (const String& from, const String& to)
{
    const auto src = File::createLegalFileName (from.trim());
    const auto dst = File::createLegalFileName (to.trim());
    if (src.isEmpty() || dst.isEmpty() || src.equalsIgnoreCase (dst))
        return false;

    auto folder = userPresetFolder();
    const auto srcFile = folder.getChildFile (src + ".sp303preset");
    const auto dstFile = folder.getChildFile (dst + ".sp303preset");
    if (! srcFile.existsAsFile() || dstFile.existsAsFile())
        return false;

    // The bank is sorted case-insensitively, so a destination that differs from an
    // existing preset only in case is a collision even where the filesystem - or a
    // later sort - would fold the two together.
    for (const auto& n : getUserPresetNames())
        if (n.equalsIgnoreCase (dst))
            return false;

    PresetDef def;
    String error;
    if (! PresetFormat::parseText (srcFile.loadFileAsString(), def, error))
        return false;

    // The state that is already on disk travels with the new name: renaming a
    // preset the user has since edited must not quietly save those edits as a
    // second copy.
    def.name = dst;
    const auto temp = folder.getChildFile (dst + ".sp303preset.tmp");
    temp.deleteFile();
    {
        FileOutputStream out (temp);
        if (! out.openedOk() || ! out.writeText (PresetFormat::toText (def), false, false, nullptr))
        {
            temp.deleteFile();
            return false;
        }
    }
    if (! temp.moveFileTo (dstFile))
    {
        if (! temp.copyFileTo (dstFile))
        {
            temp.deleteFile();
            return false;
        }
        temp.deleteFile();
    }
    if (! dstFile.existsAsFile() || ! srcFile.deleteFile())
        return false;

    // Set after the old file is gone: deleteUserPreset clears the identity, and
    // here the preset is still the one on screen - only its name changed.
    if (loadedUserPreset.equalsIgnoreCase (src))
        loadedUserPreset = dst;
    return true;
}

bool PluginProcessor::loadPresetFile (const File& file)
{
    PresetDef preset;
    String error;
    if (! file.existsAsFile() || ! PresetFormat::parseText (file.loadFileAsString(), preset, error))
        return false;

    applyPreset (preset);
    programDirty = false;
    // A dropped file that lives in the user preset folder counts as the loaded
    // user preset for the menu and the button, like loadUserPreset does.
    if (file.getParentDirectory() == userPresetFolder())
        loadedUserPreset = file.getFileNameWithoutExtension();
    else
        loadedUserPreset = {};
    return true;
}
