#include <algorithm>
#include <array>
#include <functional>

#include <JuceHeader.h>

#include "../Source/PluginProcessor.h"

using namespace juce;

/** Long-run stability suite: every scenario renders minutes of audio offline
    (the engine is far faster than realtime) and asserts the output stays finite,
    bounded and audible. A crash, an exception, a runaway filter or an endless
    NaN will fail long before the wall clock matters. */

namespace
{
    std::unique_ptr<PluginProcessor> makeManual (double sr = 48000.0, int block = 512)
    {
        auto p = std::make_unique<PluginProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        if (auto* sync = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::syncMode)))
            sync->setValueNotifyingHost (1.0f);
        if (auto* play = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::manualPlay)))
            play->setValueNotifyingHost (1.0f);
        return p;
    }

    void setChoice (PluginProcessor& p, const String& id, int index)
    {
        if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (id)))
        {
            const StringArray& choices = c->choices;
            const int n = jmax (1, choices.size() - 1);
            c->setValueNotifyingHost (static_cast<float> (jlimit (0, choices.size() - 1, index))
                                      / static_cast<float> (n));
        }
    }

    void randomizeSomeParams (PluginProcessor& p, Random& rng, int count)
    {
        const auto& params = p.getParameters();
        for (int i = 0; i < count; ++i)
        {
            auto* raw = params.getUnchecked (rng.nextInt (params.size()));
            // Audio-relevant knobs only; the transport is allowed to romp, the
            // sync mode and the play button would just stop the clock. The
            // attack and limiter controls are excluded too: a random 1.2 s
            // attack silences short soaks and hides real regressions. Skin is
            // GUI-only and repaints the process-global palette, so it has no
            // business in the audio churn.
            const String name = raw->getName (512);
            if (name == "Sync" || name == "Manual Play" || name == "Seq On"
                || name == "Attack" || name == "Skin" || name.startsWith ("Limiter"))
                continue;
            if (auto* f = dynamic_cast<AudioParameterFloat*> (raw))
                f->setValueNotifyingHost (rng.nextFloat());
            else if (auto* b = dynamic_cast<AudioParameterBool*> (raw))
                b->setValueNotifyingHost (rng.nextBool() ? 1.0f : 0.0f);
            else if (auto* c = dynamic_cast<AudioParameterChoice*> (raw))
                c->setValueNotifyingHost (static_cast<float> (rng.nextInt (c->choices.size()))
                                          / static_cast<float> (c->choices.size() - 1));
        }
    }

    /** An acid-ish pattern so the soak never falls into accidental silence. */
    Sequence soakSequence (Random& rng, int steps)
    {
        Sequence s;
        s.setNumSteps (steps);
        for (int i = 0; i < steps; ++i)
        {
            uint32_t v = Sequence::kRest;
            if (rng.nextInt (10) < 7)
            {
                v = 128u + static_cast<uint32_t> (rng.nextInt (12));
                if (rng.nextBool()) v |= Sequence::kSlideFlag;
                if (rng.nextBool()) v |= Sequence::kAccentFlag;
            }
            s.steps[static_cast<size_t> (i)] = v;
        }
        return s;
    }

    MidiBuffer randomMidi (Random& rng, int numEvents, int blockLen)
    {
        MidiBuffer midi;
        for (int i = 0; i < numEvents; ++i)
        {
            const int pos = rng.nextInt (blockLen);
            const int roll = rng.nextInt (6);
            if (roll <= 1)
                midi.addEvent (MidiMessage::noteOn (1, rng.nextInt (Range<int> (36, 96)),
                                                    static_cast<juce::uint8> (rng.nextInt (128))), pos);
            else if (roll == 2)
                midi.addEvent (MidiMessage::noteOff (1, rng.nextInt (Range<int> (36, 96))), pos);
            else if (roll == 3)
                midi.addEvent (MidiMessage::controllerEvent (1,
                    rng.nextBool() ? 74 : 71, rng.nextInt (128)), pos);
            else if (roll == 4)
                midi.addEvent (MidiMessage::pitchWheel (1, rng.nextInt (16384)), pos);            else
                midi.addEvent (MidiMessage::programChange (1, rng.nextInt (128)), pos);
        }
        return midi;
    }

    struct Health
    {
        double peak = 0.0;
        int64 nonFinite = 0;
        double dcWorst = 0.0;
        double dcSustainedWorst = 0.0;
        int64 nonzero = 0;
    };

    /** Signed DC offset of channel 0; the caller judges whether it is a one-block
        gate shard or a sustained slide. */
    double blockMean (AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        if (n == 0)
            return 0.0;
        const float* ch = buf.getReadPointer (0);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            if (std::isfinite (ch[i]))
                sum += ch[i];
        return sum / static_cast<double> (n);
    }

    Health inspectBlock (AudioBuffer<float>& buf)
    {
        Health h;
        const int n = buf.getNumSamples();
        const float* ch = buf.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float v = ch[i];
            if (! std::isfinite (v))
            {
                ++h.nonFinite;
                continue;
            }
            h.peak = std::max (h.peak, static_cast<double> (std::abs (v)));
            if (v != 0.0f)
                ++h.nonzero;
        }
        return h;
    }

    /** The core soak: seconds of audio with churn thrown at the plugin. */
    Health soak (PluginProcessor& p, double seconds, Random& rng, const String& scenario)
    {
        const int sr = static_cast<int> (p.getSampleRate());
        const int block = 512;
        AudioBuffer<float> buf (2, block);
        Health total;
        const int numBlocks = static_cast<int> (seconds * sr / block);

        // Sliding ~0.5 s window of signed block means: a genuine DC runaway
        // keeps its sign across the window, a gate shard cancels out.
        constexpr int kWindowBlocks = 48;
        std::array<double, kWindowBlocks> windowMeans {};
        int windowSlot = 0;
        double windowSum = 0.0;

        for (int b = 0; b < numBlocks; ++b)
        {
            // Parameter storm: a couple of jumps every so often.
            if (b % 37 == 0)
                randomizeSomeParams (p, rng, 2);

            // Slow body of the visit: swap saturation and filter model mid-note.
            if (b % 250 == 40 && rng.nextBool())
                setChoice (p, ParamIDs::satType, rng.nextInt (12));
            if (b % 250 == 90 && rng.nextBool())
                setChoice (p, ParamIDs::filterType, rng.nextInt (8));

            // Resequencing under load.
            if (b % 333 == 7)
                p.setSequence (soakSequence (rng, 8 * (1 + rng.nextInt (4))));
            if (b % 333 == 123)
            {
                GatePattern g;
                g.closedMask = static_cast<uint16_t> (rng.nextInt (0x10000));
                g.rotation = rng.nextInt (16);
                p.setGate (g);
            }

            buf.clear();
            MidiBuffer midi = (b % 11 == 3) ? randomMidi (rng, rng.nextInt (32), block) : MidiBuffer();
            p.processBlock (buf, midi);

            const Health h = inspectBlock (buf);
            const double mean = blockMean (buf);
            total.nonFinite += h.nonFinite;
            total.peak = std::max (total.peak, h.peak);
            total.dcWorst = std::max (total.dcWorst, std::abs (mean));
            total.nonzero += h.nonzero;

            windowSum += mean - windowMeans[static_cast<size_t> (windowSlot)];
            windowMeans[static_cast<size_t> (windowSlot)] = mean;
            windowSlot = (windowSlot + 1) % kWindowBlocks;
            total.dcSustainedWorst = std::max (total.dcSustainedWorst,
                                               std::abs (windowSum / kWindowBlocks));
        }

        std::cout << "SOAK   " << scenario << ": " << String (seconds, 0)
                  << " s audio, peak " << String (total.peak, 3)
                  << ", finite ok " << (total.nonFinite == 0)
                  << ", dc " << String (total.dcWorst, 3)
                  << ", dcHalfSec " << String (total.dcSustainedWorst, 4) << std::endl;
        return total;
    }
}

void runStabilityTests (const std::function<void (bool, const String&)>& check)
{
    std::cout << "--- long-run stability ---" << std::endl;

    Random rng (0x50a41d);

    // 1. The main soak: ten minutes of audio, everything churning.
    {
        auto p = makeManual();
        const Health h = soak (*p, 600.0, rng, "10 min, params+seq+gate churn");
        check (h.nonFinite == 0, "soak renders only finite samples (bad "
               + String ((int) h.nonFinite) + ")");
        // The master gain spans +12 dB and the Clean bypass passes an
        // environment-clamped filter output (up to ±4), plus a full-accent
        // envelope and the fat-analog stage; storms can also stack near +10 dB
        // of master gain onto resonant settings. A legitimate max render lands
        // under ~24; the assertion is about runaway, not taste.
        check (h.peak < 40.0f, "soak stays bounded (peak " + String (h.peak, 3) + ")");
        // Gate chops tear a block into mostly-one-polarity shards and a note
        // attack starts from silence, so a single 512-sample block mean can
        // legitimately reach a tenth of the render peak. What must never happen
        // is a sustained offset: the ~18 Hz DC-blocker gives every transient a
        // 8.8 ms decay, so a half-second window of signed block means staying
        // above 0.05 is a diverging filter, not a shard.
        check (h.dcWorst < 0.25 * h.peak, "soak block DC stays transient-sized (dc "
               + String (h.dcWorst, 3) + " of peak " + String (h.peak, 3) + ")");
        check (h.dcSustainedWorst < 0.05f, "soak has no sustained DC offset (half-second "
               + String (h.dcSustainedWorst, 4) + ")");
        check (h.nonzero > 1000000, "soak keeps producing audio (" + String ((int) h.nonzero)
               + " nonzero samples)");
        check (p->getPlayingStep() >= 0, "the clock survives the soak");
    }

    // 2. Audio-rate changes: every sample-rate / buffer-size combo gets a storm.
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 64, 256, 1024 })
            {
                auto p = makeManual (sr, block);
                p->setSequence (soakSequence (rng, 16));
                const Health h = soak (*p, 45.0, rng,
                                        String (sr, 0) + " Hz x " + String (block) + " block");
                check (h.nonFinite == 0 && h.peak < 24.0f,
                       String (sr, 0) + " Hz / " + String (block) + " stays healthy (peak "
                       + String (h.peak, 3) + ", bad " + String ((int) h.nonFinite) + ")");
            }
        }
    }

    // 3. Preset spam: every factory program while playing, then rapid switching.
    {
        auto p = makeManual();
        const Health first = soak (*p, 0.0, rng, "noop"); // warm-up, no stats use
        juce::ignoreUnused (first);

        bool allFinite = true;
        double worstPeak = 0.0;
        const int programs = p->getNumPrograms();
        for (int i = 0; i < programs; ++i)
        {
            p->setCurrentProgram (i);
            const Health h = soak (*p, 2.0, rng, "preset " + String (i));
            allFinite &= h.nonFinite == 0;
            worstPeak = std::max (worstPeak, h.peak);
        }
        check (allFinite && worstPeak < 20.0f,
               "all " + String (programs) + " presets render healthy under churn "
               + "(worstPeak " + String (worstPeak, 3) + (allFinite ? ")" : ", NONFINITE)"));

        for (int i = 0; i < 40; ++i)
        {
            p->setCurrentProgram (rng.nextInt (programs));
            const Health h = soak (*p, 0.5, rng, "rapid switch " + String (i));
            allFinite &= h.nonFinite == 0;
            worstPeak = std::max (worstPeak, h.peak);
        }
        check (allFinite && worstPeak < 20.0f, "rapid preset switching survives (worstPeak "
               + String (worstPeak, 3) + (allFinite ? ")" : ", NONFINITE)"));
    }

    // 4. MIDI flood: note storms, leftover note-offs, huge CC bursts.
    {
        auto p = makeManual();
        AudioBuffer<float> buf (2, 512);
        Health total;
        for (int b = 0; b < 2000; ++b)
        {
            MidiBuffer midi = randomMidi (rng, 128, 512);
            buf.clear();
            p->processBlock (buf, midi);
            const Health h = inspectBlock (buf);
            total.nonFinite += h.nonFinite;
            total.peak = std::max (total.peak, h.peak);
        }
        check (total.nonFinite == 0 && total.peak < 20.0f,
               "128-event MIDI blocks x2000 stay healthy (peak " + String (total.peak, 3) + ")");
    }

    // 5. Prepare / release cycling, then a sane render afterwards.
    {
        auto p = makeManual();
        for (int i = 0; i < 50; ++i)
        {
            p->prepareToPlay (48000.0, 512);
            p->releaseResources();
        }
        p->setRateAndBufferSizeDetails (48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        const Health h = soak (*p, 5.0, rng, "after 50 prepare/release cycles");
        check (h.nonFinite == 0 && h.peak > 0.05f,
               "the engine regains its voice after re-initialisation");
    }

    // 6. One very long block: 4096-sample buffers go through clean.
    {
        auto p = makeManual (48000.0, 4096);
        AudioBuffer<float> buf (2, 4096);
        Health total;
        for (int b = 0; b < 200; ++b)
        {
            buf.clear();
            MidiBuffer midi = (b % 5 == 0) ? randomMidi (rng, 16, 4096) : MidiBuffer();
            p->processBlock (buf, midi);
            const Health h = inspectBlock (buf);
            total.nonFinite += h.nonFinite;
            total.peak = std::max (total.peak, h.peak);
        }
        check (total.nonFinite == 0 && total.peak < 20.0f,
               "4096-sample blocks stay healthy");
    }

    // 7. Per-step velocities: the same line at full and low level keeps its
    //    relative loudness and the weak notes stay audible and finite.
    {        auto renderVel = [] (double vel)
        {
            auto p = makeManual (48000.0, 256);
            Sequence s;
            s.setNumSteps (8);
            s.steps.assign (8, static_cast<juce::uint16> (Sequence::kNoteBase + 0));
            s.baseMidi = 43;
            for (int i = 0; i < 8; ++i)
                s.setVelocity (i, static_cast<float> (vel));
            p->setSequence (s);
            AudioBuffer<float> buf (2, 256);
            double energy = 0.0; long count = 0;
            for (int b = 0; b < 200; ++b) // ~1.07 s
            {
                buf.clear();
                MidiBuffer midi;
                p->processBlock (buf, midi);
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (std::isfinite (v)) { energy += v * v; ++count; }
                }
            }
            return count > 0 ? std::sqrt (energy / count) : 0.0;
        };

        const double loud = renderVel (1.0);
        const double soft = renderVel (0.15f);
        check (loud > 0.001, "full velocity line audible (rms " + String (loud, 4) + ")");
        check (soft > 0.0001, "a low-velocity line still renders (rms " + String (soft, 5) + ")");
        check (soft < loud * 0.95, "low velocity is quieter than full (" + String (soft / loud, 2)
               + " of it)");
        check (soft > loud * 0.05, "the velocity floor keeps soft notes audible");
    }

    // 8. SEQ OFF: the sequencer stands down but the plugin stays a MIDI instrument;
    //    toggling it mid-play must not wedge the clock.
    {
        auto p = makeManual (48000.0, 256);
        if (auto* seq = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::seqOn)))
            seq->setValueNotifyingHost (0.0f);
        p->setSequence (soakSequence (rng, 16));

        AudioBuffer<float> buf (2, 256);
        Health seqOffLive;
        for (int b = 0; b < 500; ++b)
        {
            buf.clear();
            MidiBuffer midi;
            if (b % 10 == 3)
                midi.addEvent (MidiMessage::noteOn (1, rng.nextInt (Range<int> (40, 90)),
                                                    static_cast<juce::uint8> (rng.nextInt (Range<int> (30, 128)))), 0);
            p->processBlock (buf, midi);
            const Health h = inspectBlock (buf);
            seqOffLive.nonFinite += h.nonFinite;
            seqOffLive.nonzero += h.nonzero;
            seqOffLive.peak = std::max (seqOffLive.peak, h.peak);
        }
        check (seqOffLive.nonFinite == 0 && seqOffLive.nonzero > 0,
               "SEQ OFF still renders live MIDI notes (healthy, audible)");
        check (p->getPlayingStep() < 0, "the playhead stays parked while SEQ is off");

        // Toggling SEQ on/off under load, then back on: audio resumes.
        for (int i = 0; i < 8; ++i)
        {
            if (auto* seq = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::seqOn)))
                seq->setValueNotifyingHost (i % 2 == 0 ? 1.0f : 0.0f);
            const Health h = soak (*p, 0.5, rng, "seq toggle " + String (i));
            check (h.nonFinite == 0, "SEQ toggle round " + String (i) + " stays healthy");
        }

        if (auto* seq = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::seqOn)))
            seq->setValueNotifyingHost (1.0f);
        const Health resumed = soak (*p, 3.0, rng, "seq resumed");
        check (resumed.nonFinite == 0 && resumed.nonzero > 0,
               "the sequencer regains its voice after SEQ returns");
    }
// 8. Dedicated limiter stress: with the limiter engaged and its threshold /
    //    ceiling randomised mid-storm, the output stays finite and never crawls
    //    far past the ceiling. Replaces the wholesale stage exclusion.
    {
        Random limRng (0x1b57a17d);
        auto p = makeManual();
        p->setSequence (soakSequence (limRng, 16));
        if (auto* b = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::limiterOn)))
            b->setValueNotifyingHost (1.0f);
        if (auto* t = dynamic_cast<AudioParameterFloat*> (p->state().getParameter (ParamIDs::limiterThreshold)))
            t->setValueNotifyingHost (t->getNormalisableRange().convertTo0to1 (-6.0f));
        if (auto* c = dynamic_cast<AudioParameterFloat*> (p->state().getParameter (ParamIDs::limiterCeiling)))
            c->setValueNotifyingHost (c->getNormalisableRange().convertTo0to1 (-1.0f));

        AudioBuffer<float> buf (2, 512);
        Health total;
        for (int b = 0; b < 800; ++b)
        {
            buf.clear();
            MidiBuffer midi;
            if (b % 17 == 0)
            {
                randomizeSomeParams (*p, limRng, 3);
                if (auto* t2 = dynamic_cast<AudioParameterFloat*> (p->state().getParameter (ParamIDs::limiterThreshold)))
                {
                    const float v = t2->getNormalisableRange().convertTo0to1 (-12.0f + limRng.nextFloat() * 11.0f);
                    t2->setValueNotifyingHost (juce::jlimit (0.0f, 0.99f, v));
                }
                if (auto* c2 = dynamic_cast<AudioParameterFloat*> (p->state().getParameter (ParamIDs::limiterCeiling)))
                {
                    const float v = c2->getNormalisableRange().convertTo0to1 (-3.0f + limRng.nextFloat() * 2.5f);
                    c2->setValueNotifyingHost (juce::jlimit (0.0f, 0.99f, v));
                }
            }
            midi = (b % 5 == 0) ? randomMidi (limRng, 16, 512) : MidiBuffer();
            p->processBlock (buf, midi);
            const Health h = inspectBlock (buf);
            total.nonFinite += h.nonFinite;
            total.peak = std::max (total.peak, h.peak);
        }
        int ceilingClipCount = 0; (void)ceilingClipCount;
        check (total.nonFinite == 0, "the limiter soak: no non-finite samples");
        check (total.peak < 2.5f, "the limiter soak: peak stays near the ceiling (peak "
               + String (total.peak, 3) + ")");
        juce::ignoreUnused (p);
    }
}
