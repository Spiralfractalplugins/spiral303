#include <functional>

#include <JuceHeader.h>

#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"
#include "../Source/dsp/ResonantFilter.h"
#include "../Source/dsp/Saturation.h"
#include "../Source/dsp/Sequence.h"
#include "../Source/dsp/MidiImport.h"
#include "../Source/dsp/WaveOsc.h"
#include "../Source/dsp/WavExport.h"

using namespace juce;

namespace
{
    File unwritableTarget()
    {
        return File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("spiral303_no_such_dir").getChildFile ("out.wav");
    }

    void setFloat (PluginProcessor& p, const String& id, float raw)
    {
        if (auto* f = dynamic_cast<AudioParameterFloat*> (p.state().getParameter (id)))
            f->setValueNotifyingHost (f->getNormalisableRange().convertTo0to1 (raw));
    }
}

void runCoverageTests (const std::function <void (bool, const String&)>& check)
{
    std::cout << "--- version ---" << std::endl;

    // SPIRAL303_VERSION is set from project() in CMake and is the single source of
    // truth for About, the host program version and this test binary.
    {
        const String v = SPIRAL303_VERSION;
        StringArray parts = StringArray::fromTokens (v, ".", "");
        parts.removeEmptyStrings();
        bool numeric = parts.size() == 3;
        for (const auto& p : parts)
            numeric = numeric && p.containsOnly ("0123456789");
        check (numeric, "version is major.minor.patch (" + v + ")");
        check (v != "0.0.0" && v.isNotEmpty(), "version is set");
    }

    std::cout << "--- sequence and gate ---" << std::endl;

    // Pattern text round trip, flags included.
    {
        Sequence s;
        s.setNumSteps (20);
        s.baseMidi = 33;
        s.scaleIndex = 8;
        s.steps[0] = Sequence::kNoteBase + 5;
        s.steps[1] = Sequence::kNoteBase + 5 + Sequence::kSlideFlag;
        s.steps[2] = Sequence::kNoteBase + 12 + Sequence::kAccentFlag;
        s.steps[3] = Sequence::kTie;
        s.steps[4] = Sequence::kRest;
        const String text = s.encode();
        Sequence r = Sequence::decode (text);
        check (r.encode() == text, "pattern text round-trips");
        check (r.numSteps() == 20 && r.baseMidi == 33 && r.scaleIndex == 8,
               "length, base note and scale survive the round trip");
        check (r.hasSlide (1) && ! r.hasSlide (0), "slide flag survives");
        check (r.hasAccent (2) && r.degree (2) == 12, "accent and degree survive");
        check (r.isTie (3) && r.isRest (4), "tie and rest survive");

        // Per-step velocities: custom values round-trip through v2, defaults stay v1.
        Sequence v;
        v.setNumSteps (16);
        v.steps[0] = Sequence::kNoteBase + 3;
        v.steps[1] = Sequence::kNoteBase + 6;
        v.steps[2] = Sequence::kNoteBase + 9;
        v.setVelocity (0, 0.25f);
        v.setVelocity (1, 1.0f);   // == default full level, must not mint a v2 tag alone
        v.setVelocity (2, 0.6f);
        check (v.velocityOf (0) == 32 / 127.0f, "velocity 0.25 stores as 32/127");
        const String vtext = v.encode();
        check (vtext.startsWith ("v2:"), "custom velocities switch the format to v2");
        Sequence vr = Sequence::decode (vtext);
        check (vr.encode() == vtext, "velocities round-trip");
        check (vr.velocityOf (0) == v.velocityOf (0) && vr.velocityOf (2) == v.velocityOf (2),
               "velocity values survive");

        Sequence plain;
        plain.setNumSteps (16);
        plain.steps[0] = Sequence::kNoteBase + 3;
        check (plain.encode().startsWith ("v1:"), "default velocities keep the v1 format");
        Sequence legacy = Sequence::decode ("v1:36:1:128,131,0,1");
        check (legacy.velocityOf (0) == 1.0f && legacy.velocityOf (1) == 1.0f,
               "legacy v1 patterns read full velocity");

        {
            // Dragging to the exact default must not loiter in the array.
            Sequence d;
            d.setNumSteps (16);
            d.steps[0] = Sequence::kNoteBase;
            d.steps[1] = Sequence::kNoteBase;
            d.setVelocity (0, 0.4f);
            d.setVelocity (0, 1.0f);
            check (d.encode().startsWith ("v1:"), "restored default velocity stays v1");
        }
    }

    // Garbage and out-of-range text degrade instead of throwing.
    {
        check (Sequence::decode ("nonsense").numSteps() == 16, "junk decodes to a blank bar");
        check (Sequence::decode ("v9:36:0:128").numSteps() == 16, "unknown version is refused");
        check (Sequence::decode ("v1:36:99:128,129,130,131").scaleIndex == 11,
               "scale index is clamped into the table");
        Sequence long_ = Sequence::decode ("v1:36:0:128,128,128,128,128,128,128,128,128,128");
        long_.setNumSteps (64);
        check (long_.numSteps() == Sequence::kMaxSteps, "step count clamps to 32");
        long_.setNumSteps (1);
        check (long_.numSteps() == 4, "step count clamps to 4 from below");
    }

    // Scale mapping: same degree, different scales, octave extensions.
    {
        Sequence s;
        s.baseMidi = 36;
        s.scaleIndex = 0; // Chromatic
        s.steps.assign (16, static_cast<juce::uint16> (Sequence::kNoteBase + 7));
        check (s.midiNote (0) == 43, "chromatic degree 7 is a fifth");
        s.scaleIndex = 2; // Natural minor
        s.steps[1] = static_cast<juce::uint16> (Sequence::kNoteBase + 4);
        check (s.midiNote (1) == 43, "natural minor degree 4 is the fifth");
        s.scaleIndex = 4; // Phrygian
        s.steps[2] = static_cast<juce::uint16> (Sequence::kNoteBase + 1);
        check (s.midiNote (2) == 37, "phrygian degree 1 is the flat second");
        s.steps[0] = static_cast<juce::uint16> (Sequence::kNoteBase + 13);
        check (s.midiNote (0) > s.midiNote (1), "degree 13 sits above degree 4");
        s.steps[1] = Sequence::kRest;
        check (s.midiNote (1) == 36, "a rest reports the base note");
        check (Sequence::scaleNames().size() >= 10, "the scale menu is populated");
    }

    // Cell editing cycles note -> tie -> rest, and the flags toggle independently.
    {
        Sequence s;
        s.setNumSteps (8);
        s.clearAll();
        s.clickCell (0);
        check (s.isNote (0), "first click drops a note");
        const int degree = s.degree (0);
        s.clickCell (0);
        check (s.isTie (0), "second click ties it");
        s.clickCell (0);
        check (s.isRest (0), "third click clears it");
        s.clickCell (1);
        s.toggleAccent (1);
        s.toggleSlide (1);
        check (s.hasAccent (1) && s.hasSlide (1) && s.degree (1) == degree,
               "accent and slide toggle without moving the pitch");
        s.toggleAccent (1);
        check (! s.hasAccent (1) && s.hasSlide (1), "accent toggles back off");
    }

    // randomise is deterministic per seed.
    {
        Sequence a, b, c;
        a.setNumSteps (16);
        b.setNumSteps (16);
        c.setNumSteps (16);
        a.randomise (7);
        b.randomise (7);
        c.randomise (8);
        check (a.encode() == b.encode(), "the same seed gives the same pattern");
        check (a.encode() != c.encode(), "a different seed gives a different pattern");
        int notes = 0;
        for (int i = 0; i < a.numSteps(); ++i)
            notes += a.isNote (i) ? 1 : 0;
        check (notes >= 4, "a randomised bar holds notes, not only rests");
    }

    // Gate wheel.
    {
        GatePattern g;
        check (g.closedMask == 0 && ! g.isClosed (0), "a fresh wheel is fully open");
        g.toggle (0);
        g.toggle (4);
        check (g.isClosed (0) && g.isClosed (4) && ! g.isClosed (1), "toggles land on the sectors");
        g.toggle (0);
        check (! g.isClosed (0), "a second toggle reopens");
        const String text = g.encode();
        GatePattern r = GatePattern::decode (text);
        check (r.closedMask == g.closedMask, "wheel text round-trips");
        check (GatePattern::decode ("junk").closedMask == 0, "junk decodes to an open wheel");
        check (GatePattern::decode ("g1:ffff:99").rotation == 32, "rotation count is clamped");
        g.toggle (99);
        check (! g.isClosed (99), "an out-of-range sector is ignored");
    }

    std::cout << "--- preset format ---" << std::endl;

    {
        PresetDef def;
        String error;
        const String json = R"({ "format": 1, "name": "Test Line",
            "params": { "Cutoff": 1200.0, "Res": 0.7, "ClockDiv": 2, "Gain": -3.5 },
            "sequence": "v1:36:2:128,1,129,130", "gate": "g1:AAAA:0", "credit": "whoever" })";
        check (PresetFormat::parseText (json, def, error), "a well-formed preset parses");
        check (def.name == "Test Line" && def.credit == "whoever", "name and credit read");
        check (def.values.size() == 4, "every param value is collected");
        check (def.sequence == "v1:36:2:128,1,129,130" && def.gate == "g1:AAAA:0",
               "pattern and wheel read");

        PresetDef back;
        check (PresetFormat::parseText (PresetFormat::toText (def), back, error),
               "serialised preset parses back");
        check (back.name == def.name && back.sequence == def.sequence && back.gate == def.gate
                   && back.values.size() == def.values.size(), "round trip is lossless");

        PresetDef bad;
        check (! PresetFormat::parseText ("not json", bad, error), "non-JSON is refused");
        check (! PresetFormat::parseText (R"({ "format": 2, "name": "X", "params": { "A": 1 } })",
                                          bad, error), "a newer format is refused");
        check (! PresetFormat::parseText (R"({ "format": 1, "params": { "A": 1 } })",
                                          bad, error), "a missing name is refused");
        check (! PresetFormat::parseText (R"({ "format": 1, "name": "X" })", bad, error),
               "a missing params object is refused");
        check (! PresetFormat::parseText (R"({ "format": 1, "name": "X", "params": { "A": "loud" } })",
                                          bad, error), "a non-numeric param is refused");
        check (! PresetFormat::parseText (R"({ "format": 1, "name": "X", "params": {} })",
                                          bad, error), "empty params are refused");
        check (error.isNotEmpty(), "the refusal says why");
    }

    // The bank must be usable by the plugin itself: known ids, playable text.
    {
        PluginProcessor p;
        StringArray known;
        for (auto* ap : p.getParameters())
            if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (ap))
                if (! known.contains (withId->getParameterID()))
                    known.add (withId->getParameterID());

        int gatePresets = 0, badId = 0, badSeq = 0;
        StringArray names;
        for (const auto& preset : factoryPresets())
        {
            if (! names.contains (preset.name))
                names.add (preset.name);
            for (const auto& value : preset.values)
                if (! known.contains (value.paramId))
                    ++badId;
            if (preset.sequence.isNotEmpty())
            {
                const Sequence s = Sequence::decode (preset.sequence);
                bool notes = false;
                for (int i = 0; i < s.numSteps(); ++i)
                    notes = notes || s.isNote (i);
                if (! notes)
                    ++badSeq;
            }
            if (preset.gate.isNotEmpty())
                ++gatePresets;
        }
        check (badId == 0, "every preset value names a real parameter");
        check (badSeq == 0, "every preset sequence holds notes");
        check (names.size() == static_cast<int> (factoryPresets().size()),
               "preset names are unique");
        check (gatePresets >= 2, "at least a couple of presets demo the gate wheel");
        check (p.getProgramName (0).isNotEmpty() && p.getNumPrograms() > 1,
               "the host program list is filled");
        check (p.getProgramName (999).isEmpty(), "an out-of-range program has no name");
    }

    // The bank as the plugin applies it: every preset lands on the grid the editor
    // can show and actually makes sound when the pattern is rendered.
    {
        PluginProcessor p;
        const auto& bank = factoryPresets();
        int offGrid = 0, silent = 0, overRows = 0;
        for (int index = 0; index < static_cast<int> (bank.size()); ++index)
        {
            p.setCurrentProgram (index);   // the host's program-change path
            const Sequence s = p.getSequence();
            if (s.numSteps() < 4 || s.numSteps() > 32 || s.baseMidi < 24 || s.baseMidi > 72)
                ++offGrid;
            for (int i = 0; i < s.numSteps(); ++i)
                if (s.isNote (i) && s.degree (i) > 13)
                    ++overRows;

            std::vector<float> preview;
            p.renderPatternPreview (preview, 1);
            double peak = 0.0;
            for (const auto v : preview)
                peak = jmax (peak, static_cast<double> (std::abs (v)));
            if (peak < 0.02)
                ++silent;
        }
        check (static_cast<int> (factoryPresets().size()) >= 100,
               "the bank carries at least a hundred lines ("
               + String (static_cast<int> (factoryPresets().size())) + ")");
        check (offGrid == 0, "every preset fits the editor: steps 4..32, base C1..C5");
        check (overRows == 0, "no preset reaches past the fourteenth row of the grid");
        check (silent == 0, "every factory preset renders sound");
    }

    // Loading a preset by file, the way drag and drop does it.
    {
        const File dir = File::getSpecialLocation (File::tempDirectory)
                             .getChildFile ("spiral303_preset_test");
        dir.deleteRecursively();
        dir.createDirectory();
        const File file = dir.getChildFile ("dropped.json");

        PresetDef def;
        def.name = "Dropped";
        def.values = { { ParamIDs::cutoff, 640.0f }, { ParamIDs::res, 0.9f } };
        def.sequence = Sequence::decode ("v1:40:3:128,130,1,129,128,132").encode();
        def.gate = "g1:0F0F:2";
        def.credit = "test";
        const String text = PresetFormat::toText (def);
        FileOutputStream (file).writeText (text, false, false, nullptr);

        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        check (p.loadPresetFile (file), "a dropped preset file loads");
        const auto* cutoff = dynamic_cast<AudioParameterFloat*> (
            p.state().getParameter (ParamIDs::cutoff));
        check (cutoff != nullptr && std::abs (cutoff->get() - 640.0f) < 1.0f, "cutoff applied from file");
        check (p.getSequence().numSteps() == 6, "pattern from file is in place");
        check (p.getGate().closedMask == 0x0f0fu && p.getGate().rotation == 2, "wheel from file");
        check (p.gateActive(), "a preset with a closed wheel switches the gate on");
        check (! p.isProgramDirty(), "loading a preset clears the dirty flag");

        setFloat (p, ParamIDs::cutoff, 900.0f);
        check (p.isProgramDirty(), "turning a knob marks the program edited");

        check (! p.loadPresetFile (dir.getChildFile ("missing.json")), "a missing file is refused");
        dir.deleteRecursively();
    }

    std::cout << "--- parameters and clock ---" << std::endl;

    {
        PluginProcessor p;
        check (p.getParameters().size() >= 20 && p.getParameters().size() <= 41,
               "the parameter count is sane (limiter params included) ("
               + String (p.getParameters().size()) + ")");
        check (p.findFloatParam (ParamIDs::cutoff) != nullptr, "cutoff is found by id");
        check (p.findFloatParam ("NoSuchParam") == nullptr, "an unknown id is not found");

        auto* cutoff = p.findFloatParam (ParamIDs::cutoff);
        check (cutoff != nullptr && std::abs (cutoff->get() - 900.0f) < 0.5f, "cutoff defaults to 900 Hz");
        if (cutoff != nullptr)
        {
            const auto& range = cutoff->getNormalisableRange();
            check (std::abs (range.convertTo0to1 (40.0f)) < 1.0e-3
                       && std::abs (range.convertTo0to1 (8000.0f) - 1.0f) < 1.0e-3,
                   "cutoff spans 40 Hz to 8 kHz");
            check (range.convertTo0to1 (2000.0f) > range.convertTo0to1 (1000.0f),
                   "cutoff is monotonic");
        }

        auto* bpm = p.findFloatParam (ParamIDs::bpm);
        check (bpm != nullptr && std::abs (bpm->range.start - 20.0f) < 0.01, "tempo reaches 20 BPM");
        check (bpm != nullptr && std::abs (bpm->range.end - 400.0f) < 0.01, "tempo reaches 400 BPM");

        // Log scale: the middle of the travel must be the geometric mean, not the arithmetic one.
        const float mid = bpm->getNormalisableRange().convertFrom0to1 (0.5f);
        check (mid > 70.0f && mid < 100.0f, "tempo is logarithmic (mid " + String (mid, 1) + " BPM)");

        auto* gain = p.findFloatParam (ParamIDs::masterGain);
        check (gain != nullptr && std::abs (gain->get()) < 0.01f, "master gain starts at unity");
    }

    {
        PluginProcessor p;
        p.setTempoBpm (120.0);
        auto* clock = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::clockDiv));
        check (clock != nullptr && clock->choices.size() == 6, "six clock divisions");

        // 120 BPM: a 16th is 125 ms, and the clock divisions multiply that.
        clock->setValueNotifyingHost (clock->getNormalisableRange().convertTo0to1 (4.0f));
        check (std::abs (p.getStepLengthMs() - 125.0) < 0.5, "1/16 at 120 BPM is 125 ms");
        clock->setValueNotifyingHost (clock->getNormalisableRange().convertTo0to1 (0.0f));
        check (std::abs (p.getStepLengthMs() - 500.0) < 1.0, "1/4 at 120 BPM is 500 ms");
        clock->setValueNotifyingHost (clock->getNormalisableRange().convertTo0to1 (5.0f));
        check (std::abs (p.getStepLengthMs() - 83.33) < 1.0, "1/16T swings the grid");
        clock->setValueNotifyingHost (clock->getNormalisableRange().convertTo0to1 (4.0f));

        auto* gateDiv = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::gateDiv));
        check (gateDiv != nullptr && gateDiv->choices.size() == 4, "four gate divisions");
        gateDiv->setValueNotifyingHost (gateDiv->getNormalisableRange().convertTo0to1 (1.0f));
        check (std::abs (p.getGateLengthMs() - 250.0) < 1.0, "gate sector can be a 32nd pair");
        gateDiv->setValueNotifyingHost (gateDiv->getNormalisableRange().convertTo0to1 (0.0f));
        check (std::abs (p.getGateLengthMs() - 125.0) < 1.0, "gate sector follows the clock");

        check (std::abs (p.getTuneHz() - p.getCutoffHz()) < 0.01, "the scope reads the filter cutoff");
    }

    // Host sync waits for the transport; manual runs on its own. The running
    // state is cached by the audio thread, so pump one block between changes.
    {
        PluginProcessor p;
        AudioBuffer<float> buf (2, 512);
        MidiBuffer midi;
        auto pump = [&buf, &midi, &p] { buf.clear(); p.processBlock (buf, midi); };

        pump();
        check (! p.isSequencerRunning(), "host sync without a play head stands still");
        auto* sync = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::syncMode));
        sync->setValueNotifyingHost (1.0f);
        pump();
        check (p.isSequencerRunning(), "manual mode runs with no host");
        auto* play = dynamic_cast<AudioParameterBool*> (p.state().getParameter (ParamIDs::manualPlay));
        play->setValueNotifyingHost (0.0f);
        pump();
        check (! p.isSequencerRunning(), "the manual PLAY button stops it");
    }

    std::cout << "--- building blocks ---" << std::endl;

    {
        WaveOsc osc;
        osc.prepare (48000.0);
        osc.setFrequency (1000.0f);
        osc.setMorph (0.0f); // sine
        double sum = 0.0, peak = 0.0;
        for (int i = 0; i < 4800; ++i)
        {
            const float v = osc.process();
            sum += v;
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        }
        check (peak > 0.5 && peak <= 1.05, "sine fills the unit range");
        check (std::abs (sum) < 4800 * 0.02, "sine has no DC offset");

        osc.setMorph (1.0f); // square
        double sqPeak = 0.0, sqSum = 0.0;
        for (int i = 0; i < 4800; ++i)
        {
            const float v = osc.process();
            sqSum += v;
            sqPeak = std::max (sqPeak, static_cast<double> (std::abs (v)));
        }
        check (sqPeak > peak * 0.9, "square is not quieter than sine");
        check (std::abs (sqSum) < 4800 * 0.05, "square is centred on zero (mean "
               + String (sqSum / 4800.0, 3) + ")");

        double sawPeak = 0.0;
        osc.setMorph (0.5f);
        for (int i = 0; i < 4800; ++i)
            sawPeak = std::max (sawPeak, static_cast<double> (std::abs (osc.process())));
        check (sawPeak > 0.5, "saw reaches a full swing");
        check (osc.getPhase() >= 0.0f && osc.getPhase() <= 2.0f, "phase stays wrapped");
    }

    {
        ResonantFilter f;
        f.prepare (48000.0);
        f.setSlope (1.0f);
        f.setResonance (0.0f);
        float quiet = 0.0f;
        for (int i = 0; i < 4800; ++i)
            quiet = std::max (quiet, std::abs (f.process (std::sin (2.0 * MathConstants<double>::pi * 8000.0 * i / 48000.0),
                                                           300.0f)));
        check (quiet < 0.2f, "a low cutoff kills an 8 kHz tone");
        float loud = 0.0f;
        f.reset();
        for (int i = 0; i < 4800; ++i)
            loud = std::max (loud, std::abs (f.process (std::sin (2.0 * MathConstants<double>::pi * 100.0 * i / 48000.0), 300.0f)));
        check (loud > 0.7f, "the passband comes through");

        // Self-oscillation must not run away to infinity.
        f.reset();
        f.setResonance (1.0f);
        float maxOut = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float drive = i < 50 ? 0.5f : 0.0f;
            maxOut = std::max (maxOut, std::abs (f.process (drive, 500.0f)));
        }
        check (maxOut < 20.0f && std::isfinite (maxOut), "max resonance stays bounded ("
               + String (maxOut, 2) + ")");
    }

    {
        Saturation sat;
        sat.setDrive (0.0f);
        sat.setMix (1.0f);
        check (std::abs (sat.process (0.3f) - 0.3f) < 0.02f, "no drive barely bends the curve");
        sat.setDrive (1.0f);
        const float loud = sat.process (2.0f);
        check (std::abs (loud) < 1.6f, "full drive clips a hot input");
        check (std::abs (sat.process (0.0f)) < 1.0e-6f, "saturation makes nothing out of silence");
        sat.setMix (0.0f);
        check (std::abs (sat.process (0.25f) - 0.25f) < 1.0e-6f, "mix at zero is dry");
    }

    std::cout << "--- disk presets and failure paths ---" << std::endl;

    {
        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.setCurrentProgram (1);
        const String name = "Spiral303 Test Preset";
        check (p.saveUserPreset (name), "a user preset is written");
        check (p.getUserPresetNames().contains (name), "the saved preset is listed");
        check (p.loadUserPreset (name), "the saved preset loads back");
        check (p.currentUserPresetName() == name, "the loaded user preset is remembered by name");
        // Saving writes the whole current state, so afterwards the file and the
        // plugin agree: the edited marker has to go out, and the bank must not
        // grow when a preset is rewritten under its own name.
        setFloat (p, ParamIDs::cutoff, 900.0f);
        check (p.isProgramDirty(), "turning a knob after loading marks the preset edited");
        const int beforeRewrite = p.getUserPresetNames().size();
        check (p.saveUserPreset (name), "the preset is rewritten under the same name");
        check (! p.isProgramDirty(), "saving clears the edited marker");
        check (p.getUserPresetNames().size() == beforeRewrite,
               "rewriting a preset does not duplicate it in the bank");
        p.setCurrentProgram (0);
        check (p.currentUserPresetName().isEmpty(),
               "loading a factory program clears the user preset name");
        p.loadUserPreset (name);
        check (p.deleteUserPreset (name), "the preset is deleted");
        check (p.currentUserPresetName().isEmpty(),
               "deleting the loaded preset clears the name too");
        check (! p.getUserPresetNames().contains (name), "the deleted preset is gone");
        check (PluginProcessor::userPresetFolder().isDirectory(), "the preset folder exists");
        check (! p.saveUserPreset ("   "), "a blank name is refused");
        check (! p.loadUserPreset ("No Such Preset At All"), "loading a missing preset fails cleanly");
    }

    {
        // Renaming is a disk operation on the saved state: whatever the knobs are
        // doing on screen must not leak into the file under its new name.
        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        const String src = "Spiral303 Rename", dst = "Spiral303 Renamed";
        setFloat (p, ParamIDs::cutoff, 1200.0f);
        check (p.saveUserPreset (src), "the rename source is written");
        p.loadUserPreset (src);
        p.setPresetNote ("keep me");
        check (p.saveUserPreset (src), "and carries a note");
        const int count = p.getUserPresetNames().size();

        setFloat (p, ParamIDs::cutoff, 3000.0f);
        check (p.isProgramDirty(), "the live state is dirtied after the save");
        check (p.renameUserPreset (src, dst), "the preset is renamed on disk");
        check (! p.getUserPresetNames().contains (src), "the old file is gone");
        check (p.getUserPresetNames().contains (dst), "the new file is there");
        check (p.getUserPresetNames().size() == count, "renaming does not grow the bank");
        check (p.currentUserPresetName() == dst, "the loaded preset carries the new name");

        PresetDef def;
        String error;
        const auto file = PluginProcessor::userPresetFolder().getChildFile (dst + ".sp303preset");
        check (PresetFormat::parseText (file.loadFileAsString(), def, error)
                 && def.name == dst && def.comment == "keep me",
               "the renamed file keeps its note under its new name");
        check (p.loadUserPreset (dst) && std::abs (p.getCutoffHz() - 1200.0f) < 1.0f,
               "the rename copied the saved cutoff, not the one on screen");

        check (! p.renameUserPreset (dst, "   "), "an empty new name is refused");
        check (! p.renameUserPreset (dst, dst), "renaming to the same name is refused");
        check (! p.renameUserPreset (dst, "SPIRAL303 RENAMED"), "a name differing only in case collides");
        check (! p.renameUserPreset ("No Such Preset", dst + "2"), "renaming a missing preset is refused");
        check (p.getUserPresetNames().size() == count && p.currentUserPresetName() == dst,
               "and the refusals left the bank alone");
        p.deleteUserPreset (dst);
        check (! p.getUserPresetNames().contains (dst), "the renamed preset is cleaned up");
    }

    {
        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        check (! WavExport::renderPattern (p, unwritableTarget(), 1),
               "export says no when the path is unwritable");
        std::vector<float> out;
        p.setSequence (Sequence::decode ("v1:36:0:0,0,0,0"));
        const int n = p.renderPatternPreview (out, 1);
        check (n > 0 && static_cast<int> (out.size()) == n, "preview of a short pattern still renders");
        p.releaseResources();
    }

    std::cout << "--- midi import ---" << std::endl;

    {
        // The grid maths is checked on literal notes, so every step index below is
        // written down by hand: stepBeats 0.25 is a sixteenth and the first attack is
        // step 0. The base sits under the phrase so the degrees are readable, and
        // chromatic keeps the window reasoning honest - there every semitone is a
        // degree of its own.
        auto line = [] (std::vector<MidiImport::Note> notes, int numSteps = 16,
                        int base = 48, int scale = 0)
        {
            Sequence s;
            s.baseMidi = base;
            s.scaleIndex = scale;
            s.setNumSteps (numSteps);
            s.clearAll();
            MidiImport::fit (s, std::move (notes), 0.25);
            return s;
        };
        auto hit = [] (int midi, double beat, double length = 0.25, int vel = 80)
        {
            MidiImport::Note n;
            n.midi = midi; n.onsetBeat = beat; n.lengthBeats = length; n.velocity = vel;
            return n;
        };

        const auto quantised = line ({ hit (48, 0.0), hit (50, 0.24), hit (52, 0.51) });
        check (quantised.degree (0) == 0 && quantised.degree (1) == 2 && quantised.degree (2) == 4,
               "a note lands on the nearest sixteenth step");
        check (quantised.isRest (3) && quantised.isRest (15),
               "steps the phrase never reaches stay rests");
        check (quantised.numSteps() == 16, "importing never changes the pattern length");

        const auto held = line ({ hit (48, 0.0, 1.0) });
        check (held.isNote (0) && held.isTie (1) && held.isTie (2) && held.isTie (3)
               && held.isRest (4),
               "a whole note is one attack and three ties");

        const auto cut = line ({ hit (48, 0.0), hit (50, 5.0), hit (52, 3.5, 2.0) });
        check (cut.isNote (0) && cut.isRest (1) && cut.isRest (13),
               "notes past the last step are cut off, not wrapped");
        check (cut.isNote (14) && cut.isTie (15) && cut.numSteps() == 16,
               "a phrase running over the end is clipped to the grid");

        const auto slurred = line ({ hit (48, 0.0), hit (48, 0.25), hit (55, 0.5), hit (57, 1.5) });
        check (slurred.isTie (1) && ! slurred.isNote (1),
               "a held pitch is not attacked twice");
        check (slurred.isNote (2) && slurred.hasSlide (2),
               "a legato change of pitch becomes a slide");
        check (slurred.isNote (6) && ! slurred.hasSlide (6) && slurred.isRest (3),
               "and a note that starts after a rest is not");
        check (! slurred.hasSlide (0), "the first note of a phrase never glides in from nowhere");

        const auto loud = line ({ hit (48, 0.0, 0.25, 127), hit (50, 0.25, 0.25, 60) });
        check (loud.hasAccent (0) && ! loud.hasAccent (1),
               "only the top of the dynamic range earns the accent");
        check (std::abs (loud.velocityOf (0) - 1.0f) < 0.01f
               && std::abs (loud.velocityOf (1) - 60.0f / 127.0f) < 0.01f,
               "the file's velocity reaches the velocity row");

        // C major over C2: the scale is the user's, so the sharp is folded onto the
        // nearest degree and the diatonic notes must not move by a single cent.
        const auto major = line ({ hit (36, 0.0), hit (38, 0.25), hit (40, 0.5),
                                   hit (42, 0.75), hit (43, 1.0) }, 16, 36, 1);
        check (major.scaleIndex == 1 && major.baseMidi == 36,
               "import keeps the chosen scale and key");
        check (major.midiNote (0) == 36 && major.midiNote (1) == 38 && major.midiNote (2) == 40
               && major.midiNote (4) == 43,
               "in-scale notes keep their exact pitch");
        check (major.midiNote (3) == 41, "the out-of-scale F# falls to the nearer F");

        const auto lower = line ({ hit (31, 0.0), hit (36, 0.25) });
        check (lower.baseMidi == 31 && lower.midiNote (0) == 31,
               "a phrase dipping below the base takes the base down with it");
        const auto higher = line ({ hit (48, 0.0), hit (53, 0.25) }, 16, 36, 1);
        check (higher.baseMidi == 36, "a phrase above the base never lifts it");
        const auto outOfReach = line ({ hit (90, 0.0) }, 16, 36, 1);
        check (outOfReach.baseMidi == 72 && std::abs (outOfReach.midiNote (0) - 90) <= 1,
               "but a phrase nothing of which fits is rescued by moving the base");

        const auto chord = line ({ hit (60, 0.0), hit (72, 0.0) }, 16, 36);
        check (chord.degree (0) == 0 && chord.numSteps() == 16,
               "of two notes struck together the lower one is kept");
    }

    {
        // The reader is checked against a file JUCE wrote itself, so the tick-to-beat
        // conversion, the track choice and the refusals all run through the real
        // parser without a binary in the repository.
        const auto dir = File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("spiral303_midi");
        dir.deleteRecursively();
        dir.createDirectory();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (480);
        MidiMessageSequence melody, bass;
        melody.addEvent (MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (90)), 0.0);
        melody.addEvent (MidiMessage::noteOff (1, 72), 960.0);
        melody.addEvent (MidiMessage::noteOn (1, 74, static_cast<juce::uint8> (90)), 1920.0);
        melody.addEvent (MidiMessage::noteOff (1, 74), 2160.0);
        bass.addEvent (MidiMessage::noteOn (2, 36, static_cast<juce::uint8> (120)), 0.0);
        bass.addEvent (MidiMessage::noteOff (2, 36), 480.0);
        bass.addEvent (MidiMessage::noteOn (2, 39, static_cast<juce::uint8> (40)), 480.0); // never released
        bass.addEvent (MidiMessage::noteOn (2, 41, static_cast<juce::uint8> (60)), 960.0);
        bass.addEvent (MidiMessage::noteOff (2, 41), 1200.0);                              // so the track has an end
        mf.addTrack (melody);
        mf.addTrack (bass);

        const File file = dir.getChildFile ("two-tracks.mid");
        {
            FileOutputStream out (file);
            check (mf.writeTo (out, 1), "the test MIDI file is written");
        }

        std::vector<MidiImport::Note> notes;
        String error;
        check (MidiImport::readFile (file, notes, error) && notes.size() == 3,
               "a two track file reads back the notes of one track");
        check (notes[0].midi == 36 && std::abs (notes[0].onsetBeat) < 1.0e-9
               && std::abs (notes[0].lengthBeats - 1.0) < 1.0e-9,
               "the lowest track wins and 480 ticks are one quarter note");
        check (notes[1].midi == 39 && std::abs (notes[1].onsetBeat - 1.0) < 1.0e-9
               && std::abs (notes[1].lengthBeats - 1.5) < 1.0e-9,
               "a note the file never releases is closed at the end of its track");

        {
            // The tick arithmetic against the resolution the file declares: 1920 of
            // 480 per quarter note is beat four, and 240 ticks are half of it. The
            // pairings only exist once the file has been through the real parser,
            // so this goes out to disk and back rather than reading the memory copy.
            MidiMessageSequence single;
            single.addEvent (MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 1920.0);
            single.addEvent (MidiMessage::noteOff (1, 60), 2160.0);
            juce::MidiFile timed;
            timed.setTicksPerQuarterNote (480);
            timed.addTrack (single);
            const File beat = dir.getChildFile ("beat.mid");
            {
                FileOutputStream out (beat);
                timed.writeTo (out, 1);
            }
            std::vector<MidiImport::Note> one;
            check (MidiImport::readFile (beat, one, error) && one.size() == 1
                   && std::abs (one[0].onsetBeat - 4.0) < 1.0e-9
                   && std::abs (one[0].lengthBeats - 0.5) < 1.0e-9,
                   "tick 1920 of a 480 ppq file is beat four, half a quarter long");
        }

        const File drums = dir.getChildFile ("drums.mid");
        {
            juce::MidiFile kit;
            kit.setTicksPerQuarterNote (480);
            MidiMessageSequence perc;
            perc.addEvent (MidiMessage::noteOn (10, 36, static_cast<juce::uint8> (100)), 0.0);
            perc.addEvent (MidiMessage::noteOff (10, 36), 240.0);
            kit.addTrack (perc);
            FileOutputStream out (drums);
            kit.writeTo (out, 1);
        }
        std::vector<MidiImport::Note> none;
        check (! MidiImport::readFile (drums, none, error) && error.isNotEmpty(),
               "a drum lane alone is refused, with a reason");

        const File smpte = dir.getChildFile ("smpte.mid");
        {
            juce::MidiFile frames;
            frames.setSmpteTimeFormat (25, 40);
            MidiMessageSequence seq;
            seq.addEvent (MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0.0);
            seq.addEvent (MidiMessage::noteOff (1, 60), 10.0);
            frames.addTrack (seq);
            FileOutputStream out (smpte);
            frames.writeTo (out, 1);
        }
        check (! MidiImport::readFile (smpte, none, error) && error.containsIgnoreCase ("frames"),
               "SMPTE timing has no beat grid and is refused");

        const File junk = dir.getChildFile ("junk.mid");
        junk.replaceWithData ("this is not a midi file at all", 30);
        check (! MidiImport::readFile (junk, none, error) && error.isNotEmpty(),
               "a file that is not MIDI is refused");
        check (! MidiImport::readFile (dir.getChildFile ("absent.mid"), none, error),
               "and so is a file that is not there");

        dir.deleteRecursively();
    }
}
