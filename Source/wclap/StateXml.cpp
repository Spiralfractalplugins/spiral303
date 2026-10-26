#include "StateXml.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace spiral303
{
    namespace
    {
        // JUCE's magic number for a binary XML block, and the tag of the value
        // tree the plugin stores its state in.
        constexpr uint32_t kMagicXmlNumber = 0x21324356;
        constexpr const char* kStateTag = "Spiral303";

        std::string escapeXml (const std::string& text)
        {
            std::string result;
            result.reserve (text.size());

            for (const char c : text)
            {
                switch (c)
                {
                    case '&':  result += "&amp;";  break;
                    case '<':  result += "&lt;";   break;
                    case '>':  result += "&gt;";   break;
                    case '"':  result += "&quot;"; break;
                    case '\'': result += "&apos;"; break;
                    default:   result += c;        break;
                }
            }

            return result;
        }

        std::string unescapeXml (const std::string& text)
        {
            std::string result;
            result.reserve (text.size());

            for (size_t i = 0; i < text.size();)
            {
                if (text[i] != '&')
                {
                    result += text[i++];
                    continue;
                }

                const size_t end = text.find (';', i);
                if (end == std::string::npos || end - i > 10)
                {
                    result += text[i++];
                    continue;
                }

                const std::string entity = text.substr (i + 1, end - i - 1);
                if (entity == "amp")       result += '&';
                else if (entity == "lt")   result += '<';
                else if (entity == "gt")   result += '>';
                else if (entity == "quot") result += '"';
                else if (entity == "apos") result += '\'';
                else                       result += text.substr (i, end - i + 1);

                i = end + 1;
            }

            return result;
        }

        /** Reads one attribute out of a tag. Returns false when it is not there. */
        bool readAttribute (const std::string& tag, const std::string& name, std::string& out)
        {
            const std::string key = name + "=\"";
            size_t pos = tag.find (key);
            if (pos == std::string::npos)
                return false;

            pos += key.size();
            const size_t end = tag.find ('"', pos);
            if (end == std::string::npos)
                return false;

            out = unescapeXml (tag.substr (pos, end - pos));
            return true;
        }

        std::string formatValue (float value)
        {
            char buffer[32];
            // Nine significant digits round-trip a 32-bit float exactly.
            std::snprintf (buffer, sizeof (buffer), "%.9g", static_cast<double> (value));
            return buffer;
        }
    }

    std::vector<uint8_t> encodeState (const PluginState& state)
    {
        std::string xml = "<";
        xml += kStateTag;

        if (! state.sequence.empty())
            xml += " sequence=\"" + escapeXml (state.sequence) + "\"";
        if (! state.gate.empty())
            xml += " gate=\"" + escapeXml (state.gate) + "\"";
        if (! state.presetNote.empty())
            xml += " presetNote=\"" + escapeXml (state.presetNote) + "\"";

        xml += ">";

        for (int i = 0; i < kParamCount; ++i)
        {
            xml += "<PARAM id=\"";
            xml += paramDef (i).id;
            xml += "\" value=\"";
            xml += formatValue (state.values[i]);
            xml += "\"/>";
        }

        xml += "</";
        xml += kStateTag;
        xml += ">";

        std::vector<uint8_t> out;
        out.reserve (xml.size() + 16);

        const uint32_t magic = kMagicXmlNumber;
        const uint32_t length = static_cast<uint32_t> (xml.size() + 9);
        for (int i = 0; i < 4; ++i)
            out.push_back (static_cast<uint8_t> ((magic >> (i * 8)) & 0xffu));
        for (int i = 0; i < 4; ++i)
            out.push_back (static_cast<uint8_t> ((length >> (i * 8)) & 0xffu));

        out.insert (out.end(), xml.begin(), xml.end());
        out.push_back (0);

        return out;
    }

    bool decodeState (const void* data, size_t size, PluginState& state)
    {
        if (data == nullptr || size < 9)
            return false;

        const auto* bytes = static_cast<const uint8_t*> (data);

        uint32_t magic = 0, length = 0;
        for (int i = 0; i < 4; ++i)
            magic |= static_cast<uint32_t> (bytes[i]) << (i * 8);
        for (int i = 0; i < 4; ++i)
            length |= static_cast<uint32_t> (bytes[4 + i]) << (i * 8);

        if (magic != kMagicXmlNumber)
            return false;

        const size_t textSize = (length > 0 && length - 9 < size - 8) ? length - 9 : size - 8;
        const std::string xml (reinterpret_cast<const char*> (bytes + 8), textSize);

        if (xml.find (kStateTag) == std::string::npos)
            return false;

        PluginState parsed;

        // The root tag carries the pattern and the gate.
        const size_t rootStart = xml.find ('<');
        const size_t rootEnd = xml.find ('>', rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
            return false;

        const std::string rootTag = xml.substr (rootStart, rootEnd - rootStart + 1);
        readAttribute (rootTag, "sequence", parsed.sequence);
        readAttribute (rootTag, "gate", parsed.gate);
        readAttribute (rootTag, "presetNote", parsed.presetNote);

        // Then the parameters.
        size_t pos = 0;
        while (true)
        {
            const size_t start = xml.find ("<PARAM", pos);
            if (start == std::string::npos)
                break;

            const size_t end = xml.find ('>', start);
            if (end == std::string::npos)
                break;

            const std::string tag = xml.substr (start, end - start + 1);

            std::string id, value;
            if (readAttribute (tag, "id", id) && readAttribute (tag, "value", value))
            {
                const int index = paramIndexForId (id.c_str());
                if (index >= 0)
                    parsed.values[index] = static_cast<float> (std::atof (value.c_str()));
            }

            pos = end + 1;
        }

        state = std::move (parsed);
        return true;
    }
}
