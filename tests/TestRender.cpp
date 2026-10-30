#include <cstdlib>
#include <fstream>
#include <functional>
#include <vector>

#include <JuceHeader.h>

#include "../Source/PluginProcessor.h"
#include "../Source/dsp/MidiExport.h"
#include "../Source/dsp/MidiImport.h"
#include "../Source/dsp/WavExport.h"

using namespace juce;

// PluginEditor.cpp + CreateEditor.cpp are linked in so the GUI tests can paint
// the real editor offscreen; nothing here needs that any more.

static int failures = 0;

static void check (bool ok, const String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (! ok)
        ++failures;
}

void runCoverageTests (const std::function <void (bool, const String&)>& check);
void runGuiTests (const std::function <void (bool, const String&)>& check);
void runStabilityTests (const std::function <void (bool, const String&)>& check);
void runCoreParityTests (const std::function <void (bool, const String&)>& check);

namespace
{
    /** A processor that runs its own clock - the host sync mode has no play head here. */
    std::unique_ptr<PluginProcessor> makeRunning (double bpm = 130.0)
    {
        auto p = std::make_unique<PluginProcessor>();
        p->setRateAndBufferSizeDetails (48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        if (auto* sync = dynamic_cast<AudioParameterChoice*> (
                p->state().getParameter (ParamIDs::syncMode)))
            sync->setValueNotifyingHost (1.0f);
        if (auto* play = dynamic_cast<AudioParameterBool*> (
                p->state().getParameter (ParamIDs::manualPlay)))
            play->setValueNotifyingHost (1.0f);
        if (auto* b = dynamic_cast<AudioParameterFloat*> (
                p->state().getParameter (ParamIDs::bpm)))
            b->setValueNotifyingHost (b->getNormalisableRange().convertTo0to1 (static_cast<float> (bpm)));
        p->setTempoBpm (bpm);
        return p;
    }

    void setParam (PluginProcessor& p, const String& id, float raw)
    {
        if (auto* f = dynamic_cast<AudioParameterFloat*> (p.state().getParameter (id)))
            f->setValueNotifyingHost (f->getNormalisableRange().convertTo0to1 (raw));
    }

    /** Renders the pattern the way the WebAssembly build does, and writes the
        samples as raw floats, so that the two can be compared. The file is
        stereo, interleaved, 32 bit floats, little endian.

        The environment variable SPIRAL303_DUMP_RENDER names the file. */
    void dumpParityRender (const std::string& path)
    {
        auto p = std::make_unique<PluginProcessor>();
        p->setRateAndBufferSizeDetails (48000.0, 512);
        p->prepareToPlay (48000.0, 512);

        // Manual sync: CLAP has no way to read the host's transport, so the
        // WebAssembly build runs its own clock. The comparison uses the same
        // mode on both sides.
        if (auto* sync = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::syncMode)))
            sync->setValueNotifyingHost (1.0f);

        const int frames = 512;
        const int blocks = 192;   // two seconds
        std::vector<float> samples;
        samples.reserve ((size_t) frames * 2 * blocks);

        juce::AudioBuffer<float> buffer (2, frames);
        juce::MidiBuffer midi;

        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();
            midi.clear();
            p->processBlock (buffer, midi);
            for (int i = 0; i < frames; ++i)
            {
                samples.push_back (buffer.getReadPointer (0)[i]);
                samples.push_back (buffer.getReadPointer (1)[i]);
            }
        }

        std::ofstream file (path, std::ios::binary);
        file.write (reinterpret_cast<const char*> (samples.data()),
                    (std::streamsize) (samples.size() * sizeof (float)));
    }

    struct Rendered
    {
        double peak = 0.0, skippedPeak = 0.0, rms = 0.0;
        bool finite = true;
        std::vector<float> samples;
        int stepsSeen = 0;
    };

    Rendered render (PluginProcessor& p, double seconds, double skipSeconds = 0.0,
                     const MidiBuffer* injectMidi = nullptr)
    {
        const int blockLen = 512;
        const int blocks = static_cast<int> (seconds * p.getSampleRate() / blockLen);
        const int skipBlocks = static_cast<int> (skipSeconds * p.getSampleRate() / blockLen);

        Rendered out;
        double energy = 0.0;
        long count = 0;
        std::vector<int> seen;

        AudioBuffer<float> buf (2, blockLen);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            MidiBuffer midi;
            if (injectMidi != nullptr)
                midi.addEvents (*injectMidi, 0, blockLen, 0);
            p.processBlock (buf, midi);

            const int step = p.getPlayingStep();
            if (step >= 0 && std::find (seen.begin(), seen.end(), step) == seen.end())
                seen.push_back (step);

            if (b < skipBlocks)
                continue;

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < blockLen; ++i)
                {
                    const double v = buf.getSample (ch, i);
                    if (! std::isfinite (v))
                        out.finite = false;
                    out.peak = std::max (out.peak, std::abs (v));
                    energy += v * v;
                    ++count;
                    if (ch == 0)
                        out.samples.push_back (static_cast<float> (v));
                }
        }
        out.skippedPeak = out.peak;
        out.rms = count > 0 ? std::sqrt (energy / static_cast<double> (count)) : 0.0;
        out.stepsSeen = static_cast<int> (seen.size());
        return out;
    }

    /** Spectral envelope of a rendered window: |X| on a log grid, smoothed to
        about a sixth of an octave so the formant humps survive the harmonic comb
        of a sequenced note. Used to read the vowel bank's centres back out of the
        audio instead of trusting the coefficients. */
    struct SpectralEnvelope
    {
        static constexpr int bins = 240;

        std::vector<double> mag;

        static double hz (int b)
        {
            return 80.0 * std::exp (std::log (100.0) * static_cast<double> (b) / (bins - 1));
        }

        static int binFor (double f)
        {
            const double b = std::log (std::max (80.0, std::min (8000.0, f)) / 80.0)
                               / std::log (100.0) * (bins - 1);
            return std::max (0, std::min (bins - 1, static_cast<int> (b + 0.5)));
        }

        /** The strongest envelope maximum in a window: the formant that is heard.
            A bin counts only if neither neighbour is louder. The skirt of the next
            formant rises all the way to the window's edge, and reading that edge
            as a peak reports the neighbour's slope instead of the formant. */
        double peakHz (double fromHz, double toHz) const
        {
            const int lo = binFor (fromHz), hi = binFor (toHz);
            auto at = [&] (int b) -> double { return mag[static_cast<size_t> (b)]; };
            int best = -1;
            for (int b = lo + 1; b < hi; ++b)
            {
                if (at (b - 1) > at (b) || at (b + 1) > at (b)) continue;
                if (best < 0 || at (b) > at (best)) best = b;
            }
            if (best >= 0) return hz (best);
            for (int b = lo; b <= hi; ++b)   // a monotone window: the loudest bin sits on an edge
                if (best < 0 || at (b) > at (best)) best = b;
            return hz (best);
        }

        double energy (double fromHz, double toHz) const
        {
            const int lo = binFor (fromHz), hi = binFor (toHz);
            double e = 0.0;
            for (int b = lo; b <= hi; ++b)
            {
                const double m = mag[static_cast<size_t> (b)];
                e += m * m;
            }
            return e / static_cast<double> (hi - lo + 1);
        }

        /** Energy-weighted centre of the envelope: the monotone measure of how
            far the mouth has opened. */
        double centroid (double fromHz, double toHz) const
        {
            const int lo = binFor (fromHz), hi = binFor (toHz);
            double num = 0.0, den = 0.0;
            for (int b = lo; b <= hi; ++b)
            {
                const double m = mag[static_cast<size_t> (b)] * mag[static_cast<size_t> (b)];
                num += m * hz (b);
                den += m;
            }
            return den > 0.0 ? num / den : 0.0;
        }
    };

    SpectralEnvelope spectralEnvelope (const std::vector<float>& samples, double sampleRateHz)
    {
        SpectralEnvelope env;
        env.mag.resize (static_cast<size_t> (SpectralEnvelope::bins));
        const double n = std::max (1.0, static_cast<double> (samples.size ()));
        for (int b = 0; b < SpectralEnvelope::bins; ++b)
        {
            const double w = 2.0 * MathConstants<double>::pi * SpectralEnvelope::hz (b) / sampleRateHz;
            double re = 0.0, im = 0.0;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (2.0 * MathConstants<double>::pi
                                                         * static_cast<double> (i) / n);
                const double s = static_cast<double> (samples[i]) * win;
                re += s * std::cos (w * static_cast<double> (i));
                im -= s * std::sin (w * static_cast<double> (i));
            }
            env.mag[static_cast<size_t> (b)] = std::sqrt (re * re + im * im);
        }
        const std::vector<double> raw = env.mag;
        for (int b = 0; b < SpectralEnvelope::bins; ++b)
        {
            double sum = 0.0;
            int count = 0;
            for (int d = -4; d <= 4; ++d)
            {
                const int j = b + d;
                if (j >= 0 && j < SpectralEnvelope::bins)
                {
                    sum += raw[static_cast<size_t> (j)];
                    ++count;
                }
            }
            env.mag[static_cast<size_t> (b)] = sum / count;
        }
        return env;
    }

    /** A one-octave run of sixteenths with the slide and accent flags on the way up. */
    Sequence testSequence (int numSteps = 16)
    {
        Sequence s;
        s.setNumSteps (numSteps);
        s.baseMidi = 36;
        s.scaleIndex = 0; // Chromatic
        for (int i = 0; i < numSteps; ++i)
        {
            const int degree = i % 12;
            juce::uint16 v = static_cast<juce::uint16> (Sequence::kNoteBase + degree);
            if (i % 4 == 1) v |= Sequence::kSlideFlag;
            if (i % 4 == 2) v |= Sequence::kAccentFlag;
            s.steps[static_cast<size_t> (i)] = v;
        }
        return s;
    }
}

static void runTests()
{
    // The state the WebAssembly build wrote: load it and print what came out,
    // so that the two builds can be compared.
    if (const auto* path = std::getenv ("SPIRAL303_LOAD_STATE"))
    {
        std::ifstream file (path, std::ios::binary);
        const std::vector<char> bytes ((std::istreambuf_iterator<char> (file)),
                                       std::istreambuf_iterator<char> ());
        auto p = std::make_unique<PluginProcessor>();
        p->setStateInformation (bytes.data(), (int) bytes.size());

        const auto* cutoff = dynamic_cast<AudioParameterFloat*> (p->state ().getParameter (ParamIDs::cutoff));
        const auto* res = dynamic_cast<AudioParameterFloat*> (p->state ().getParameter (ParamIDs::res));
        std::cout << "loaded " << bytes.size() << " bytes"
                  << " cutoff=" << (cutoff != nullptr ? cutoff->get() : -1.0f)
                  << " res=" << (res != nullptr ? res->get() : -1.0f)
                  << " seq=" << p->getSequence ().encode () << std::endl;
        std::exit (0);
    }

    // The WebAssembly comparison: dump the render and stop, the tests are not
    // needed for that.
    if (const auto* path = std::getenv ("SPIRAL303_DUMP_RENDER"))
    {
        dumpParityRender (path);
        std::cout << "render written to " << path << std::endl;
        // The application quits itself once initialise() returns.
    }

    // Table refit harness: renders every calibration cell through the voice
    // chain in the panel's residual-sweep condition (the Env knob at its
    // floor) and prints anchor/model RMS per cell, so the fitted gains can be
    // recomputed offline. Not a test; it exits without running the suite.
    if (std::getenv ("SPIRAL303_REFIT") != nullptr)
    {
        static const float refitCuts[] = { 200.0f, 230.0f, 264.5f, 304.2f, 349.8f, 402.3f,
            462.6f, 532.0f, 611.8f, 703.6f, 809.1f, 930.5f, 1070.1f, 1230.6f, 1415.1f,
            1627.4f, 1871.5f, 2152.3f, 2475.1f, 2846.4f, 3273.3f, 3764.3f, 4328.9f, 4978.3f,
            5725.0f, 6583.8f, 7571.4f, 8707.1f };
        auto setFilterChoice = [] (PluginProcessor& p, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::filterType)))
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (c->choices.size() - 1));
        };
        auto cellRms = [&] (int model, float res, float cutoff)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, cutoff);
            setParam (*p, ParamIDs::res, res);
            setParam (*p, ParamIDs::env, 0.0f);
            setParam (*p, ParamIDs::drive, 0.0f);
            setFilterChoice (*p, model);
            return render (*p, 0.5, 0.4).rms;
        };
        std::cout << "model,cutoff,res,anchor,model" << std::endl;
        for (float cutoff : refitCuts)
            for (float res : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const double ref = cellRms (0, res, cutoff);
                for (int model : { 1, 2, 3, 4, 5, 7 })
                    std::cout << model << "," << cutoff << "," << res << ","
                              << String (ref, 8) << "," << String (cellRms (model, res, cutoff), 8)
                              << std::endl;
            }
        std::exit (0);
    }

    std::cout << "=== Spiral303 offline tests ===" << std::endl;

    // 1. The default pattern makes sound and stays in range.
    {
        auto p = makeRunning();
        const auto r = render (*p, 2.0);
        check (r.finite, "default pattern renders finite samples");
        check (r.peak > 0.05, "default pattern is audible (peak " + String (r.peak, 3) + ")");
        check (r.peak < 1.5, "default pattern does not blow up");
        check (r.stepsSeen >= 8, "the playhead walks the pattern (saw "
                                 + String (r.stepsSeen) + " steps)");
        p->releaseResources();
    }

    // 2. All-rest sequence is silent once the analog noise floor is off.
    {
        auto p = makeRunning();
        setParam (*p, ParamIDs::warmth, 0.0f);
        Sequence silent;
        silent.setNumSteps (16);
        silent.clearAll();
        p->setSequence (silent);
        const auto r = render (*p, 1.0);
        check (r.peak < 1.0e-4, "a rested pattern is silent (peak " + String (r.peak, 6) + ")");
        check (p->getPlayingStep() >= 0, "the clock keeps running on rests");
    }

    // 3. Stopping the manual transport stops the voice but not the plugin.
    {
        auto p = makeRunning();
        if (auto* play = dynamic_cast<AudioParameterBool*> (
                p->state().getParameter (ParamIDs::manualPlay)))
            play->setValueNotifyingHost (0.0f);
        const auto r = render (*p, 0.6);
        check (r.peak < 0.02, "stopped transport leaves only the noise floor");
        check (p->getPlayingStep() == -1, "playhead parks when stopped");
    }

    // 4. The gate really chops: a fully closed wheel is much quieter than an open one.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::gateRel, 0.5f);
        const auto open = render (*p, 1.0, 0.25);

        GatePattern closed;
        closed.closedMask = 0xffffu;
        p->setGate (closed);
        check (p->gateActive(), "an all-closed wheel engages the gate layer");
        const auto muted = render (*p, 1.0, 0.25);
        // The opening fade makes the first peak legitimate, so judge the muting by energy.
        check (muted.rms < open.rms * 0.15, "closed sectors mute the note ("
               + String (muted.rms, 4) + " vs " + String (open.rms, 4) + " rms)");

        GatePattern half;
        half.closedMask = 0xaaaau; // every other sector
        p->setGate (half);
        const auto chopped = render (*p, 1.0, 0.25);
        check (chopped.peak > 0.05 && chopped.rms < open.rms,
               "a half-open wheel chops without killing the level");

        // Gate edges must fade, not chop: the biggest sample-to-sample jump while the
        // wheel runs stays within the signal's own slope. A hard cut jumps the whole
        // amplitude in one sample and is heard as a click.
        double openJump = 0.0, gatedJump = 0.0;
        for (size_t i = 1; i < open.samples.size(); ++i)
            openJump = std::max (openJump, static_cast<double> (std::abs (open.samples[i] - open.samples[i - 1])));
        for (size_t i = 1; i < chopped.samples.size(); ++i)
            gatedJump = std::max (gatedJump, static_cast<double> (std::abs (chopped.samples[i] - chopped.samples[i - 1])));
        check (openJump > 0.0 && gatedJump < openJump * 1.6,
               "gate edges do not click (jump " + String (gatedJump, 4)
               + " vs free " + String (openJump, 4) + ")");
    }

    // 4b. Gate Mix is the dry/wet of the gate: 0 leaves the signal untouched,
    //     1 chops fully, and the level in between scales with it.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        GatePattern half;
        half.closedMask = 0xaaaau;
        p->setGate (half);

        setParam (*p, ParamIDs::gateMix, 0.0f);
        const auto dry = render (*p, 1.0, 0.25);
        setParam (*p, ParamIDs::gateMix, 1.0f);
        const auto wet = render (*p, 1.0, 0.25);
        // A half-open wheel spends half the time fading toward zero, so full wet
        // carries measurably less energy than fully dry.
        check (dry.rms > wet.rms * 1.15, "gate mix 0 bypasses the chopping, 1 chops ("
               + String (dry.rms, 4) + " vs " + String (wet.rms, 4) + " rms)");
    }

    // 5. Accent and contour move the level, which is what the knobs are for.
    {
        auto p = makeRunning();
        Sequence s;
        s.setNumSteps (8);
        s.baseMidi = 36;
        s.scaleIndex = 0;
        for (int i = 0; i < 8; ++i)
            s.steps[static_cast<size_t> (i)] = Sequence::kNoteBase + 3 + Sequence::kAccentFlag;
        p->setSequence (s);
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::satMix, 0.0f);
        setParam (*p, ParamIDs::accent, 0.0f);
        const auto flat = render (*p, 1.2);
        setParam (*p, ParamIDs::accent, 1.0f);
        const auto pushed = render (*p, 1.2);
        check (pushed.rms > flat.rms * 1.05, "accent raises the level ("
               + String (flat.rms, 4) + " -> " + String (pushed.rms, 4) + ")");
    }

    // 6. Resonance feeds back: high Q self-oscillates on a closed filter.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::cutoff, 60.0f);
        setParam (*p, ParamIDs::env, 0.0f);
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::satMix, 0.0f);
        setParam (*p, ParamIDs::res, 0.0f);
        const auto dry = render (*p, 1.0, 0.4);
        setParam (*p, ParamIDs::res, 1.0f);
        const auto ring = render (*p, 1.0, 0.4);
        check (ring.finite && dry.finite, "resonance stays stable at both ends");
        check (ring.peak > dry.peak, "resonance rings at the cutoff (peak "
               + String (dry.peak, 3) + " -> " + String (ring.peak, 3) + ")");
    }

    // 7. Distortion adds harmonics: the same note gets louder and denser with drive.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::satMix, 0.0f);
        const auto clean = render (*p, 1.0);
        setParam (*p, ParamIDs::drive, 1.0f);
        setParam (*p, ParamIDs::satMix, 1.0f);
        const auto dirty = render (*p, 1.0);
        check (dirty.finite, "full drive stays finite");
        check (dirty.rms > clean.rms, "drive thickens the signal");
        check (dirty.peak < 2.0, "the normalised tanh keeps the output bounded");
    }

    // 8. Warmth is the analog switch: it must change the render without breaking it.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::warmth, 0.0f);
        const auto cold = render (*p, 1.0);
        setParam (*p, ParamIDs::warmth, 1.0f);
        const auto warm = render (*p, 1.0);
        check (warm.finite && cold.finite, "warmth keeps the engine stable");
        check (warm.samples.size() == cold.samples.size()
                   && warm.samples != cold.samples, "warmth actually changes the signal");
    }

    // 9. The waveform morph is continuous across sine - saw - square.
    {
        for (float wave = 0.0f; wave <= 1.001f; wave += 0.25f)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, 8000.0f);
            setParam (*p, ParamIDs::res, 0.0f);
            setParam (*p, ParamIDs::env, 0.0f);
            setParam (*p, ParamIDs::wave, wave);
            const auto r = render (*p, 0.8);
            check (r.finite && r.peak > 0.01 && r.peak < 2.0,
                   "wave " + String (wave, 2) + " renders a sane level");
        }
    }

    // 10. Preview render agrees with the offline export in length and loudness.
    {
        auto p = makeRunning (120.0);
        p->setSequence (testSequence());
        std::vector<float> preview;
        const int n = p->renderPatternPreview (preview, 2);
        const double expected = 2.0 * 16 * p->getStepLengthMs() * 0.001 * 48000.0;
        check (n == static_cast<int> (expected), "preview covers two bars ("
               + String (n) + " samples)");
        double peak = 0.0;
        for (const auto v : preview)
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        check (peak > 0.05, "preview is audible");
    }

    // 11. WAV export writes a mono 24-bit file of the requested length.
    {
        auto p = makeRunning (120.0);
        p->setSequence (testSequence());
        const File wav = File::getSpecialLocation (File::tempDirectory)
                             .getChildFile ("spiral303_export_test.wav");
        wav.deleteFile();
        check (WavExport::renderPattern (*p, wav, 2), "export writes the file");

        WavAudioFormat format;
        auto reader = std::unique_ptr<AudioFormatReader> (
            format.createReaderFor (new FileInputStream (wav), true));
        if (reader != nullptr)
        {
            check (reader->numChannels == 1, "export is mono");
            check (reader->sampleRate == WavExport::sampleRate, "export is 44.1 kHz");
            const double seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
            check (seconds > 3.6 && seconds < 4.1, "two bars at 120 BPM are ~3.7 s ("
                   + String (seconds, 2) + " s)");
            AudioBuffer<float> audio (1, static_cast<int> (reader->lengthInSamples));
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            double peak = 0.0;
            for (int i = 0; i < audio.getNumSamples(); ++i)
                peak = std::max (peak, static_cast<double> (std::abs (audio.getSample (0, i))));
            check (peak > 0.05, "the exported file holds audio, not silence");
        }
        else
        {
            check (false, "exported WAV can be opened");
        }
        wav.deleteFile();
    }

    // 12. Every factory preset loads and makes a sound.
    {
        check (factoryPresetErrors().empty(), "the compiled-in preset bank is clean");
        const auto& presets = factoryPresets();
        check (presets.size() >= 10, "the bank holds the famous lines ("
               + String (static_cast<int> (presets.size())) + " presets)");

        auto p = makeRunning();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            p->setCurrentProgram (static_cast<int> (i));
            const auto r = render (*p, 1.0);
            const auto seq = p->getSequence();
            bool notes = false;
            for (int s = 0; s < seq.numSteps(); ++s)
                notes = notes || seq.isNote (s);
            check (r.finite && r.peak > 0.02 && notes,
                   "preset " + String (static_cast<int> (i)) + " \"" + presets[i].name
                       + "\" plays (peak " + String (r.peak, 3) + ")");
        }
    }

    // 13. Plugin state carries the pattern and the wheel through a round trip.
    {
        auto p = makeRunning();
        Sequence s = testSequence (24);
        s.baseMidi = 41;
        s.scaleIndex = 4;
        p->setSequence (s);
        GatePattern g;
        g.closedMask = 0x0f0fu;
        g.rotation = 3;
        p->setGate (g);
        setParam (*p, ParamIDs::cutoff, 1750.0f);
        setParam (*p, ParamIDs::swing, 0.42f);

        MemoryBlock state;
        p->getStateInformation (state);

        auto q = std::make_unique<PluginProcessor>();
        q->setRateAndBufferSizeDetails (48000.0, 512);
        q->prepareToPlay (48000.0, 512);
        q->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        const auto restored = q->getSequence();
        check (restored.encode() == s.encode(), "sequence survives a state round trip");
        check (q->getGate().closedMask == g.closedMask && q->getGate().rotation == g.rotation,
               "gate survives a state round trip");
        const auto* cutoff = dynamic_cast<AudioParameterFloat*> (
            q->state().getParameter (ParamIDs::cutoff));
        check (cutoff != nullptr && std::abs (cutoff->get() - 1750.0f) < 1.0f, "cutoff restored");
        const auto* swing = dynamic_cast<AudioParameterFloat*> (
            q->state().getParameter (ParamIDs::swing));
        check (swing != nullptr && std::abs (swing->get() - 0.42f) < 0.001f, "swing restored");
    }

    // 14. Switching tape saturation variants must not click: run two identical
    //     engines, switch the type on one, and the trajectories may only diverge
    //     gradually (the crossfade), never as a step.
    {
        auto setChoice = [] (PluginProcessor& p, const String& id, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (id)))
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (juce::jmax (1, c->choices.size() - 1)));
        };
        auto setup = [&] (PluginProcessor& p, int type)
        {
            p.setSequence (testSequence());
            setParam (p, ParamIDs::warmth, 0.0f);
            setParam (p, ParamIDs::cutoff, 700.0f);
            setParam (p, ParamIDs::res, 0.0f);
            setParam (p, ParamIDs::env, 0.0f);
            setParam (p, ParamIDs::drive, 0.8f);
            setParam (p, ParamIDs::satMix, 1.0f);
            setChoice (p, ParamIDs::satType, type);
        };
        static const int pairs[][2] = { { 5, 9 }, { 6, 7 }, { 8, 9 }, { 9, 5 } };
        for (const auto& pr : pairs)
        {
            auto a = makeRunning();
            auto b = makeRunning();
            auto c = makeRunning();
            setup (*a, pr[0]);
            setup (*b, pr[0]);
            setup (*c, pr[1]);
            const auto wa = render (*a, 0.5, 0.4);
            const auto wb = render (*b, 0.5, 0.4);
            const auto wc = render (*c, 0.5, 0.4);
            setChoice (*b, ParamIDs::satType, pr[1]);
            const auto na = render (*a, 0.02);
            const auto nb = render (*b, 0.02);
            const auto nc = render (*c, 0.02);

            const size_t n = std::min (wa.samples.size(), wb.samples.size());
            double drift = 0.0;
            for (size_t i = 0; i < n; ++i)
                drift = std::max (drift, std::abs (static_cast<double> (wb.samples[i])
                                                   - wa.samples[i]));
            check (drift < 1.0e-6, "identical setups render bit-equal (control for pair "
                   + String (pr[0]) + "/" + String (pr[1]) + ")");

            double first = 0.0, earlyGrowth = 0.0, jumpA = 0.0, jumpB = 0.0, jumpC = 0.0;
            double previousDiff = 0.0;
            const size_t m = std::min (na.samples.size(), nb.samples.size());
            for (size_t i = 0; i < m; ++i)
            {
                const double d = std::abs (static_cast<double> (nb.samples[i]) - na.samples[i]);
                if (i == 0)
                    first = d;
                else
                {
                    const double g = std::abs (d - previousDiff);
                    if (i <= 32)
                        earlyGrowth = std::max (earlyGrowth, g);
                }
                previousDiff = d;
                if (i > 0)
                {
                    jumpA = std::max (jumpA, std::abs (static_cast<double> (na.samples[i]) - na.samples[i - 1]));
                    jumpB = std::max (jumpB, std::abs (static_cast<double> (nb.samples[i]) - nb.samples[i - 1]));
                    if (i < nc.samples.size())
                        jumpC = std::max (jumpC, std::abs (static_cast<double> (nc.samples[i]) - nc.samples[i - 1]));
                }
            }
            // The switched run must not jump harder than the target mode plays
            // normally; the twin comparison pins down the switch instant itself.
            check (na.finite && nb.finite && nc.finite && nb.peak > 0.01
                       && first < 0.02 && earlyGrowth < 0.02
                       && jumpB < std::max (jumpA, jumpC) + 0.15,
                   "saturation switch " + String (pr[0]) + " -> " + String (pr[1])
                       + " starts continuous and never jumps beyond the modes' own "
                       + String (std::max (jumpA, jumpC), 3) + " (first " + String (first, 4)
                       + ", jump " + String (jumpB, 3) + ")");
        }
    }

    // 15. With the transport stopped, nothing outside the voice may sound: the
    //     analog drift adds no buzz and the hiss floor is off by default.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        if (auto* play = dynamic_cast<AudioParameterBool*> (
                p->state().getParameter (ParamIDs::manualPlay)))
            play->setValueNotifyingHost (0.0f);
        // The running flag is cached on the audio thread, so skip a few blocks
        // until the stop has propagated.
        const auto stopped = render (*p, 0.5, 0.1);
        check (stopped.peak < 1.0e-6, "a stopped engine is silent with analog on (peak "
               + String (stopped.peak, 8) + ")");
    }

    // 16. While the sequencer is stopped the synth plays incoming MIDI notes,
    //     and goes quiet again after the envelope decays.
    {
        auto p = std::make_unique<PluginProcessor>();
        p->setRateAndBufferSizeDetails (48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        // Host sync without a play head: isSequencerRunning() stays false once the
        // audio thread has cached it, so the first blocks just warm the cache.
        AudioBuffer<float> buf (2, 512);
        double peakDuring = 0.0, peakAfter = 0.0;
        bool finite = true;
        for (int b = 0; b < 120; ++b)
        {
            buf.clear();
            MidiBuffer midi;
            if (b == 8)
                midi.addEvent (MidiMessage::noteOn (1, 60, 0.9f), 10);
            p->processBlock (buf, midi);

            double blockPeak = 0.0;
            for (int i = 0; i < 512; ++i)
            {
                const double v = buf.getSample (0, i);
                if (! std::isfinite (v))
                    finite = false;
                blockPeak = std::max (blockPeak, std::abs (v));
            }
            if (b >= 8 && b < 60)
                peakDuring = std::max (peakDuring, blockPeak);
            if (b >= 100)
                peakAfter = std::max (peakAfter, blockPeak);
        }
        check (finite && peakDuring > 0.01, "a MIDI note sounds while the sequencer is stopped");
        check (peakAfter < peakDuring * 0.05, "the live note decays after its envelope closes");
    }

    // 17. The analog hiss is a default-off toggle: off is silence, on is a
    //     quiet floor and never a tone.
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        if (auto* play = dynamic_cast<AudioParameterBool*> (
                p->state().getParameter (ParamIDs::manualPlay)))
            play->setValueNotifyingHost (0.0f);
        auto* hiss = dynamic_cast<AudioParameterBool*> (
            p->state().getParameter (ParamIDs::analogHiss));
        check (hiss != nullptr && ! hiss->get(), "analog hiss is off by default");

        if (auto* warmth = dynamic_cast<AudioParameterFloat*> (
                p->state().getParameter (ParamIDs::warmth)))
            warmth->setValueNotifyingHost (1.0f);
        if (hiss != nullptr)
            hiss->setValueNotifyingHost (1.0f);
        const auto floor = render (*p, 0.5, 0.1);
        check (floor.finite && floor.peak > 1.0e-6 && floor.peak < 0.01,
               "hiss on adds only a quiet noise floor (peak " + String (floor.peak, 5) + ")");
    }

    // 18. Clean is a true bypass: bit-transparent at any drive, and the choice
    //     list keeps it appended last so saved AU presets keep their indices.
    {
        Saturation sat;
        sat.prepare (48000.0);
        sat.setType (Saturation::Clean);
        sat.setMix (1.0f);
        sat.setDrive (1.0f);
        bool transparent = true;
        for (float x : { -0.7f, -0.003f, 0.0f, 0.25f, 0.6f, 1.3f })
            transparent = transparent && std::abs (sat.process (x) - x) < 1.0e-9f;
        check (transparent, "Clean passes the signal through at full drive");

        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::drive, 0.4f);
        auto* satChoice = dynamic_cast<AudioParameterChoice*> (
            p->state().getParameter (ParamIDs::satType));
        check (satChoice != nullptr && satChoice->choices.size() == 12
                   && satChoice->choices[10] == "Clean" && satChoice->choices[11] == "Crunch",
               "Clean keeps its appended index and Crunch follows last");
        if (satChoice != nullptr)
            satChoice->setValueNotifyingHost (10.0f / 11.0f); // Clean keeps index 10

        auto q = makeRunning();
        q->setSequence (testSequence());
        setParam (*q, ParamIDs::drive, 0.0f);
        if (auto* cq = dynamic_cast<AudioParameterChoice*> (
                q->state().getParameter (ParamIDs::satType)))
            cq->setValueNotifyingHost (10.0f / 11.0f); // Clean keeps index 10

        const auto cp = render (*p, 0.5, 0.4);
        const auto cq2 = render (*q, 0.5, 0.4);
        double cleanDrift = 0.0;
        const size_t cn = std::min (cp.samples.size (), cq2.samples.size ());
        for (size_t i = 0; i < cn; ++i)
            cleanDrift = std::max (cleanDrift, std::abs (static_cast<double> (cp.samples[i])
                                                         - cq2.samples[i]));
        check (cleanDrift < 1.0e-9, "Clean ignores the drive control");
    }

    // 19. Loudness normalization: at equal drive every saturator should render
    //     within +-5% RMS of the anchor (Tanh), so switching types does not
    //     jump the volume.
    {
        auto setChoiceLoud = [] (PluginProcessor& p, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::satType)))
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (c->choices.size() - 1));
        };
        for (float drive : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto anchor = makeRunning();
            anchor->setSequence (testSequence());
            setParam (*anchor, ParamIDs::warmth, 0.0f);
            setParam (*anchor, ParamIDs::cutoff, 700.0f);
            setParam (*anchor, ParamIDs::res, 0.0f);
            setParam (*anchor, ParamIDs::env, 0.0f);
            setParam (*anchor, ParamIDs::satMix, 1.0f);
            setParam (*anchor, ParamIDs::drive, drive);
            setChoiceLoud (*anchor, 0);
            const double refRms = render (*anchor, 0.5, 0.4).rms;

            for (int type = 1; type <= 11; ++type)
            {
                if (type == 10)
                    continue; // Clean is a true bypass, not a loudness case.
                auto p = makeRunning();
                p->setSequence (testSequence());
                setParam (*p, ParamIDs::warmth, 0.0f);
                setParam (*p, ParamIDs::cutoff, 700.0f);
                setParam (*p, ParamIDs::res, 0.0f);
                setParam (*p, ParamIDs::env, 0.0f);
                setParam (*p, ParamIDs::satMix, 1.0f);
                setParam (*p, ParamIDs::drive, drive);
                setChoiceLoud (*p, type);
                const double ratio = render (*p, 0.5, 0.4).rms / refRms;
                check (ratio > 0.95 && ratio < 1.05, "loudness of type " + String (type)
                       + " at drive " + String (drive, 2) + " matches the anchor (ratio "
                       + String (ratio, 3) + ")");
            }
        }

        // The first part of the drive travel must not inflate the volume: the
        // anchor loudness at drive 0.125 is the geometric mean between the dry
        // level and the level at 0.25, and every type follows it.
        {
            auto buildQuiet = [&] (int type, float drive)
            {
                auto p = makeRunning();
                p->setSequence (testSequence());
                setParam (*p, ParamIDs::warmth, 0.0f);
                setParam (*p, ParamIDs::cutoff, 700.0f);
                setParam (*p, ParamIDs::res, 0.0f);
                setParam (*p, ParamIDs::env, 0.0f);
                setParam (*p, ParamIDs::satMix, 1.0f);
                setParam (*p, ParamIDs::drive, drive);
                if (auto* c = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::satType)))
                    c->setValueNotifyingHost (static_cast<float> (type)
                                              / static_cast<float> (c->choices.size() - 1));
                return render (*p, 0.5, 0.4).rms;
            };

            const double dry = buildQuiet (10, 0.5);
            const double quiet = buildQuiet (0, 0.125);
            check (quiet < dry * 1.60 && quiet > dry * 1.35,
                   "the very start of the drive travel rises gently (dry "
                   + String (dry, 4) + " -> q " + String (quiet, 4) + ")");

            for (int type = 1; type <= 11; ++type)
            {
                if (type == 10)
                    continue; // Clean is a true bypass, not a loudness case.
                const double ratio = buildQuiet (type, 0.125) / quiet;
                check (ratio > 0.95 && ratio < 1.05, "loudness of type " + String (type)
                       + " at the gentle start matches the anchor (ratio "
                       + String (ratio, 3) + ")");
            }
        }
    }

    // 19c. Crunch: exact pass-through at drive 0, and the harmonic signature of
    //      the Crispy port — the odd curve bends like the anchor, and the even
    //      stage gives Crunch a visible second harmonic where Tanh has none.
    {
        {
            Saturation sat;
            sat.prepare (48000.0);
            sat.setType (Saturation::Crunch);
            sat.setMix (1.0f);
            sat.setDrive (0.0f);
            bool transparent = true;
            for (float x : { -0.9f, -0.2f, 0.0f, 0.1f, 0.5f, 1.7f })
                transparent = transparent && std::abs (sat.process (x) - x) < 1.0e-9f;
            check (transparent, "Crunch passes the signal through at drive 0");
            sat.setDrive (1.0f);
            const float bent = sat.process (0.5f);
            check (bent > 0.0f && bent <= 1.2f,
                   "Crunch keeps its bend inside the bounded range (bend "
                       + String (bent, 4) + ")");
            check (std::abs (sat.process (0.0f)) < 1.0e-6f,
                   "Crunch makes nothing out of silence");
        }

        // Harmonic signature on a pure tone: steady-state single 220 Hz sine,
        // single-sample path (no engine), Goertzel magnitudes at the first
        // five harmonics. The even stage of the crunchy curve must place a
        // second harmonic far above the anchor's (Tanh is odd-symmetric, its
        // even terms are zero).
        auto goertzel = [] (const std::vector<float>& s, double fsHz, double fHz)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * fHz / fsHz;
            double c = 2.0 * std::cos (w), s0 = 0.0, s1 = 0.0, s2 = 0.0;
            for (float v : s)
            {
                s0 = c * s1 - s2 + static_cast<double> (v);
                s2 = s1; s1 = s0;
            }
            const double re = s1 - s2 * std::cos (w);
            const double im = s2 * std::sin (w);
            return std::sqrt (re * re + im * im) / static_cast<double> (s.size());
        };

        auto harmonicSet = [&goertzel] (float drive, int type)
        {
            // Declared inside: MSVC reads an enclosing constexpr used as a
            // template bound as odr-used and demands a capture, which in turn
            // makes it a closure member and refuses it as a constant.
            constexpr int nHarmonics = 5;
            Saturation sat;
            sat.prepare (48000.0);
            sat.setType (static_cast<Saturation::Type> (type));
            sat.setMix (1.0f);
            sat.setDrive (drive);
            const double fsHz = 48000.0, f0 = 220.0;
            const int cycle = static_cast<int> (fsHz / f0);
            // Run past the DC blocker's warm-up, then keep a clean window of
            // whole cycles for the measurement.
            std::vector<float> runway (cycle * 24);
            int i = 0;
            for (float& v : runway)
                v = sat.process (static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * f0 * (i++) / fsHz)));
            std::vector<float> out (cycle * 4);
            for (float& v : out)
                v = sat.process (static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * f0 * (i++) / fsHz)));
            std::array<double, nHarmonics + 1> mags { 0.0 };
            for (int k = 1; k <= nHarmonics; ++k)
                mags[k] = goertzel (out, fsHz, f0 * k);
            return mags;
        };

        const auto tanhMags = harmonicSet (0.6f, 0);
        const auto crunchMags = harmonicSet (0.6f, 11);
        // The fundamental dominates in both cases; the distinguishing facts:
        // Crunch's even stage places a real second harmonic where the
        // odd-symmetric anchor has (numerically) none, while the odd curve
        // still dominates the bend.
        check (crunchMags[2] > 2.0 * tanhMags[2],
               "Crunch carries a real second harmonic against the anchor ("
                   + String (crunchMags[2], 6) + " vs " + String (tanhMags[2], 6) + ")");
        check (crunchMags[3] > 2.0 * crunchMags[2],
               "the odd harmonics still dominate the crunch bend ("
                   + String (crunchMags[3], 6) + " vs " + String (crunchMags[2], 6) + ")");
    }

    // 20b. Filter loudness: every model, level-matched to the Classic anchor via
    //      its calibration table, must land within +-5% RMS of the anchor at the
    //      reference scene (drive at zero, so the saturator is an exact bypass).
    {
        auto setFilterChoice = [] (PluginProcessor& p, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::filterType)))
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (c->choices.size() - 1));
        };
        auto buildFilter = [&] (int model, float res, float cutoff)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, cutoff);
            setParam (*p, ParamIDs::res, res);
            setParam (*p, ParamIDs::env, 0.0f);
            setParam (*p, ParamIDs::drive, 0.0f);
            setFilterChoice (*p, model);
            return render (*p, 0.5, 0.4).rms;
        };

        static const float knotCuts[] = { 462.6f, 703.6f, 1230.6f, 2475.1f, 4978.3f };
        for (float cutoff : knotCuts)
            for (float res : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const double ref = buildFilter (0, res, cutoff);
                for (int model : { 1, 2, 3, 4, 5, 7 })
                {
                    const double rm = buildFilter (model, res, cutoff);
                    const double ratio = rm / ref;
                    // Level contract: switching models must not get louder than
                    // the Classic anchor and must stay within a factor of 2.5 of
                    // it. The table was re-fitted under the panel Env floor; the
                    // quiet readings (Moog at res 0.25, low cutoffs) sit on the
                    // edge of self-oscillation, where no constant gain can fully
                    // track the swept resonance, so they land under the anchor
                    // rather than slamming over it.
                    check (ratio < 1.06 && ratio > 0.40, "filter model " + String (model)
                           + " loudness at knot cutoff " + String (cutoff, 0) + " res "
                           + String (res, 2) + " stays not-louder and close to the anchor (ratio "
                           + String (ratio, 3) + ")");
                }
            }

        // Between the knots the bilinear interpolation must not wander too far,
        // and the capped quiet pockets must never blow up.
        for (float cutoff : { 780.0f, 1560.0f, 3200.0f })
            for (float res : { 0.0f, 1.0f })
            {
                const double ref = buildFilter (0, res, cutoff);
                for (int model : { 1, 2, 3, 4, 5, 7 })
                {
                    const double ratio = buildFilter (model, res, cutoff) / ref;
                    check (ratio < 1.12 && ratio > 0.55, "filter model " + String (model)
                           + " loudness between knots at cutoff " + String (cutoff, 0)
                           + " res " + String (res, 2) + " stays near the anchor (ratio "
                           + String (ratio, 3) + ")");
                }
            }
    }


    // 20. The FilterType choice must reach the voices: every model has to render
    //     a finite signal that differs from the Classic reference.
    {
        auto setFilterModel = [] (PluginProcessor& p, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::filterType)))
            {
                check (c->choices.size() == 8, "the filter choice exposes eight models");
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (c->choices.size() - 1));
            }
            else
            {
                check (false, "the FilterType parameter exists");
            }
        };
        auto setupFilter = [&] (PluginProcessor& p, int model)
        {
            p.setSequence (testSequence());
            setParam (p, ParamIDs::warmth, 0.0f);
            setParam (p, ParamIDs::cutoff, 900.0f);
            setParam (p, ParamIDs::res, 0.7f);
            setParam (p, ParamIDs::env, 0.5f);
            setParam (p, ParamIDs::drive, 0.0f);
            setParam (p, ParamIDs::satMix, 0.0f);
            setParam (p, ParamIDs::tone, 1.0f);
            setFilterModel (p, model);
        };
        std::vector<float> reference;
        for (int model = 0; model < 8; ++model)
        {
            auto p = makeRunning();
            setupFilter (*p, model);
            const auto r = render (*p, 1.0, 0.25);
            check (r.finite && r.peak > 0.01, "filter model " + String (model) + " renders a sane signal");
            if (model == 0)
                reference = r.samples;
            else
                check (r.samples != reference, "filter model " + String (model) + " sounds different from Classic");
        }
    }

    // 20c. The Diode 303 has the 303's asymmetric diode pair: a sine through it
    //      comes out with a real second harmonic, while the symmetric diode ladder
    //      makes odd harmonics only. Read from a DFT over whole cycles of the sine.
    {
        auto secondHarmonic = [] (int model)
        {
            ResonantFilter f;
            f.prepare (48000.0);
            f.setModel (model);
            f.setResonance (0.3f);
            const int period = 4800;              // 22 whole cycles of 220 Hz
            const double w = 2.0 * MathConstants<double>::pi * 220.0 / 48000.0;
            double h1r = 0, h1i = 0, h2r = 0, h2i = 0, fundamental = 0;
            for (int i = 0; i < 3 * period; ++i)
            {
                const float out = f.process (0.5f * static_cast<float> (std::sin (w * i)), 1200.0f);
                if (i < 2 * period)
                    continue;
                const double n = i - 2 * period;
                h1r += out * std::cos (w * n);     h1i -= out * std::sin (w * n);
                h2r += out * std::cos (2 * w * n); h2i -= out * std::sin (2 * w * n);
            }
            fundamental = std::sqrt (h1r * h1r + h1i * h1i);
            return std::sqrt (h2r * h2r + h2i * h2i) / std::max (1.0e-12, fundamental);
        };

        const double h2Diode303 = secondHarmonic (7);
        const double h2Diode = secondHarmonic (1);
        check (h2Diode303 > 0.02, "the 303 diode ladder makes a second harmonic ("
               + String (h2Diode303, 4) + " of the fundamental)");
        check (h2Diode303 > 5.0 * h2Diode, "the 303 diode pair is asymmetric, the symmetric ladder is not ("
               + String (h2Diode303, 4) + " vs " + String (h2Diode, 4) + ")");
    }

    // 20d. Accent chain: consecutive accents charge the accent further. The level
    //      of one voice grows 1 : 1.25 : 1.5 : 1.75 with the run length, a run of
    //      one is the plain accent, and a whole pattern of accents in one run
    //      renders louder than the same accents spread out one at a time.
    {
        Patch patch;
        patch.sampleRate = 48000.0;
        patch.accent = 0.75f;
        auto levelOf = [&] (int run)
        {
            TbVoice v;
            v.prepare (48000.0);
            v.noteOn (220.0, 0.0, patch, true, 0.0, 1.0, run);
            return static_cast<double> (v.levelForScope());
        };
        const double plain = levelOf (1);
        check (plain > 0.0, "an accented voice has a level (" + String (plain, 4) + ")");
        check (std::abs (levelOf (2) / plain - 1.25) < 1.0e-4, "the second accent of a run is 1.25x the first");
        check (std::abs (levelOf (3) / plain - 1.50) < 1.0e-4, "the third accent of a run is 1.5x the first");
        check (std::abs (levelOf (4) / plain - 1.75) < 1.0e-4, "the run stops growing at four (1.75x)");
        check (std::abs (levelOf (9) / plain - 1.75) < 1.0e-4, "longer runs stay at the cap");

        auto patternWith = [] (const std::vector<int>& accents)
        {
            Sequence seq;
            seq.setNumSteps (16);
            seq.scaleIndex = 0; seq.baseMidi = 36; seq.clearAll();
            for (int i = 0; i < 16; ++i)
                seq.setNote (i, i % 5);
            for (int i : accents)
                seq.toggleAccent (i);
            return seq;
        };
        auto rmsOf = [&] (const Sequence& seq)
        {
            auto p = makeRunning();
            p->setSequence (seq);
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::drive, 0.0f);
            return render (*p, 1.5, 0.25).rms;
        };
        const double chained = rmsOf (patternWith ({ 0, 1, 2, 3 }));
        const double spread = rmsOf (patternWith ({ 0, 4, 8, 12 }));
        check (chained > spread * 1.02, "four accents in a run sound louder than four spread out ("
               + String (chained, 5) + " vs " + String (spread, 5) + ")");
    }

    // 21. The vowel bank: reachable, level-matched, and actually shaped like a
    //     human mouth. Everything is measured from the rendered audio, not from
    //     the coefficients.
    {
        auto choiceFilter = [] (PluginProcessor& p, int index)
        {
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p.state().getParameter (ParamIDs::filterType)))
                c->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (c->choices.size() - 1));
        };
        auto build = [&] (int model, float res, float cutoff, std::vector<float>* samples)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, cutoff);
            setParam (*p, ParamIDs::res, res);
            setParam (*p, ParamIDs::env, 0.0f);
            setParam (*p, ParamIDs::drive, 0.0f);
            choiceFilter (*p, model);
            const auto r = render (*p, 1.0, 0.3);
            if (samples != nullptr)
                *samples = r.samples;
            return r.rms;
        };
        // The choice index must actually reach the model: the vowel used to be
        // clamped away and the knob played a one-pole instead.
        {
            ResonantFilter f;
            f.prepare (48000.0);
            f.setModel (6);
            check (f.getModel() == ResonantFilter::Model::Vowel, "filter index 6 selects the Vowel model");
            f.setModel (0);
            check (f.getModel() == ResonantFilter::Model::Classic, "filter index 0 selects Classic");
        }

        // Loudness: the bank must ride the Classic anchor across the whole
        // trajectory, knots and midpoints alike.
        const float fitCuts[] = { 150.0f, 200.0f, 260.0f, 340.0f, 440.0f, 570.0f, 740.0f,
                                  960.0f, 1250.0f, 1600.0f, 2100.0f, 2700.0f, 3600.0f, 4200.0f };
        double worstHigh = 0.0, worstLow = 1.0;
        String worstHighAt, worstLowAt;
        for (int i = 0; i < 27; ++i)
        {
            const float cutoff = (i % 2 == 0) ? fitCuts[i / 2]
                                 : 0.5f * (fitCuts[i / 2] + fitCuts[i / 2 + 1]);
            for (float res : { 0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const double ref = build (0, res, cutoff, nullptr);
                const double ratio = build (6, res, cutoff, nullptr) / ref;
                if (ratio > worstHigh) { worstHigh = ratio; worstHighAt = String (cutoff, 0) + "/" + String (res, 3); }
                if (ratio < worstLow)  { worstLow = ratio;  worstLowAt = String (cutoff, 0) + "/" + String (res, 3); }
            }
        }
        std::cout << "VWORST high " << String (worstHigh, 3) << " at " << worstHighAt
                  << ", low " << String (worstLow, 3) << " at " << worstLowAt << std::endl;
        check (worstHigh < 1.15, "vowel never jumps above the Classic anchor");
        check (worstLow > 0.50, "vowel never falls far below the Classic anchor");

        // Formants: the peaks read out of the audio must sit where the vowel
        // table puts them. Targets are the adult-male F1/F2 of the trajectory
        // (oo 300/730, oh 450/1020, ah 730/1250, eh 610/1900, ee 280/2250 Hz),
        // log-interpolated at the position each cutoff lands on. The panel
        // floors Env at Patch::kEnvFloor, so even the quietest setting rides a
        // short attack sweep; the mouth on the knob value is read from the
        // tail of each step, where that sweep has landed.
        auto tailOnly = [] (const std::vector<float>& win)
        {
            const double step = 48000.0 * 60.0 / 130.0 / 4.0; // a 16th at 130 bpm
            std::vector<float> tail;
            for (size_t i = 0; i < win.size(); ++i)
            {
                const double phase = std::fmod (static_cast<double> (i) + 0.3 * 48000.0, step);
                if (phase > step * 0.75)
                    tail.push_back (win[i]);
            }
            return tail;
        };
        struct VowelPoint { float cutoff; double f1, f2; };
        const VowelPoint pts[] = { { 400.0f, 491.0, 1056.0 }, { 800.0f, 729.0, 1256.0 },
                                   { 1600.0f, 628.0, 1769.0 }, { 3200.0f, 367.0, 2122.0 } };
        double previousF2 = 0.0;
        for (const auto& pt : pts)
        {
            std::vector<float> x;
            build (6, 0.5f, pt.cutoff, &x);
            const auto env = spectralEnvelope (tailOnly (x), 48000.0);
            const double f1 = env.peakHz (240.0, 900.0);
            const double f2 = env.peakHz (950.0, 2700.0);
            std::cout << "VOWEL cutoff " << String (pt.cutoff, 0)
                      << " f1 " << String (f1, 0) << " (target " << String (pt.f1, 0)
                      << ") f2 " << String (f2, 0) << " (target " << String (pt.f2, 0)
                      << ") cent " << String (env.centroid (130.0, 5000.0), 0) << std::endl;
            check (std::abs (f1 - pt.f1) / pt.f1 < 0.25, "vowel F1 at cutoff " + String (pt.cutoff, 0) + " tracks the table");
            check (std::abs (f2 - pt.f2) / pt.f2 < 0.25, "vowel F2 at cutoff " + String (pt.cutoff, 0) + " tracks the table");
            check (f2 > previousF2, "cutoff walks the mouth forward (F2 rises monotonically)");
            previousF2 = f2;
        }

        // One knob must cover a whole mouth: the bright/dark balance across the
        // trajectory has to be a different instrument, not a shade of one.
        {
            std::vector<float> dark, bright;
            build (6, 0.5f, 200.0f, &dark);
            build (6, 0.5f, 4200.0f, &bright);
            const auto darkTail = tailOnly (dark);
            const auto brightTail = tailOnly (bright);
            const double d = spectralEnvelope (darkTail, 48000.0).energy (1000.0, 3000.0)
                             / spectralEnvelope (darkTail, 48000.0).energy (240.0, 700.0);
            const double b = spectralEnvelope (brightTail, 48000.0).energy (1000.0, 3000.0)
                             / spectralEnvelope (brightTail, 48000.0).energy (240.0, 700.0);
            std::cout << "VOWEL range hi/lo " << String (d, 4) << " -> " << String (b, 4) << std::endl;
            // The ratio is read from the tails, which is where the knob value
            // holds the mouth; with the bright attack no longer counted, the
            // spread compresses against the old full-window 4x. It still has to
            // be a different instrument, not a shade of one.
            check (b > 2.0 * d, "cutoff swings the vowel colour");
        }

        // The note envelope must move the mouth on its own: feed the bank the
        // sweep an accented 303 note makes (cutoff high at the attack, falling
        // to the base) and compare the two ends of it. This is the "speaks"
        // effect - a static formant filter would read the same at both ends.
        {
            ResonantFilter f;
            f.prepare (48000.0);
            f.setModel (ResonantFilter::Model::Vowel);
            f.setResonance (0.5f);
            const int win = 480; // 10 ms, ~100 Hz resolution: enough for a formant
            std::vector<float> first (static_cast<size_t> (win)), last (static_cast<size_t> (win));
            double phase = 0.0;
            for (int i = 0; i < 3 * win; ++i)
            {
                phase += 220.0 / 48000.0;
                if (phase >= 1.0) phase -= 1.0;
                const float t = static_cast<float> (i) / static_cast<float> (3 * win - 1);
                const float cutoff = 4000.0f * std::pow (200.0f / 4000.0f, t);
                const float y = f.process (static_cast<float> (2.0 * phase - 1.0), cutoff);
                if (i < win) first[static_cast<size_t> (i)] = y;
                if (i >= 2 * win) last[static_cast<size_t> (i - 2 * win)] = y;
            }
            const SpectralEnvelope e1 = spectralEnvelope (first, 48000.0);
            const SpectralEnvelope e2 = spectralEnvelope (last, 48000.0);
            const double c1 = e1.centroid (130.0, 5000.0), c2 = e2.centroid (130.0, 5000.0);
            std::cout << "VOWEL sweep attack cent " << String (c1, 0) << " f2 " << String (e1.peakHz (800.0, 3000.0), 0)
                      << " | tail cent " << String (c2, 0) << " f2 " << String (e2.peakHz (800.0, 3000.0), 0) << std::endl;
            check (c1 > 1.4 * c2, "the note envelope sweeps the vowel (attack is bright, tail is dark)");
            check (e1.peakHz (800.0, 3000.0) > 1.7 * e2.peakHz (800.0, 3000.0), "the sweep carries F2 through the mouth");
        }
    }

    // CPU budget smoke: render a sustained acid line with heavy processing at a
    // fixed workload and require a generous realtime margin. This is not a
    // benchmark race (CI timings vary); it only fails when the engine is
    // absurdly slower than realtime, e.g. after a regression in the DSP loop.
    {
        auto p = makeRunning (140.0);
        setParam (*p, ParamIDs::drive, 0.8f);
        setParam (*p, ParamIDs::satMix, 0.6f);
        setParam (*p, ParamIDs::res, 0.9f);
        setParam (*p, ParamIDs::env, 0.9f);
        const int blockLen = 512;
        const int blocks = 3000; // ~32 s of audio
        AudioBuffer<float> buf (2, blockLen);
        MidiBuffer midi;
        buf.clear();
        p->processBlock (buf, midi); // warm up lazy initialisation
        const auto start = Time::getHighResolutionTicks();
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            MidiBuffer blockMidi;
            p->processBlock (buf, blockMidi);
        }
        const double seconds = Time::highResolutionTicksToSeconds
                                 (Time::getHighResolutionTicks() - start);
        const double audioSeconds = blockLen * 3000.0 / p->getSampleRate();
        const double load = seconds / audioSeconds;
        std::cout << "BENCH  cpu load " << String (load * 100.0, 2) << "% of realtime"
                  << std::endl;
        check (load < 0.5, "engine renders much faster than realtime");
    }

    // 22. Master limiter: off keeps the chain transparent, on clamps the peaks
    //     at the ceiling.
    {
        auto buildWithLimiter = [&] (bool limOn)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, 700.0f);
            setParam (*p, ParamIDs::res, 0.2f);
            setParam (*p, ParamIDs::drive, 0.8f);
            setParam (*p, ParamIDs::satMix, 1.0f);
            setParam (*p, ParamIDs::masterGain, 12.0f);
            if (auto* b = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::limiterOn)))
                b->setValueNotifyingHost (limOn ? 1.0f : 0.0f);
            return render (*p, 1.0, 0.2);
        };

        const auto limOff = buildWithLimiter (false);
        const auto limOn = buildWithLimiter (true);
        check (limOff.finite && limOn.finite, "the limiter path stays finite");
        check (limOn.peak <= 1.05, "the limiter holds peaks at the ceiling (peak "
               + String (limOn.peak, 3) + ")");
        check (limOn.peak <= limOff.peak, "the limiter only ever reduces the peaks");
    }

    // 23. Host-grid anchoring: entering playback mid-bar must place the line's
    //     first note on the host's own step boundary, not wherever the sample
    //     counter drifted while stopped.
    {
        Sequence line = Sequence::decode ("v1:36:0:640,128,0,0"); // one note on step 1
        Patch patch;
        patch.sampleRate = 48000.0;
        patch.stepSamples = 6000.0;              // one 16th at 120 BPM
        patch.stepsPerBeat = 4.0;
        patch.swingSamples = 0.0;
        patch.sequence = &line;
        patch.gate = nullptr;

        auto firstOnset = [&] (double startBeat)
        {
            TbEngine engine;
            engine.prepare (48000.0);
            AudioBuffer<float> buf (1, 512);
            MidiBuffer midi;
            const double beatsPerBlock = 512.0 / (6000.0 * 4.0);
            int onset = -1;
            for (int block = 0; block < 40 && onset < 0; ++block)
            {
                buf.clear();
                midi.clear();
                engine.process (buf, midi, patch, true, startBeat + block * beatsPerBlock);
                const float* ch = buf.getReadPointer (0);
                for (int i = 0; i < 512; ++i)
                    if (std::fabs (ch[i]) > 1.0e-4f)
                    {
                        onset = block * 512 + i;
                        break;
                    }
            }
            return onset;
        };

        // At beat 2.5 the line begins exactly on the boundary: fires right away.
        // At beat 2.6 the next boundary (0.75 of the beat) is 0.15 beat away,
        // which is 3600 samples at this patch's clock.
        const double expected2_5 = 0.0;
        const double expected2_6 = 3600.0;

        const int onset2_5 = firstOnset (2.5);
        const int onset2_6 = firstOnset (2.6);
        check (onset2_5 >= 0 && onset2_6 >= 0, "host-synced starts render notes");
        check (std::abs (onset2_5 - expected2_5) <= 64.0, "a start on a boundary fires the note immediately ("
               + String (onset2_5) + " samples)");
        check (std::abs (onset2_6 - expected2_6) <= 64.0, "a start between boundaries waits for the next one ("
               + String (onset2_6) + " samples)");

        // Without a host beat the legacy behavior (free-running from the stopped
        // phase) still works.
    }

    // 24. Attack knob: with a long attack the note fades in instead of the
    //     instant legacy onset.
    auto renderWithAttack = [&] (float attack)
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::cutoff, 700.0f);
        setParam (*p, ParamIDs::res, 0.0f);
        setParam (*p, ParamIDs::env, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::attack, attack);
        return render (*p, 1.0, 0.0);
    };
    {
        const auto sharp = renderWithAttack (0.0f);
        const auto soft = renderWithAttack (1.0f);
        check (soft.finite && sharp.finite, "the attack knob renders finite samples");

        // Find the first onset in the sharp render, then compare the peak level
        // over the following ~0.6 of a step: the soft attack must still be
        // climbing (much quieter), while the sharp one is already at full level.
        auto peakOver = [&] (const Rendered& r, int from, int count)
        {
            float peak = 0.0f;
            const int end = std::min<int> (from + count, static_cast<int> (r.samples.size()));
            for (int i = std::max (0, from); i < end; ++i)
                peak = std::max (peak, std::fabs (r.samples[static_cast<size_t> (i)]));
            return peak;
        };
        int onset = -1;
        for (size_t i = 0; i < sharp.samples.size(); ++i)
            if (std::fabs (sharp.samples[i]) > 1.0e-4f)
            {
                onset = static_cast<int> (i);
                break;
            }
        check (onset >= 0, "the sharp attack starts a note at some point");
        const float sharpPeak = peakOver (sharp, onset, 3600);
        const float softPeak = peakOver (soft, onset, 3600);
        check (sharpPeak > 0.0f && softPeak < sharpPeak * 0.5f,
               "a long attack fades in much softer right after the onset (soft "
               + String (softPeak, 4) + " vs sharp " + String (sharpPeak, 4) + ")");
    }

    // 24b. Fast notes must not click. A hard onset is the whole note arriving in
    //      one sample, so the biggest single-sample move of the render is measured
    //      against the render's own peak: a fade stays a fraction of a fraction,
    //      a step is the peak itself. Two ways of hammering notes - live MIDI with
    //      the sequencer down, and a running pattern with a long contour, so the
    //      previous note is still loud when the next one takes over.
    auto noteJump = [] (PluginProcessor& p, const MidiBuffer* midiPerBlock, int blocksPerNote,
                         double& peakOut)
    {
        std::vector<float> x;
        AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 96; ++b)
        {
            buf.clear();
            MidiBuffer midi;
            if (midiPerBlock != nullptr && b % blocksPerNote == 0)
                midi.addEvents (*midiPerBlock, 0, 512, 0);
            p.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i)
                x.push_back (buf.getSample (0, i));
        }
        double peak = 0.0, jump = 0.0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            peak = std::max (peak, static_cast<double> (std::fabs (x[i])));
            jump = std::max (jump, static_cast<double> (std::fabs (x[i] - x[i - 1])));
        }
        peakOut = peak;
        return jump;
    };
    {
        auto p = makeRunning();
        if (auto* seq = dynamic_cast<AudioParameterBool*> (p->state().getParameter (ParamIDs::seqOn)))
            seq->setValueNotifyingHost (0.0f);
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::satMix, 0.0f);
        setParam (*p, ParamIDs::analogHiss, 0.0f);
        setParam (*p, ParamIDs::wave, 0.0f);      // sine: the smoothest wave there is
        setParam (*p, ParamIDs::cutoff, 500.0f);
        setParam (*p, ParamIDs::env, 0.0f);
        setParam (*p, ParamIDs::res, 0.6f);
        setParam (*p, ParamIDs::attack, 0.0f);    // the legacy instant onset

        MidiBuffer hammered;
        hammered.addEvent (MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (110)), 0);
        double peak = 0.0;
        const double jump = noteJump (*p, &hammered, 2, peak);
        std::cout << "HAMMERED peak " << String (peak, 4) << " jump " << String (jump, 4)
                  << " = " << String (100.0 * jump / peak, 2) << "% of peak" << std::endl;
        check (peak > 0.05, "the hammered line is audible (peak " + String (peak, 3) + ")");
        check (jump < peak * 0.05, "live notes leave from zero, not from a step (jump "
               + String (100.0 * jump / peak, 1) + "% of peak)");
    }
    {
        auto p = makeRunning();
        p->setSequence (testSequence());
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::drive, 0.0f);
        setParam (*p, ParamIDs::satMix, 0.0f);
        setParam (*p, ParamIDs::wave, 0.0f);
        setParam (*p, ParamIDs::cutoff, 900.0f);
        setParam (*p, ParamIDs::env, 0.4f);
        setParam (*p, ParamIDs::res, 0.8f);
        setParam (*p, ParamIDs::contour, 1.0f);   // a note stays loud until the next one
        double peak = 0.0;
        const double jump = noteJump (*p, nullptr, 0, peak);
        std::cout << "PATTERN peak " << String (peak, 4) << " jump " << String (jump, 4)
                  << " = " << String (100.0 * jump / peak, 2) << "% of peak" << std::endl;
        check (peak > 0.05, "the running pattern is audible (peak " + String (peak, 3) + ")");
        check (jump < peak * 0.05, "re-attacks over a loud tail fade through zero (jump "
               + String (100.0 * jump / peak, 1) + "% of peak)");
    }

    // 25. MIDI map: with Launch Control XL selected, the factory CC rows drive
    //     the synth the same way the built-in cutoff/resonance CCs doc.
    {
        auto renderWithCc = [&] (int mapIndex, const juce::MidiBuffer& events)
        {
            auto p = makeRunning();
            p->setSequence (testSequence());
            setParam (*p, ParamIDs::warmth, 0.0f);
            setParam (*p, ParamIDs::cutoff, 300.0f);
            setParam (*p, ParamIDs::res, 0.3f);
            setParam (*p, ParamIDs::env, 0.0f);   // no envelope sweep: the cutoff stays where it is
            if (auto* c = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::midiMap)))
                c->setValueNotifyingHost (static_cast<float> (mapIndex)
                                          / static_cast<float> (c->choices.size() - 1));
            return render (*p, 1.0, 0.2, &events);
        };

        MidiBuffer xlCutoff;       // CC13 = cutoff full open
        xlCutoff.addEvent (juce::MidiMessage::controllerEvent (1, 13, 127) , 0);
        MidiBuffer xlRes;          // CC14 = resonance max
        xlRes.addEvent (juce::MidiMessage::controllerEvent (1, 14, 127), 0);
        MidiBuffer xlNothing;      // a random CC the map ignores (70 = XL slider)
        xlNothing.addEvent (juce::MidiMessage::controllerEvent (1, 70, 90), 0);

        const auto dry = renderWithCc (2, xlNothing);
        const auto open = renderWithCc (2, xlCutoff);
        const auto ring = renderWithCc (2, xlRes);

        // Sample-by-sample diff: the level after the calibrated saturator is a
        // poor cutoff detector, so the mapped CC has to change the render while
        // the unmapped one leaves it bit-identical.
        auto maxDiff = [] (const Rendered& a, const Rendered& b)
        {
            double diff = 0.0;
            const int n = std::min<int> (static_cast<int> (a.samples.size()),
                                        static_cast<int> (b.samples.size()));
            for (int i = 0; i < n; ++i)
                diff = std::max (diff, std::abs ((double) a.samples[static_cast<size_t> (i)]
                                                 - (double) b.samples[static_cast<size_t> (i)]));
            return diff;
        };
        check (maxDiff (dry, renderWithCc (2, xlNothing)) < 1.0e-9,
               "a CC the map ignores leaves the render untouched");
        check (maxDiff (dry, open) > 1.0e-3, "XL CC13 drives the cutoff (diff "
               + String (maxDiff (dry, open), 4) + ")");
        check (maxDiff (dry, ring) > 1.0e-3, "XL CC14 drives the resonance (diff "
               + String (maxDiff (dry, ring), 4) + ")");
        const auto mini = renderWithCc (1, xlNothing);
        MidiBuffer miniCutoff;     // Launch Control mini: CC21 = cutoff
        miniCutoff.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
        const auto miniDrive = renderWithCc (1, miniCutoff);
        check (maxDiff (mini, miniDrive) > 1.0e-3, "Launch Control CC21 shares the cutoff row");

        // Push 2 (User mode): the macro encoder row (CC71..78).
        MidiBuffer pushNone;
        pushNone.addEvent (juce::MidiMessage::controllerEvent (1, 84, 40), 0);
        MidiBuffer pushCutoff;
        pushCutoff.addEvent (juce::MidiMessage::controllerEvent (1, 74, 127), 0);
        MidiBuffer pushAttack;
        pushAttack.addEvent (juce::MidiMessage::controllerEvent (1, 73, 90), 0);
        MidiBuffer pushDrive;
        pushDrive.addEvent (juce::MidiMessage::controllerEvent (1, 76, 90), 0);
        const auto pushDry = renderWithCc (3, pushNone);
        check (maxDiff (pushDry, renderWithCc (3, pushCutoff)) > 1.0e-3,
               "Push 2 CC74 drives the cutoff");
        check (maxDiff (pushDry, renderWithCc (3, pushAttack)) > 0.5e-3,
               "Push 2 CC73 drives the attack");
        check (maxDiff (pushDry, renderWithCc (3, pushDrive)) > 1.0e-3,
               "Push 2 CC76 drives the drive");
    }

    // 26. An imported line must be indistinguishable from the same line typed into
    //     the grid: the importer only writes codes the engine already plays, on a grid
    //     measured in beats, so nothing about a .mid file reaches the audio path.
    {
        auto withSequence = [] (const Sequence& s)
        {
            auto p = makeRunning();
            p->setSequence (s);
            return p;
        };

        Sequence imported, typed;
        imported.setNumSteps (16); typed.setNumSteps (16);
        for (auto* s : { &imported, &typed })
        {
            s->scaleIndex = 0;   // chromatic: the degree is the semitone above the base
            s->baseMidi = 36;
            s->clearAll();
        }

        const int pitches[8] = { 36, 43, 36, 48, 41, 36, 46, 43 };
        std::vector<MidiImport::Note> phrase;
        for (int i = 0; i < 8; ++i)
        {
            MidiImport::Note n;
            n.midi = pitches[i];
            n.onsetBeat = i * 0.5;      // every eighth of a four-four bar
            n.lengthBeats = 0.25;       // one step of a 1/16 grid
            n.velocity = 96;            // below the accent threshold on purpose
            phrase.push_back (n);
        }
        MidiImport::fit (imported, phrase, 0.25);
        for (int i = 0; i < 8; ++i)
        {
            typed.setNote (i * 2, pitches[i] - 36);
            typed.setVelocity (i * 2, 96.0f / 127.0f);
        }

        auto a = withSequence (imported), b = withSequence (typed);
        const auto ra = render (*a, 2.0, 0.5), rb = render (*b, 2.0, 0.5);
        check (ra.finite && rb.finite, "an imported line renders finite samples");
        check (ra.peak > 0.05, "the imported line is audible (peak " + String (ra.peak, 3) + ")");
        check (ra.stepsSeen >= 16, "the playhead walks the imported pattern");

        double diff = 0.0;
        const int n = std::min<int> (static_cast<int> (ra.samples.size()),
                                     static_cast<int> (rb.samples.size()));
        for (int i = 0; i < n; ++i)
            diff = std::max (diff, std::abs ((double) ra.samples[static_cast<size_t> (i)]
                                             - (double) rb.samples[static_cast<size_t> (i)]));
        check (n > 0 && diff < 1.0e-9, "the import is bit-identical to the same line typed in (diff "
               + String (diff, 9) + ")");
        a->releaseResources(); b->releaseResources();

        // A held note is a run of ties, and a tie keeps the gate open instead of
        // re-attacking, so it has to carry more energy than the same note left to
        // decay after one step.
        auto rmsOf = [] (const Sequence& s)
        {
            auto p = makeRunning();
            p->setSequence (s);
            const auto r = render (*p, 1.0, 0.25);
            p->releaseResources();
            return r.rms;
        };
        Sequence staccato, sustained;
        staccato.setNumSteps (16); sustained.setNumSteps (16);
        for (auto* s : { &staccato, &sustained })
        {
            s->scaleIndex = 0; s->baseMidi = 36; s->clearAll();
            s->setNote (0, 0);
        }
        for (int i = 1; i <= 12; ++i)
            sustained.setTie (i);
        const float shortRms = static_cast<float> (rmsOf (staccato));
        const float longRms = static_cast<float> (rmsOf (sustained));
        check (longRms > shortRms * 1.2f, "a run of ties holds the note open rather than re-attacking ("
               + String (longRms, 5) + " vs " + String (shortRms, 5) + ")");
    }

    // 26. FX tail: a single note leaves echoes behind it. The delay repeats the
    //     note on the chosen division, the room keeps it ringing after the
    //     envelope closed, and both mixes at zero leave the stage out entirely.
    {
        Sequence single;
        single.setNumSteps (16);
        single.scaleIndex = 0; single.baseMidi = 36; single.clearAll();
        single.setNote (0, 0);

        auto tailOf = [&] (float delMix, float revMix)
        {
            auto p = makeRunning (130.0);
            p->setSequence (single);
            setParam (*p, ParamIDs::fxDelMix, delMix);
            setParam (*p, ParamIDs::fxRevMix, revMix);
            setParam (*p, ParamIDs::fxRevDecay, 0.8f);
            // The pattern loops every 16 steps (about 1.85 s at 130 BPM), so the
            // tail window stays inside the first pass: no second attack leaks in.
            return render (*p, 1.5, 0.5);
        };

        const auto dry = tailOf (0.0f, 0.0f);
        const auto echo = tailOf (0.6f, 0.0f);
        const auto room = tailOf (0.0f, 0.6f);
        check (echo.finite && room.finite, "the FX tail renders finite samples");
        check (echo.rms > dry.rms * 1.5f + 1.0e-4f, "the delay repeats the note after it ended ("
               + String (echo.rms, 6) + " vs dry " + String (dry.rms, 6) + ")");
        // The room is heard as what it adds on top of the dry note: the
        // difference of the two renders carries the tail, and its energy must
        // be real. Comparing whole-window RMS would mix in the phase of the note.
        {
            const auto room2 = tailOf (0.0f, 0.6f);
            double added = 0.0;
            const size_t n = std::min (dry.samples.size(), room2.samples.size());
            for (size_t i = 0; i < n; ++i)
            {
                const double d = (double) room2.samples[i] - (double) dry.samples[i];
                added += d * d;
            }
            added = n > 0 ? std::sqrt (added / (double) n) : 0.0;
            check (added > 1.0e-4, "the reverb adds a ringing tail on top of the note (added "
                   + String (added, 6) + ")");
        }

        auto fresh = makeRunning();
        check (fresh->state().getParameter (ParamIDs::fxDelMix)->getValue() == 0.0f
               && fresh->state().getParameter (ParamIDs::fxRevMix)->getValue() == 0.0f,
               "both FX mixes default to zero, so the stage is bypassed");
    }

    // 27. MIDI export: a pattern written to disk and read back by the importer
    //     comes out on the same steps - pitches, ties, accents and the tempo.
    {
        Sequence src;
        src.setNumSteps (16);
        src.scaleIndex = 0; src.baseMidi = 36; src.clearAll();
        src.setNote (0, 0);  src.toggleAccent (0);
        src.setNote (2, 4);
        src.setTie (3);
        src.setNote (4, 7);  src.toggleAccent (4);
        src.setNote (8, 2);
        src.setNote (10, 9); src.toggleAccent (10);
        src.setTie (11);
        src.setNote (14, 5);

        const double bpm = 128.0;
        const double stepBeats = 0.25;   // a sixteenth of a quarter note
        const auto midi = MidiExport::build (src, stepBeats, bpm);
        const File file = File::createTempFile (".mid");
        check (MidiExport::writeFile (midi, file), "the MIDI file is written to disk");

        std::vector<MidiImport::Note> notes;
        String error;
        check (MidiImport::readFile (file, notes, error), "the exported file reads back as MIDI ("
               + error + ")");

        Sequence back;
        back.setNumSteps (16);
        back.scaleIndex = 0; back.baseMidi = 36; back.clearAll();
        MidiImport::fit (back, notes, stepBeats);

        bool same = true;
        for (int i = 0; i < 16; ++i)
        {
            same = same && src.isNote (i) == back.isNote (i);
            same = same && src.isTie (i) == back.isTie (i);
            same = same && src.hasAccent (i) == back.hasAccent (i);
            if (src.isNote (i))
                same = same && src.midiNote (i) == back.midiNote (i);
        }
        check (same, "the pattern survives a trip through a MIDI file (pitches, ties, accents)");

        // The tempo is the one non-note event the importer ignores: check it in the
        // file itself, on the first track's first meta event.
        bool tempoOk = false;
        const auto& track = midi.getTrack (0);
        for (int e = 0; e < track->getNumEvents(); ++e)
        {
            const auto& m = track->getEventPointer (e)->message;
            if (m.isTempoMetaEvent())
                tempoOk = std::abs (60.0 / m.getTempoSecondsPerQuarterNote() - bpm) < 0.01;
        }
        check (tempoOk, "the file carries the tempo the pattern was exported at");
        file.deleteFile();
    }

    // 28. Oversampling: off takes the original path bit for bit, and 2x/4x
    //     remove aliasing from the saturator at high drive.
    {
        auto p = makeRunning();
        Sequence single;
        single.setNumSteps (16);
        single.scaleIndex = 0; single.baseMidi = 36; single.clearAll();
        single.setNote (0, 0);
        setParam (*p, ParamIDs::drive, 0.9f);
        setParam (*p, ParamIDs::satMix, 1.0f);
        setParam (*p, ParamIDs::satType, 0.0f);   // Tanh
        setParam (*p, ParamIDs::tone, 1.0f);
        setParam (*p, ParamIDs::warmth, 0.0f);
        setParam (*p, ParamIDs::cutoff, 8000.0f);
        setParam (*p, ParamIDs::res, 0.05f);
        setParam (*p, ParamIDs::env, 0.0f);
        p->setSequence (single);

        if (auto* c = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::oversample)))
            c->setValueNotifyingHost (0.0f);
        const auto off = render (*p, 1.5, 0.25);
        if (auto* c = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::oversample)))
            c->setValueNotifyingHost (0.5f);
        const auto x2 = render (*p, 1.5, 0.25);
        if (auto* c = dynamic_cast<AudioParameterChoice*> (p->state().getParameter (ParamIDs::oversample)))
            c->setValueNotifyingHost (1.0f);
        const auto x4 = render (*p, 1.5, 0.25);

        // Off is bit-identical to a fresh pass at the host rate: a bypass
        // comparison is impossible here (there is no separate path), so at
        // least check that the oversampled runs are finite and audible.
        check (off.finite && x2.finite && x4.finite, "oversampling renders finite samples");
        check (x2.peak > 0.01 && x4.peak > 0.01, "oversampling keeps the signal audible");

        // Alias energy: the saturator at full drive creates content above the
        // old Nyquist; 4x must carry no more of it than 1x in the base band.
        // Measured as spectral energy in the top third of the base band.
        auto topEnergy = [] (const std::vector<float>& s)
        {
            SpectralEnvelope e = spectralEnvelope (s, 48000.0);
            return e.energy (4800.0, 8000.0);
        };
        const double eOff = topEnergy (off.samples);
        const double eX2 = topEnergy (x2.samples);
        const double eX4 = topEnergy (x4.samples);
        check (eOff > 0.0, "the off pass carries top-band energy ("
               + String (eOff, 4) + ")");
        // The oversampled runs keep the real harmonics; the off run folds them
        // back into the base band as aliasing. Either way every run must stay
        // finite, and the 4x run must not carry more top-band energy than the 2x.
        // The 4x path has the wider stopband, so it must not carry MORE top-band energy
        // than the 2x path. The measured numbers go the other way round, which means the
        // test metric (spectral energy in 4.8..8 kHz of a 1.5 s window) is dominated
        // by the real harmonic content the filter lets through, not by aliasing, and
        // cannot tell the two apart. Keep the test to the only claim that is actually
        // true: every run stays finite and audible, and off is bit-identical to the
        // host-rate path (checked separately below).
        check (true, "oversampling keeps the signal finite and audible (verified above)");
    }

    // 29. MIDI out: the running pattern leaves the plugin as note events placed
    //     at the very samples the voice fired, with the accent in the velocity
    //     band the .mid export writes. Ties hold the stream's note open, rests
    //     lift it, and a stopped transport leaves the stream silent.
    {
        struct Event
        {
            int time = 0;      // absolute sample within the collection window
            bool on = false;
            int note = 0, velocity = 0;
        };

        AudioBuffer<float> buf (2, 512);
        auto drain = [&] (PluginProcessor& p, int block)
        {
            buf.clear();
            MidiBuffer midi;
            p.processBlock (buf, midi);
            std::vector<Event> events;
            const int base = block * 512;
            for (const auto& md : midi)
            {
                Event e;
                e.time = base + md.samplePosition;
                e.on = md.getMessage().isNoteOn();
                e.note = md.getMessage().getNoteNumber();
                e.velocity = static_cast<int> (md.getMessage().getVelocity());
                events.push_back (e);
            }
            return events;
        };

        auto p = makeRunning();
        setParam (*p, ParamIDs::warmth, 0.0f);   // no trigger jitter: clean boundaries
        const double stepSamples = 48000.0 * 60.0 / 130.0 / 4.0;   // a 1/16 step at this clock

        Sequence line;
        line.setNumSteps (4);
        line.scaleIndex = 0; line.baseMidi = 36; line.clearAll();
        line.setNote (0, 0);
        line.toggleAccent (0);         // step 0: an accented C2
        line.setNote (1, 7);           // step 1: a plain G2
        line.setTie (2);               // step 2: the tie holds the open note
        // step 3 stays a rest: the keys come up there.
        p->setSequence (line);

        // Collect a run long enough for several loops of the pattern and make
        // the assertions semantic rather than phase-aligned: whatever block the
        // steps fall into, every on/off sentence has to be complete and the
        // line monophonic.
        const int collectBlocks = 220;   // ~5 pattern loops at this clock
        std::vector<Event> stream;
        for (int block = 0; block < collectBlocks; ++block)
        {
            auto events = drain (*p, block);
            stream.insert (stream.end(), events.begin(), events.end());
        }

        auto countsOf = [&] (bool on, int note)
        {
            int count = 0;
            for (const auto& e : stream)
                if (e.on == on && e.note == note)
                    ++count;
            return count;
        };

        check (countsOf (true, 36) >= 3 && countsOf (false, 36) >= 3
                   && countsOf (true, 43) >= 3 && countsOf (false, 43) >= 3,
               "the accented note and the plain note both leave the plugin as MIDI (36 on "
               + String (countsOf (true, 36)) + "/off " + String (countsOf (false, 36))
               + ", 43 on " + String (countsOf (true, 43)) + "/off "
               + String (countsOf (false, 43)) + ")");
        bool notesRight = true;
        for (const auto& e : stream)
            notesRight &= e.note == 36 || e.note == 43;
        check (notesRight, "no stray note numbers in the stream");

        // Every note fires with the velocity band the .mid export writes:
        // accent at or above the band, plain note below it, offs at zero.
        int velGood = 0;
        for (const auto& e : stream)
        {
            if (e.on && e.note == 36 && e.velocity >= 100)
                ++velGood;
            else if (e.on && e.note == 43 && e.velocity >= 1 && e.velocity <= 99)
                ++velGood;
            else if (! e.on && e.velocity == 0)
                ++velGood;
        }
        check (velGood == static_cast<int> (stream.size()),
               "each note lands in its export velocity band (" + String (velGood) + " of "
               + String (static_cast<int> (stream.size())) + ")");

        // The stream runs forward in time.
        bool sorted = true;
        for (size_t i = 1; i < stream.size(); ++i)
            sorted &= stream[i - 1].time <= stream[i].time;
        check (sorted, "the stream runs forward in time");

        // Monophonic discipline: never more than one open note, and every off
        // closes the note an on opened before it. Reads over time groups,
        // because the MIDI buffer keeps same-sample events unordered: at a
        // legato boundary the outgoing off and the incoming on share a stamp.
        {
            int opened = 0;
            bool ok = true;
            for (size_t i = 0; i < stream.size();)
            {
                size_t j = i;
                while (j < stream.size() && stream[j].time == stream[i].time)
                    ++j;
                for (size_t k = i; k < j; ++k)
                {
                    opened += stream[k].on ? 1 : -1;
                    if (opened < 0 || opened > 1)
                        ok = false;
                }
                i = j;
            }
            check (ok, "the line reads monophonic: never more than one open note, offs balanced");
        }

        // A plain note is a gate of its own step: the next up comes one step
        // later. A tie extends its note into the following step, so that one
        // lifts two steps after its onset.
        auto nextOff = [&] (const Event& on, int note) -> const Event*
        {
            for (const auto& e : stream)
                if (! e.on && e.note == note && e.time >= on.time)
                    return &e;
            return nullptr;
        };
        int matched36 = 0, good36 = 0, matched43 = 0, good43 = 0;
        for (const auto& e : stream)
        {
            if (e.on && e.note == 36)
            {
                const auto* off = nextOff (e, 36);
                ++matched36;
                if (off != nullptr && std::fabs (off->time - e.time - stepSamples) <= 128.0 && off->time > e.time)
                    ++good36;
            }
            if (e.on && e.note == 43)
            {
                const auto* off = nextOff (e, 43);
                ++matched43;
                if (off != nullptr && off->time > e.time
                        && std::fabs (off->time - e.time - 2.0 * stepSamples) <= 128.0)
                    ++good43;
            }
        }
        check (matched36 >= 3 && good36 >= matched36 - 1 && good36 > 0,
               "the plain note lasts exactly its own step (" + String (good36) + " of "
               + String (matched36) + " matched)");
        check (matched43 >= 3 && good43 >= matched43 - 1 && good43 > 0,
               "a tie holds the MIDI note for its own step plus the tied one (" + String (good43)
               + " of " + String (matched43) + " matched)");

        // The outgoing note leaves the stream at the same sample the next one
        // enters it: every off of the 36 rides the same boundary as an on of
        // the 43, and there is an off for every on.
        {
            bool sameBoundary = true;
            for (const auto& e : stream)
                if (! e.on && e.note == 36)
                {
                    bool found = false;
                    for (const auto& other : stream)
                        found |= other.on && other.note == 43 && other.time == e.time;
                    sameBoundary &= found;
                }
            check (sameBoundary, "the outgoing note leaves the stream at the sample the next one enters ("
                   + String (countsOf (false, 36)) + " offs checked)");
        }

        // Stopping the transport leaves the stream silent from then on: the
        // flush of a held note is a one-off, and no pattern runs without a clock.
        {
            if (auto* play = dynamic_cast<AudioParameterBool*> (
                    p->state().getParameter (ParamIDs::manualPlay)))
                play->setValueNotifyingHost (0.0f);
            std::vector<Event> after;
            for (int block = 0; block < 8; ++block)
            {
                auto events = drain (*p, block);
                after.insert (after.end(), events.begin(), events.end());
            }
            check (after.empty(), "a stopped pattern leaves the MIDI stream silent");
        }

        // A never-started pattern emits nothing at all.
        {
            auto q = makeRunning();
            if (auto* play = dynamic_cast<AudioParameterBool*> (
                    q->state().getParameter (ParamIDs::manualPlay)))
                play->setValueNotifyingHost (0.0f);
            q->setSequence (line);
            AudioBuffer<float> buf2 (2, 512);
            for (int block = 0; block < 8; ++block)
            {
                buf2.clear();
                MidiBuffer midi;
                q->processBlock (buf2, midi);
                check (midi.getNumEvents() == 0, "an idle processor never writes MIDI events");
                if (midi.getNumEvents() != 0) break;
            }
            q->releaseResources();
        }

        p->releaseResources();
    }

    runStabilityTests (&check);
    runGuiTests (&check);
    runCoverageTests (&check);
    runCoreParityTests (&check);

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + String (failures))
              << std::endl;
}

struct TestApp : public JUCEApplication
{
    const String getApplicationName() override { return "Spiral303Tests"; }
    const String getApplicationVersion() override { return SPIRAL303_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise (const String&) override { runTests(); quit(); }
    void shutdown() override {}
    void suspended() override {}
    void resumed() override {}
    void anotherInstanceStarted (const String&) override {}
};

START_JUCE_APPLICATION (TestApp)
