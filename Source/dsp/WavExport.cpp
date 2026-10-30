#include "WavExport.h"

#include <algorithm>
#include <vector>

using namespace juce;

bool WavExport::renderPattern (PluginProcessor& source, const File& file, int numBars)
{
    MemoryBlock state;
    source.getStateInformation (state);

    // A fresh instance has no play head, so the sequencer has to be put into the
    // manual mode by hand; the state itself carries the tempo and the pattern.
    PluginProcessor target;
    target.setRateAndBufferSizeDetails (sampleRate, blockSamples);
    target.prepareToPlay (sampleRate, blockSamples);
    target.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    target.setTempoBpm (source.getTempoBpm());

    if (auto* sync = dynamic_cast<AudioParameterChoice*> (
            target.state().getParameter (ParamIDs::syncMode)))
        // Manual, whatever the choice's current size is.
        sync->setValueNotifyingHost (1.0f / static_cast<float> (sync->getAllValueStrings().size() - 1));
    if (auto* play = dynamic_cast<AudioParameterBool*> (
            target.state().getParameter (ParamIDs::manualPlay)))
        play->setValueNotifyingHost (1.0f);

    const double seconds = numBars * source.getSequence().numSteps() * source.getStepLengthMs() * 0.001;
    const int wantedSamples = static_cast<int> (jmin (maxSeconds * sampleRate, std::max (1.0, seconds * sampleRate)));

    std::vector<float> rendered (static_cast<size_t> (wantedSamples), 0.0f);
    AudioBuffer<float> mix (jmax (1, target.getTotalNumOutputChannels()), blockSamples);

    int produced = 0;
    while (produced < wantedSamples)
    {
        mix.clear();
        MidiBuffer midi;
        target.processBlock (mix, midi);

        const float* src = mix.getReadPointer (0);
        const int n = jmin (blockSamples, wantedSamples - produced);
        std::copy (src, src + n, rendered.begin() + produced);
        produced += n;
    }

    // A sweep below the audible range can leave a near-DC offset in the tail; a short
    // fade keeps that from clicking when the file ends.
    const int fade = jmin (produced, static_cast<int> (sampleRate * 0.005));
    for (int i = 0; i < fade; ++i)
        rendered[static_cast<size_t> (produced - 1 - i)] *= static_cast<float> (i) / fade;

    WavAudioFormat format;
    auto writer = std::unique_ptr<AudioFormatWriter> (format.createWriterFor (
        new FileOutputStream (file), sampleRate, 1, 24, {}, 0));
    if (writer == nullptr)
        return false;

    AudioBuffer<float> chunk (1, blockSamples);
    for (int offset = 0; offset < produced; offset += blockSamples)
    {
        const int n = jmin (blockSamples, produced - offset);
        chunk.copyFrom (0, 0, rendered.data() + offset, n);
        if (! writer->writeFromAudioSampleBuffer (chunk, 0, n))
            return false;
        if (n < blockSamples)
            break;
    }

    writer->flush();
    target.releaseResources();
    return true;
}
