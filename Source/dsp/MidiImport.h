#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Sequence.h"

/** Turns a standard MIDI file into a Spiral303 pattern.
    Split in two: collectNotes() reads the file into pitches and durations measured in
    quarter-note beats, fit() lays those notes over the step grid. Keeping the file out
    of the grid maths is what makes both halves testable without a disk or a window.

    The file's own tempo map is deliberately never consulted. The sequencer already runs
    on the host's transport or the BPM knob, so a note position expressed in beats lands
    on the right step whatever the tempo is, and the line keeps its rhythm when the clock
    moves. Tempo meta events are therefore noise here, not data. */
namespace MidiImport
{
    /** One note of one track. Both times are quarter-note beats from the start of
        the track, so nothing in here knows about seconds, ticks or the file's tempo. */
    struct Note
    {
        int midi = 60;
        double onsetBeat = 0.0;
        double lengthBeats = 0.0;
        int velocity = 100;
    };

    /** JUCE numbers channels 1..16, so the GM drum lane is 10. Its numbers name kit
        pieces, not pitches, and a low kick would win the lowest-average contest from
        the bass line it is only accompanying. */
    inline constexpr int kDrumChannel = 10;

    /** The velocity that also earns the accent flag: the top quarter of the range,
        which is where a 303 line is meant to bite. */
    inline constexpr int kAccentVelocity = 100;

    /** The NOTE combo shows C1..C5; the base of an imported phrase is kept inside it
        so the control and the model never disagree about the key. */
    inline constexpr int kBaseNoteFirst = 24, kBaseNoteLast = 72;

    /** Notes of one track. Durations come from JUCE's own matched-pair scan:
        MidiFile::readFrom runs updateMatchedPairs on every track, which pairs each
        note-on with its key-up and counts a zero-velocity note-on as a key-up. A note
        the file never releases at all is left unpaired there, so it is held to the end
        of its own track instead of vanishing with the rest of the line. The pairing is
        read through getIndexOfMatchingKeyUp, because getTimeOfMatchingKeyUp answers 0 -
        not "none" - for an unpaired event, which a note starting at tick 0 would read
        as an instant release. */
    inline void collectTrack (const juce::MidiMessageSequence& track, double ticksPerBeat,
                              std::vector<Note>& out)
    {
        const double end = track.getEndTime();

        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (! m.isNoteOn() || m.getChannel() == kDrumChannel)
                continue;

            const double onset = track.getEventTime (i);
            const double keyUp = track.getIndexOfMatchingKeyUp (i) >= 0
                ? track.getTimeOfMatchingKeyUp (i) : end;
            Note n;
            n.midi = m.getNoteNumber();
            n.onsetBeat = onset / ticksPerBeat;
            n.lengthBeats = juce::jmax (0.0, (keyUp - onset) / ticksPerBeat);
            n.velocity = m.getVelocity();
            out.push_back (n);
        }
    }

    /** The notes of the track that sits lowest - the bass line, which is what a 303
        wants. Empty when the file has no bar grid or no pitched notes at all. */
    inline std::vector<Note> collectNotes (const juce::MidiFile& file)
    {
        std::vector<Note> best;
        double bestAverage = 1.0e9;
        const double ticksPerBeat = file.getTimeFormat();
        if (ticksPerBeat <= 0.0)
            return best;

        for (int t = 0; t < file.getNumTracks(); ++t)
        {
            const auto* track = file.getTrack (t);
            if (track == nullptr)
                continue;

            std::vector<Note> notes;
            collectTrack (*track, ticksPerBeat, notes);
            if (notes.empty())
                continue;

            double sum = 0.0;
            for (const auto& n : notes)
                sum += static_cast<double> (n.midi);
            const double average = sum / static_cast<double> (notes.size());

            // Lowest tessitura wins; an exact tie goes to the busier track, which is
            // the one carrying the line rather than a few doubled notes.
            if (average < bestAverage || (average == bestAverage && notes.size() > best.size()))
            {
                bestAverage = average;
                best = std::move (notes);
            }
        }
        return best;
    }

    /** The nearest degree of the sequence's own scale, ties falling downwards. Notes
        below the base land on degree 0 and notes above the last one on the last
        degree, so the phrase can only ever fold inside the rows the editor shows. */
    inline int nearestDegree (const std::vector<int>& offsets, int semitones)
    {
        const int last = static_cast<int> (offsets.size()) - 1;
        if (semitones <= offsets[0])
            return 0;
        if (semitones >= offsets[last])
            return last;

        int best = 0, bestGap = std::abs (offsets[0] - semitones);
        for (int k = 1; k <= last; ++k)
        {
            const int gap = std::abs (offsets[k] - semitones);
            if (gap < bestGap)
                { best = k; bestGap = gap; }
        }
        return best;
    }

    /** Lay the phrase over the grid: stepBeats is how long one sequencer step is in
        quarter notes, so 0.25 is the plain sixteenth and the clock divider makes it
        larger or smaller. The first attack becomes step 0 - a clip exported from the
        second bar must not spend half the pattern on a count-in. Notes past the last
        step are cut, and steps past the end of the phrase stay rests. */
    inline void fit (Sequence& seq, std::vector<Note> notes, double stepBeats)
    {
        const int n = seq.numSteps();
        if (n <= 0 || stepBeats <= 0.0 || notes.empty())
            return;

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                   { return a.onsetBeat != b.onsetBeat ? a.onsetBeat < b.onsetBeat
                                                       : a.midi < b.midi; });

        const auto& offsets = Sequence::scaleOffsets (seq.scaleIndex);
        int lowest = notes.front().midi;
        for (const auto& nt : notes)
            lowest = juce::jmin (lowest, nt.midi);

        // The key stays where the user left it. The base only ever moves down, to
        // make room for a phrase that dips below it - or up in the one case where
        // the whole phrase sits above the reach of the current base, because an
        // import that collapses onto the top row is not an import.
        int base = seq.baseMidi;
        if (lowest < base || lowest > base + offsets.back())
            base = lowest;
        seq.baseMidi = juce::jlimit (kBaseNoteFirst, kBaseNoteLast, base);

        const double origin = notes.front().onsetBeat;
        int heldTo = -1, heldDegree = -1;   // the sustain run of the last attack
        int attackedAt = -1;

        for (const auto& nt : notes)
        {
            const long onset = std::llround ((nt.onsetBeat - origin) / stepBeats);
            if (onset >= n)
                break;   // sorted, so nothing after this can fit either

            const int s0 = static_cast<int> (onset);
            if (s0 <= attackedAt)
                continue;   // one voice per step: of a chord the lowest note is kept
            const int length = static_cast<int> (std::llround (nt.lengthBeats / stepBeats));
            const int s1 = juce::jmin (n - 1, s0 + juce::jmax (1, length) - 1);
            const int degree = nearestDegree (offsets, nt.midi - seq.baseMidi);
            const bool legato = heldTo >= 0 && s0 <= heldTo + 1;

            if (legato && degree == heldDegree)
            {
                // One pitch held over is one note: the 303 cannot re-articulate it,
                // so the run grows by ties instead of attacking a second time.
                for (int k = heldTo + 1; k <= s1; ++k)
                    seq.setTie (k);
                heldTo = juce::jmax (heldTo, s1);
                continue;
            }

            seq.setNote (s0, degree);
            seq.setVelocity (s0, static_cast<float> (nt.velocity) / 127.0f);
            if (nt.velocity >= kAccentVelocity)
                seq.toggleAccent (s0);
            if (legato)
                seq.toggleSlide (s0);   // a legato change of pitch is a glide
            for (int k = s0 + 1; k <= s1; ++k)
                seq.setTie (k);

            heldTo = s1;
            heldDegree = degree;
            attackedAt = s0;
        }
    }

    /** Read a file into notes, saying why it refused. The grid is left alone so a
        failed import cannot leave a half-written pattern behind. */
    inline bool readFile (const juce::File& file, std::vector<Note>& notes, juce::String& error)
    {
        error.clear();
        auto stream = file.createInputStream();
        if (stream == nullptr)
            { error = file.getFileName() + " could not be opened."; return false; }

        juce::MidiFile midi;
        if (! midi.readFrom (*stream))
            { error = file.getFileName() + " is not a standard MIDI file."; return false; }

        if (midi.getTimeFormat() <= 0)
            { error = file.getFileName() + " counts time in frames, not beats, so it has no grid to follow.";
              return false; }

        notes = collectNotes (midi);
        if (notes.empty())
            { error = file.getFileName() + " holds no pitched notes - only drum hits or track metadata.";
              return false; }
        return true;
    }
}
