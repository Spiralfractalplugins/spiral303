#pragma once

#include <algorithm>
#include <vector>

#include "../core/CoreInclude.h"

/** One sequencer step.
    Values below kNoteBase encode a rest or a tie; a real note is kNoteBase plus the
    degree offset above the base note, with the slide and accent flags on top.
    Velocities live in a parallel array (1..127); older patterns and rests read 1.0. */
struct Sequence
{
    static constexpr int kMaxSteps = 32;
    /** A cell is a bit field: the note marker in bit 7, the degree below it, and the
        two flags above it. Rest and tie are the only codes under 128. */
    static constexpr juce::uint16 kRest = 0, kTie = 1;
    static constexpr juce::uint16 kNoteFlag = 0x80, kDegreeMask = 0x7f;
    static constexpr juce::uint16 kNoteBase = kNoteFlag;
    static constexpr juce::uint16 kSlideFlag = 0x100, kAccentFlag = 0x200;
    static constexpr juce::uint8 kDefaultVelocity = 127;   // full level keeps the legacy look

    int baseMidi = 36;                       // C2, the step-0 pitch of degree 0
    int scaleIndex = 1;                      // index into scaleNames()
    std::vector<juce::uint16> steps = std::vector<juce::uint16> (16, kRest);
    std::vector<juce::uint8> velocity {};    // parallel to steps; empty = all default

    static const juce::StringArray& scaleNames();
    /** Semitone offsets of the scale a degree index walks through. */
    static const std::vector<int>& scaleOffsets (int scaleIndex);

    int numSteps() const { return static_cast<int> (steps.size()); }
    void setNumSteps (int n);

    bool isRest (int i) const { return at (i) == kRest; }
    bool isTie (int i) const { return at (i) == kTie; }
    bool isNote (int i) const { return (at (i) & kNoteFlag) != 0; }
    bool hasSlide (int i) const { return (at (i) & kSlideFlag) != 0; }
    bool hasAccent (int i) const { return (at (i) & kAccentFlag) != 0; }

    /** Degree above the base note; clamped for scale lookup. */
    int degree (int i) const { return isNote (i) ? static_cast<int> (at (i) & kDegreeMask) : 0; }
    void setDegree (int i, int d)
    {
        if (isNote (i))
            steps[static_cast<size_t> (i)] = static_cast<juce::uint16> (kNoteFlag | (d & 0x7f)
                                                                        | (hasSlide (i) ? kSlideFlag : 0)
                                                                        | (hasAccent (i) ? kAccentFlag : 0));
    }
    void toggleSlide (int i) { steps[static_cast<size_t> (i)] ^= kSlideFlag; }
    void toggleAccent (int i) { steps[static_cast<size_t> (i)] ^= kAccentFlag; }

    /** Place a cell without the click cycle: a fresh note of this degree, flags
        clear, so setVelocity/toggleSlide/toggleAccent can finish it. The MIDI
        importer fills a whole pattern this way. */
    void setNote (int i, int d)
    {
        if (i >= 0 && i < static_cast<int> (steps.size()))
            steps[static_cast<size_t> (i)] = static_cast<juce::uint16> (kNoteFlag | (d & 0x7f));
    }
    void setTie (int i)
    {
        if (i >= 0 && i < static_cast<int> (steps.size()))
            steps[static_cast<size_t> (i)] = kTie;
    }

    /** Velocity of a note as 0..1; rests, ties and legacy patterns read 1.0. */
    float velocityOf (int i) const
    {
        if (! isNote (i) || static_cast<size_t> (i) >= velocity.size())
            return 1.0f;
        return static_cast<float> (velocity[static_cast<size_t> (i)]) / 127.0f;
    }

    void setVelocity (int i, float norm)
    {
        if (i < 0 || i >= static_cast<int> (steps.size()))
            return;
        if (velocity.size() != steps.size())
            velocity.resize (steps.size(), kDefaultVelocity);
        velocity[static_cast<size_t> (i)] =
            static_cast<juce::uint8> (jlimit (1, 127,
                static_cast<int> (norm * 127.0f + 0.5f)));
    }

    void resetVelocity (int i)
    {
        if (static_cast<size_t> (i) < velocity.size())
            velocity[static_cast<size_t> (i)] = kDefaultVelocity;
    }

    bool hasCustomVelocity (int i) const
    {
        return static_cast<size_t> (i) < velocity.size()
               && velocity[static_cast<size_t> (i)] != kDefaultVelocity;
    }

    /** Cycle an empty cell through note-low → tie → rest; notes just reset to rest. */
    void clickCell (int i);
    void clearAll();
    void randomise (int seed);

    /** MIDI note of a step for the given scale, clamped to a sane range. */
    int midiNote (int i) const;

    juce::String encode() const;
    static Sequence decode (const juce::String&);

private:
    juce::uint16 at (int i) const
    {
        if (i < 0 || i >= static_cast<int> (steps.size()))
            return kRest;
        return steps[static_cast<size_t> (i)];
    }
};

/** The circular gate: 16 sectors, open ones unset in the mask. */
struct GatePattern
{
    static constexpr int kDefaultSectors = 16;
    static constexpr int kMaxSectors = 32;

    int numSectors = kDefaultSectors;
    juce::uint32 closedMask = 0; // bit set = this sector mutes
    /** Rotates the wheel against the clock, so the same mask can land on a different beat. */
    int rotation = 0;

    void clampSectors() { if (numSectors < 1) numSectors = 1; if (numSectors > kMaxSectors) numSectors = kMaxSectors; }

    bool isClosed (int sector) const
    {
        if (sector < 0 || sector >= numSectors)
            return false;
        return (closedMask & (1u << static_cast<unsigned> (sector))) != 0;
    }
    void toggle (int sector);
    void setSectorCount (int n)
    {
        if (n < 1) n = 1;
        if (n > kMaxSectors) n = kMaxSectors;
        numSectors = n;
        if (numSectors < 32)
            closedMask &= ((1u << static_cast<unsigned> (numSectors)) - 1u);
        if (numSectors > 0)
            rotation %= numSectors;
    }
    void clearAll() { closedMask = 0; }

    juce::String encode() const;
    static GatePattern decode (const juce::String&);
};
