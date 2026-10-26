// The scenario both flavours of the core render.
//
// This header is included twice, each time inside a namespace: once in the
// JUCE flavour and once in the WebAssembly flavour (compiled against the
// stand-in in Source/core/JuceLite.h). It must therefore only use what the
// namespace provides - no includes of its own beyond the standard library.
//
// It deliberately touches every corner the stand-in has to cover: the clamps,
// the maths constants, the random generator (analog drift and noise floor),
// the audio buffer, the MIDI buffer, the string class (the sequencer's
// encode/decode) and the float vector copy (the stereo path).
//
// The FX twin of the first scenario exercises the tail stages (the tempo-synced
// delay and the Schroeder room) the same way the two processors apply them:
// the delay on both channels, then the mid-fed reverb added back to both.

inline void buildParityPatch (Sequence& decoded, Patch& patch,
                              std::shared_ptr<GatePattern>& gateHolder)
{
    // A pattern with notes, ties, slides, accents and velocities.
    decoded.setNumSteps (16);
    decoded.scaleIndex = 2;
    decoded.baseMidi = 36;
    decoded.clearAll();
    decoded.setNote (0, 0);  decoded.toggleAccent (0); decoded.setVelocity (0, 0.9f);
    decoded.setNote (2, 4);  decoded.toggleSlide (2);
    decoded.setTie (3);
    decoded.setNote (4, 7);
    decoded.setNote (6, 2);  decoded.setVelocity (6, 0.5f);
    decoded.setNote (8, 9);  decoded.toggleAccent (8);
    decoded.setTie (9);
    decoded.setNote (12, 5);

    // Round-trip through the text form: the encoder and the decoder are part of
    // the core, and they are where the string class is used.
    const auto text = decoded.encode();
    decoded = Sequence::decode (text);

    GatePattern gate;
    gate.numSectors = 16;
    gate.closedMask = 0x00a5u;
    gate.rotation = 3;
    gateHolder = std::make_shared<GatePattern> (gate);

    patch.sampleRate = 48000.0;
    patch.wave = 0.42f;
    patch.cutoff = 1200.0f;
    patch.res = 0.7f;
    patch.env = 0.6f;
    patch.contour = 0.4f;
    patch.accent = 0.8f;
    patch.glide = 0.25f;
    patch.attackSeconds = 0.02;
    patch.drive = 0.3f;
    patch.satMix = 0.6f;
    patch.satType = 2;
    patch.analogMode = 1;
    patch.filterModel = 3;
    patch.tone = 1.0f;
    patch.warmth = 0.6f;
    patch.stepSamples = 48000.0 * 0.125;
    patch.swingSamples = 240.0;
    patch.gateRatio = 1.0;
    patch.gateMix = 0.8f;
    patch.hiss = 0.3f;
    patch.midiMap = 1;
    patch.sequence = &decoded;
    patch.gate = gateHolder;
}

inline void renderParityPattern (std::vector<float>& outLeft,
                                 std::vector<float>& outRight,
                                 int frames)
{
    Sequence decoded;
    std::shared_ptr<GatePattern> gateHolder;
    Patch patch;
    buildParityPatch (decoded, patch, gateHolder);

    TbEngine engine;
    engine.prepare (48000.0);

    juce::AudioBuffer<float> buffer (2, frames);
    // The engine adds into the buffer, so it has to start empty - JUCE leaves
    // the memory of a fresh buffer untouched.
    buffer.clear();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 100);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 200);

    engine.process (buffer, midi, patch, true, 0.0);

    outLeft.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    outRight.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
}

inline void renderParityPatternFx (std::vector<float>& outLeft,
                                   std::vector<float>& outRight,
                                   int frames)
{
    Sequence decoded;
    std::shared_ptr<GatePattern> gateHolder;
    Patch patch;
    buildParityPatch (decoded, patch, gateHolder);

    // The tail on: a dotted eighth at 100 BPM with a warm feedback lowpass,
    // plus a room at half mix. The same expressions the processor buildPatch
    // runs, spelled out so the scenario depends on nothing outside.
    patch.fxDelMix = 0.45f;
    patch.fxDelaySeconds = 60.0 / 100.0 * 0.75;
    patch.fxDelFb = 0.55f;
    patch.fxDelToneHz = ParamCurves::delayToneToHz (0.65f);
    patch.fxRevMix = 0.4f;
    patch.fxRevDecay = 0.7f;

    TbEngine engine;
    engine.prepare (48000.0);

    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 100);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 200);

    engine.process (buffer, midi, patch, true, 0.0);

    // The same chain the two processors apply, in their order: the delay on
    // every channel, then the mid-fed room added back to both channels.
    FxDelay delayLine;
    FxReverb reverb;
    delayLine.prepare (48000.0);
    reverb.prepare (48000.0);

    float* channels[2] { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    delayLine.setParameters (patch.fxDelaySeconds, patch.fxDelFb,
                             patch.fxDelToneHz, patch.fxDelMix);
    delayLine.process (channels, 2, frames);

    reverb.setDecay (patch.fxRevDecay);
    const float* read[2] { buffer.getReadPointer (0), buffer.getReadPointer (1) };
    for (int i = 0; i < frames; ++i)
    {
        const float mid = 0.5f * (read[0][i] + read[1][i]);
        const float wet = reverb.processSample (mid) * patch.fxRevMix;
        channels[0][i] += wet;
        channels[1][i] += wet;
    }

    outLeft.assign (read[0], read[0] + frames);
    outRight.assign (read[1], read[1] + frames);
}
