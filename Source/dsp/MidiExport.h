#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

#include "MidiImport.h"
#include "Sequence.h"

/** Writes the pattern as a one-loop standard MIDI file: the mirror of MidiImport.
    Every step grid position becomes a tick position, a tie extends the note it
    follows, and the tempo rides along as a meta event so a DAW opens at the
    sequencer's tempo.

    The accent flag travels in velocity: accented notes sit at or above
    MidiImport::kAccentVelocity, every other note stays below it. Without that
    split a plain note at the default full level would read back as an accent.

    Slides are written as legato (a note runs into the next one with no gap), which
    is what a glide sounds like in MIDI; the importer reads that back as a glide
    only where the pitch changes. The gate is a volume effect and is not exported.

    Like the importer this only needs the step length in quarter-note beats, so the
    grid is the same on both sides for any tempo or clock division. */
namespace MidiExport
{
    inline constexpr int kTicksPerQuarter = 480;
    inline constexpr int kChannel = 1;

    inline juce::MidiFile build (const Sequence& seq, double stepBeats, double bpm)
    {
        juce::MidiMessageSequence track;
        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (
            static_cast<int> (std::llround (60000000.0 / safeBpm))), 0.0);

        const int n = seq.numSteps();
        for (int i = 0; i < n; ++i)
        {
            if (! seq.isNote (i))
                continue;

            // A note lasts until the next attack: its own step plus every tie after it.
            int length = 1;
            while (i + length < n && seq.isTie (i + length))
                ++length;

            const auto onsetTick = std::llround (i * stepBeats * kTicksPerQuarter);
            const auto endTick = std::llround ((i + length) * stepBeats * kTicksPerQuarter);

            const int level = static_cast<int> (std::llround (seq.velocityOf (i) * 127.0f));
            int velocity = seq.hasAccent (i)
                ? std::max (MidiImport::kAccentVelocity, level)
                : std::min (MidiImport::kAccentVelocity - 1, level);
            velocity = std::clamp (velocity, 1, 127);

            const int note = seq.midiNote (i);
            track.addEvent (juce::MidiMessage::noteOn (kChannel, note, static_cast<juce::uint8> (velocity)),
                            static_cast<double> (onsetTick));
            track.addEvent (juce::MidiMessage::noteOff (kChannel, note),
                            static_cast<double> (endTick));
        }

        track.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);
        file.addTrack (track);
        return file;
    }

    /** Writes the file, saying nothing on success; false when the disk refuses. */
    inline bool writeFile (const juce::MidiFile& midi, const juce::File& file)
    {
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return false;
        out.setPosition (0);
        out.truncate();
        if (! midi.writeTo (out))
            return false;
        out.flush();
        return out.getStatus().wasOk();
    }
}
