#include "Sequence.h"

#include <cmath>

using namespace juce;

namespace
{
    // Degree index -> semitones above the base note. Degrees run 0..13 within an
    // octave plus two shared extensions, so a 14-row editor covers an octave and a bit.
    const std::vector<int> kChromatic   { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    const std::vector<int> kMajor       { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23 };
    const std::vector<int> kMinorNat    { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22 };
    const std::vector<int> kMinorHarm   { 0, 2, 3, 5, 7, 8, 11, 12, 14, 15, 17, 19, 20, 23 };
    const std::vector<int> kPhrygian    { 0, 1, 3, 5, 7, 8, 10, 12, 13, 15, 17, 19, 20, 22 };
    const std::vector<int> kPhrygianDom { 0, 1, 4, 5, 7, 8, 11, 12, 13, 16, 17, 19, 20, 23 };
    const std::vector<int> kDorian      { 0, 2, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19, 21, 22 };
    const std::vector<int> kMixolydian  { 0, 2, 4, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22 };
    const std::vector<int> kMinorPent   { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31 };
    const std::vector<int> kMajorPent   { 0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26, 28, 31 };
    const std::vector<int> kUkrainianD  { 0, 2, 3, 6, 7, 9, 10, 12, 14, 15, 18, 19, 21, 22 };
    const std::vector<int> kWholeTone   { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26 };

    const std::vector<std::vector<int>> kScaleTables
        { kChromatic, kMajor, kMinorNat, kMinorHarm, kPhrygian, kPhrygianDom, kDorian,
          kMixolydian, kMinorPent, kMajorPent, kUkrainianD, kWholeTone };
}

const StringArray& Sequence::scaleNames()
{
    static const StringArray names
        { "Chromatic", "Major", "Minor", "Harmonic Minor", "Phrygian", "Phrygian Dominant",
          "Dorian", "Mixolydian", "Minor Pentatonic", "Major Pentatonic",
          "Ukrainian Dorian", "Whole Tone" };
    return names;
}

const std::vector<int>& Sequence::scaleOffsets (int scaleIndex)
{
    if (scaleIndex < 0 || scaleIndex >= static_cast<int> (kScaleTables.size()))
        scaleIndex = 0;
    return kScaleTables[static_cast<size_t> (scaleIndex)];
}

void Sequence::setNumSteps (int n)
{
    n = jlimit (4, kMaxSteps, n);
    steps.resize (static_cast<size_t> (n), kRest);
}

void Sequence::clickCell (int i)
{
    auto v = at (i);
    if (v == kRest)
    {
        steps[static_cast<size_t> (i)] = static_cast<juce::uint16> (kNoteBase + 7); // a fifth up
        resetVelocity (i);
    }
    else
        steps[static_cast<size_t> (i)] = v == kNoteBase + 7 ? kTie : kRest;
}

void Sequence::clearAll()
{
    std::fill (steps.begin(), steps.end(), kRest);
    std::fill (velocity.begin(), velocity.end(), kDefaultVelocity);
}

void Sequence::randomise (int seed)
{
    Random rng (seed);
    const int n = static_cast<int> (steps.size());
    for (int i = 0; i < n; ++i)
    {
        const int roll = rng.nextInt (100);
        if (roll < 18)
            steps[static_cast<size_t> (i)] = roll < 8 ? kTie : kRest;
        else
        {
            const int degree = rng.nextInt (14);
            juce::uint16 v = static_cast<juce::uint16> (kNoteBase + degree);
            if (rng.nextInt (100) < 25) v |= kAccentFlag;
            if (rng.nextInt (100) < 20) v |= kSlideFlag;
            steps[static_cast<size_t> (i)] = v;
            if (rng.nextInt (100) < 45)
                setVelocity (i, static_cast<float> (rng.nextInt ({ 45, 128 })) / 127.0f);
            else
                resetVelocity (i);
        }
    }
}

int Sequence::midiNote (int i) const
{
    if (! isNote (i))
        return baseMidi;
    const auto& offsets = scaleOffsets (scaleIndex);
    const int d = degree (i);
    const int semis = offsets[static_cast<size_t> (std::min (d, static_cast<int> (offsets.size()) - 1))];
    return jlimit (12, 108, baseMidi + semis);
}

String Sequence::encode() const
{
    bool customVel = false;
    for (size_t i = 0; i < steps.size(); ++i)
        customVel |= i < velocity.size() && velocity[i] != kDefaultVelocity;

    String s = customVel ? "v2:" : "v1:";
    s += String (baseMidi) + ":" + String (scaleIndex) + ":";
    for (size_t i = 0; i < steps.size(); ++i)
        s += (i > 0 ? "," : "") + String (static_cast<int> (steps[i]));

    if (customVel)
    {
        s += ":";
        for (size_t i = 0; i < steps.size(); ++i)
            s += (i > 0 ? "," : "")
               + String (i < velocity.size()
                            ? static_cast<int> (velocity[i]) : static_cast<int> (kDefaultVelocity));
    }
    return s;
}

Sequence Sequence::decode (const String& text)
{
    Sequence seq;
    const StringArray parts = StringArray::fromTokens (text, ":", "");
    const bool isV2 = parts.size() >= 4 && parts[0].equalsIgnoreCase ("v2");
    if (parts.size() < 4 || ! (parts[0].equalsIgnoreCase ("v1") || isV2))
        return seq;

    seq.baseMidi = parts[1].getIntValue();
    seq.scaleIndex = jlimit (0, static_cast<int> (kScaleTables.size()) - 1, parts[2].getIntValue());

    const StringArray values = StringArray::fromTokens (parts[3], ",", "");
    seq.steps.clear();
    for (const auto& v : values)
        seq.steps.push_back (static_cast<juce::uint16> (jlimit (0, 0x7fff, v.getIntValue())));
    if (seq.steps.size() < 4)
        seq.steps.assign (16, kRest);
    if (seq.steps.size() > kMaxSteps)
        seq.steps.resize (static_cast<size_t> (kMaxSteps));

    if (isV2)
    {
        seq.velocity.assign (seq.steps.size(), kDefaultVelocity);
        if (parts.size() > 4)
        {
            const StringArray vels = StringArray::fromTokens (parts[4], ",", "");
            for (size_t i = 0; i < seq.steps.size(); ++i)
            {
                const int vel = i < vels.size() ? vels[i].getIntValue()
                                                : static_cast<int> (kDefaultVelocity);
                seq.velocity[i] = static_cast<juce::uint8> (jlimit (1, 127, vel));
            }
        }
    }
    return seq;
}

void GatePattern::toggle (int sector)
{
    if (sector < 0 || sector >= numSectors)
        return;
    closedMask ^= (1u << static_cast<unsigned> (sector));
}

String GatePattern::encode() const
{
    return "g1:" + String::toHexString (closedMask).toUpperCase() + ":" + String (rotation)
           + ":" + String (numSectors);
}

GatePattern GatePattern::decode (const String& text)
{
    GatePattern g;
    const StringArray parts = StringArray::fromTokens (text, ":", "");
    if (parts.size() < 2 || ! parts[0].equalsIgnoreCase ("g1"))
        return g;
    g.closedMask = static_cast<juce::uint32> (parts[1].getHexValue32());
    // One full turn is the useful range; the engine wraps it, and a negative value
    // simply rotates the other way.
    if (parts.size() > 2)
        g.rotation = jlimit (- GatePattern::kMaxSectors, GatePattern::kMaxSectors, parts[2].getIntValue());
    if (parts.size() > 3)
    {
        int n = parts[3].getIntValue();
        if (n < 1) n = GatePattern::kDefaultSectors;
        if (n > GatePattern::kMaxSectors) n = GatePattern::kMaxSectors;
        g.numSectors = n;
    }
    else
        g.numSectors = GatePattern::kDefaultSectors;
    if (g.numSectors >= 32)
        ;
    else
        g.closedMask &= ((1u << static_cast<unsigned> (g.numSectors)) - 1u);
    return g;
}
