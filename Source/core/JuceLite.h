#pragma once

// A stand-in for the slice of JUCE that the DSP core uses.
//
// The WebAssembly build cannot compile JUCE itself: juce_core is tied to the
// host operating system, and juce_gui_basics has no meaning inside a wasm
// sandbox. So the audio core is compiled against this header instead, and the
// native build keeps using the real JUCE.
//
// Every routine here is a line-by-line port of its JUCE original, because the
// DSP is bit-exact sensitive: a different rounding, a different random
// sequence or a different clamp changes the offline renders and breaks the
// loudness calibration tables.
//
// Only what Source/dsp actually uses is here. If the core grows a new JUCE
// dependency, add it here rather than pulling JUCE into the wasm build.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace juce
{
    //==============================================================================
    // Fixed-width types, as in juce_core/system/juce_Types.h
    using uint8 = std::uint8_t;
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    using int8 = std::int8_t;
    using int16 = std::int16_t;
    using int32 = std::int32_t;
    using int64 = std::int64_t;

    //==============================================================================
    // Maths - juce_core/maths/juce_MathsFunctions.h

    template <typename Type>
    constexpr Type jmax (Type a, Type b)                                   { return a < b ? b : a; }

    template <typename Type>
    constexpr Type jmin (Type a, Type b)                                   { return b < a ? b : a; }

    template <typename Type>
    Type jlimit (Type lowerLimit, Type upperLimit, Type valueToConstrain) noexcept
    {
        return valueToConstrain < lowerLimit ? lowerLimit
                                             : (valueToConstrain > upperLimit ? upperLimit : valueToConstrain);
    }

    template <typename FloatType>
    struct MathConstants
    {
        static constexpr FloatType pi = static_cast<FloatType> (3.141592653589793238L);
        static constexpr FloatType twoPi = static_cast<FloatType> (2 * 3.141592653589793238L);
        static constexpr FloatType halfPi = static_cast<FloatType> (3.141592653589793238L / 2);
        static constexpr FloatType euler = static_cast<FloatType> (2.71828182845904523536L);
        static constexpr FloatType sqrt2 = static_cast<FloatType> (1.4142135623730950488L);
    };

    //==============================================================================
    // Range - juce_core/maths/juce_Range.h, the small part the random generator
    // takes for a bounded roll.

    template <typename Type>
    struct Range
    {
        Range() = default;
        Range (Type startValue, Type endValue) : start (startValue), end (endValue) {}

        Type getStart() const noexcept { return start; }
        Type getEnd() const noexcept { return end; }
        Type getLength() const noexcept { return static_cast<Type> (end - start); }

        Type start {}, end {};
    };

    //==============================================================================
    // Random - juce_core/maths/juce_Random.{h,cpp}
    //
    // A 48-bit LCG, the same generator JUCE uses. The analog drift and the
    // noise floor draw from it, so a different generator would change the
    // renders even though both are "random".

    class Random final
    {
    public:
        explicit Random (int64 seedValue) noexcept : seed (seedValue) {}

        int nextInt() noexcept
        {
            seed = (int64) (((((uint64) seed) * 0x5deece66dLL) + 11) & 0xffffffffffffLL);
            return (int) (seed >> 16);
        }

        int nextInt (int maxValue) noexcept
        {
            return (int) ((((unsigned int) nextInt()) * (uint64) maxValue) >> 32);
        }

        int nextInt (Range<int> range) noexcept
        {
            return range.getStart() + nextInt (range.getLength());
        }

        float nextFloat() noexcept
        {
            auto result = static_cast<float> (static_cast<uint32> (nextInt()))
                          / (static_cast<float> (std::numeric_limits<uint32>::max()) + 1.0f);
            return jmin (result, 1.0f - std::numeric_limits<float>::epsilon());
        }

        double nextDouble() noexcept
        {
            return static_cast<uint32> (nextInt()) / (std::numeric_limits<uint32>::max() + 1.0);
        }

        void setSeed (int64 newSeed) noexcept { seed = newSeed; }
        int64 getSeed() const noexcept { return seed; }

    private:
        int64 seed;
    };

    //==============================================================================
    // String - the small part of juce_core/text/juce_String.h that the
    // sequencer's encode/decode uses.

    class String
    {
    public:
        String() = default;
        String (const char* text) : text (text != nullptr ? text : "") {}
        String (const std::string& text) : text (text) {}
        String (int value) : text (std::to_string (value)) {}
        String (int64 value) : text (std::to_string (value)) {}
        String (uint32 value) : text (std::to_string (value)) {}

        static String fromUTF8 (const char* data, int size)
        {
            return String (std::string (data != nullptr ? data : "", data != nullptr ? (size_t) size : 0));
        }

        bool isEmpty() const noexcept { return text.empty(); }
        bool isNotEmpty() const noexcept { return ! text.empty(); }
        int length() const noexcept { return (int) text.size(); }

        const std::string& raw() const noexcept { return text; }

        bool operator== (const String& other) const noexcept { return text == other.text; }
        bool operator!= (const String& other) const noexcept { return text != other.text; }

        String& operator+= (const String& other) { text += other.text; return *this; }
        String& operator+= (const char* other) { if (other != nullptr) text += other; return *this; }
        String& operator+= (char other) { text += other; return *this; }

        String operator+ (const String& other) const { return String (text + other.text); }
        String operator+ (const char* other) const { return String (text + (other != nullptr ? other : "")); }

        bool equalsIgnoreCase (const String& other) const noexcept
        {
            if (text.size() != other.text.size())
                return false;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (std::tolower (static_cast<unsigned char> (text[i]))
                    != std::tolower (static_cast<unsigned char> (other.text[i])))
                    return false;
            }
            return true;
        }

        String toUpperCase() const
        {
            std::string result = text;
            for (auto& c : result)
                c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
            return String (result);
        }

        String trim() const
        {
            const auto isSpace = [] (char c)
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
            };

            size_t first = 0;
            while (first < text.size() && isSpace (text[first]))
                ++first;
            size_t last = text.size();
            while (last > first && isSpace (text[last - 1]))
                --last;
            return String (text.substr (first, last - first));
        }

        int getIntValue() const noexcept
        {
            return (int) std::strtol (text.c_str(), nullptr, 10);
        }

        uint32 getHexValue32() const noexcept
        {
            return (uint32) std::strtoul (text.c_str(), nullptr, 16);
        }

        /** The byte-wise hex form JUCE produces: two digits per byte, lower case. */
        static String toHexString (uint32 value)
        {
            static const char* const hexDigits = "0123456789abcdef";
            std::string result;
            result.reserve (8);
            for (int i = 3; i >= 0; --i)
            {
                const auto byte = static_cast<unsigned char> ((value >> (i * 8)) & 0xffu);
                result += hexDigits[byte >> 4];
                result += hexDigits[byte & 0xf];
            }
            return String (result);
        }

    private:
        std::string text;
    };

    inline String operator+ (const char* lhs, const String& rhs)
    {
        return String (std::string (lhs != nullptr ? lhs : "") + rhs.raw());
    }

    inline bool operator== (const char* lhs, const String& rhs)
    {
        return String (lhs) == rhs;
    }

    //==============================================================================
    // StringArray - juce_core/text/juce_StringArray.h, only what the sequencer needs.

    class StringArray
    {
    public:
        StringArray() = default;

        StringArray (std::initializer_list<String> items) : items (items) {}

        /** Splits at every character of `delimiter`, keeping empty tokens, the way
            JUCE 8 tokenises: no trimming, no skipping. `quoteCharacters` is unused
            here - the sequencer never quotes its tokens. */
        static StringArray fromTokens (const String& text, const String& delimiter, const String& quoteCharacters)
        {
            (void) quoteCharacters;

            StringArray result;
            const std::string& source = text.raw();
            const std::string& breaks = delimiter.raw();

            if (source.empty())
                return result;

            size_t start = 0;
            for (;;)
            {
                size_t pos = start;
                while (pos < source.size() && breaks.find (source[pos]) == std::string::npos)
                    ++pos;

                result.items.push_back (String (source.substr (start, pos - start)));

                if (pos >= source.size())
                    break;

                start = pos + 1;
            }
            return result;
        }

        int size() const noexcept { return (int) items.size(); }
        bool isEmpty() const noexcept { return items.empty(); }
        const String& operator[] (int index) const { return items[(size_t) index]; }
        void add (const String& item) { items.push_back (item); }

        auto begin() const noexcept { return items.begin(); }
        auto end() const noexcept { return items.end(); }

    private:
        std::vector<String> items;
    };

    //==============================================================================
    // AudioBuffer - juce_audio_basics/buffers/juce_AudioBuffer.h, the part the
    // engine and the analog thickener use.

    template <typename SampleType>
    class AudioBuffer
    {
    public:
        AudioBuffer() = default;

        AudioBuffer (int numChannels, int numSamples)
        {
            setSize (numChannels, numSamples);
        }

        void setSize (int numChannels, int numSamples)
        {
            channels = std::max (0, numChannels);
            samples = std::max (0, numSamples);
            data.assign ((size_t) channels, std::vector<SampleType> ((size_t) samples, (SampleType) 0));
        }

        int getNumChannels() const noexcept { return channels; }
        int getNumSamples() const noexcept { return samples; }

        SampleType* getWritePointer (int channel) noexcept
        {
            return data[(size_t) channel].data();
        }

        const SampleType* getReadPointer (int channel) const noexcept
        {
            return data[(size_t) channel].data();
        }

        void clear()
        {
            for (auto& channel : data)
                std::fill (channel.begin(), channel.end(), (SampleType) 0);
        }

    private:
        std::vector<std::vector<SampleType>> data;
        int channels = 0, samples = 0;
    };

    //==============================================================================
    // MidiMessage / MidiBuffer - juce_audio_basics/midi/juce_MidiMessage.h and
    // juce_MidiBuffer.h. The engine only reads notes and controllers.
    //
    // The signatures follow JUCE 8: the channel comes first and the velocity
    // is a 0..127 byte, not a 0..1 float.

    namespace MidiHelpers
    {
        inline uint8 validVelocity (int v) noexcept
        {
            return (uint8) jlimit (0, 127, v);
        }

        /** The same "round half to even" as JUCE's roundToInt. */
        inline int roundToInt (float value) noexcept
        {
            union { int asInt[2]; double asDouble; } n;
            n.asDouble = ((double) value) + 6755399441055744.0;
            return n.asInt[0];
        }

        inline uint8 floatValueToMidiByte (float v) noexcept
        {
            return validVelocity (roundToInt (v * 127.0f));
        }
    }

    class MidiMessage
    {
    public:
        enum class Type { NoteOn, NoteOff, Controller };

        MidiMessage() = default;

        static MidiMessage noteOn (int channel, int noteNumber, float velocity)
        {
            return noteOn (channel, noteNumber, MidiHelpers::floatValueToMidiByte (velocity));
        }

        static MidiMessage noteOn (int channel, int noteNumber, uint8 velocity)
        {
            MidiMessage m;
            m.type = Type::NoteOn;
            m.channel = channel;
            m.noteNumber = noteNumber;
            m.velocity = velocity;
            return m;
        }

        static MidiMessage noteOff (int channel, int noteNumber, float velocity)
        {
            return noteOff (channel, noteNumber, MidiHelpers::floatValueToMidiByte (velocity));
        }

        static MidiMessage noteOff (int channel, int noteNumber, uint8 velocity)
        {
            MidiMessage m;
            m.type = Type::NoteOff;
            m.channel = channel;
            m.noteNumber = noteNumber;
            m.velocity = velocity;
            return m;
        }

        static MidiMessage noteOff (int channel, int noteNumber)
        {
            return noteOff (channel, noteNumber, (uint8) 0);
        }

        static MidiMessage controllerEvent (int channel, int controllerType, int value)
        {
            MidiMessage m;
            m.type = Type::Controller;
            m.channel = channel;
            m.controllerNumber = controllerType;
            m.controllerValue = value;
            return m;
        }

        bool isNoteOn() const noexcept { return type == Type::NoteOn && velocity != 0; }
        bool isNoteOff() const noexcept { return type == Type::NoteOff || (type == Type::NoteOn && velocity == 0); }
        bool isController() const noexcept { return type == Type::Controller; }

        int getNoteNumber() const noexcept { return noteNumber; }
        /** JUCE 8 returns the velocity as a 0..127 byte. */
        uint8 getVelocity() const noexcept { return velocity; }
        int getControllerNumber() const noexcept { return controllerNumber; }
        int getControllerValue() const noexcept { return controllerValue; }
        int getChannel() const noexcept { return channel; }

    private:
        Type type = Type::NoteOn;
        int channel = 1;
        int noteNumber = 0;
        uint8 velocity = 0;
        int controllerNumber = 0;
        int controllerValue = 0;
    };

    class MidiBufferMetadata
    {
    public:
        MidiBufferMetadata (const MidiMessage& message, int samplePosition)
            : message (message), samplePosition (samplePosition) {}

        const MidiMessage& getMessage() const noexcept { return message; }
        int samplePosition = 0;

    private:
        MidiMessage message;
    };

    class MidiBuffer
    {
    public:
        MidiBuffer() = default;

        void clear() { events.clear(); }

        void addEvent (const MidiMessage& message, int samplePosition)
        {
            events.push_back (MidiBufferMetadata (message, samplePosition));
        }

        int getNumEvents() const noexcept { return (int) events.size(); }

        bool empty() const noexcept { return events.empty(); }

        auto begin() const noexcept { return events.begin(); }
        auto end() const noexcept { return events.end(); }

    private:
        std::vector<MidiBufferMetadata> events;
    };

    //==============================================================================
    // FloatVectorOperations - juce_audio_basics/utilities/juce_FloatVectorOperations.h

    struct FloatVectorOperations
    {
        static void copy (float* dest, const float* src, int numValues)
        {
            if (numValues > 0)
                std::memmove (dest, src, sizeof (float) * (size_t) numValues);
        }
    };
}
