#include "JsonLite.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spiral303
{
    namespace
    {
        struct Parser
        {
            const std::string& text;
            size_t pos = 0;
            std::string error;

            explicit Parser (const std::string& t) : text (t) {}

            void skipSpace()
            {
                while (pos < text.size())
                {
                    const char c = text[pos];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                        ++pos;
                    else
                        break;
                }
            }

            bool fail (const std::string& why)
            {
                if (error.empty())
                    error = why + " at offset " + std::to_string (pos);
                return false;
            }

            bool parseValue (JsonValue& out)
            {
                skipSpace();
                if (pos >= text.size())
                    return fail ("unexpected end of text");

                const char c = text[pos];
                switch (c)
                {
                    case '{': return parseObject (out);
                    case '[': return parseArray (out);
                    case '"':
                        out.type = JsonValue::Type::String;
                        return parseString (out.text);
                    case 't':
                        if (text.compare (pos, 4, "true") == 0)
                        {
                            pos += 4;
                            out.type = JsonValue::Type::Bool;
                            out.boolean = true;
                            return true;
                        }
                        return fail ("expected true");
                    case 'f':
                        if (text.compare (pos, 5, "false") == 0)
                        {
                            pos += 5;
                            out.type = JsonValue::Type::Bool;
                            out.boolean = false;
                            return true;
                        }
                        return fail ("expected false");
                    case 'n':
                        if (text.compare (pos, 4, "null") == 0)
                        {
                            pos += 4;
                            out.type = JsonValue::Type::Null;
                            return true;
                        }
                        return fail ("expected null");
                    default: return parseNumber (out);
                }
            }

            bool parseNumber (JsonValue& out)
            {
                const size_t start = pos;
                if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
                    ++pos;

                bool anyDigits = false;
                while (pos < text.size() && std::isdigit (static_cast<unsigned char> (text[pos])))
                {
                    ++pos;
                    anyDigits = true;
                }

                if (pos < text.size() && text[pos] == '.')
                {
                    ++pos;
                    while (pos < text.size() && std::isdigit (static_cast<unsigned char> (text[pos])))
                    {
                        ++pos;
                        anyDigits = true;
                    }
                }

                if (anyDigits && pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
                {
                    ++pos;
                    if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
                        ++pos;
                    while (pos < text.size() && std::isdigit (static_cast<unsigned char> (text[pos])))
                        ++pos;
                }

                if (! anyDigits)
                    return fail ("expected a number");

                out.type = JsonValue::Type::Number;
                out.number = std::strtod (text.substr (start, pos - start).c_str(), nullptr);
                return true;
            }

            bool parseString (std::string& out)
            {
                if (pos >= text.size() || text[pos] != '"')
                    return fail ("expected a string");

                ++pos;
                out.clear();

                while (pos < text.size())
                {
                    const char c = text[pos++];
                    if (c == '"')
                        return true;

                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }

                    if (pos >= text.size())
                        break;

                    const char escape = text[pos++];
                    switch (escape)
                    {
                        case '"':  out += '"';  break;
                        case '\\': out += '\\'; break;
                        case '/':  out += '/';  break;
                        case 'b':  out += '\b'; break;
                        case 'f':  out += '\f'; break;
                        case 'n':  out += '\n'; break;
                        case 'r':  out += '\r'; break;
                        case 't':  out += '\t'; break;
                        case 'u':
                        {
                            if (pos + 4 > text.size())
                                return fail ("truncated unicode escape");

                            const std::string hex = text.substr (pos, 4);
                            pos += 4;
                            const unsigned int code = static_cast<unsigned int> (std::strtoul (hex.c_str(), nullptr, 16));
                            // The presets are ASCII; anything else is kept as the
                            // replacement character rather than mis-encoded.
                            out += (code < 128) ? static_cast<char> (code) : '?';
                            break;
                        }
                        default: return fail ("unknown escape");
                    }
                }

                return fail ("unterminated string");
            }

            bool parseArray (JsonValue& out)
            {
                out.type = JsonValue::Type::Array;
                ++pos; // '['
                skipSpace();

                if (pos < text.size() && text[pos] == ']')
                {
                    ++pos;
                    return true;
                }

                for (;;)
                {
                    JsonValue item;
                    if (! parseValue (item))
                        return false;

                    out.items.push_back (std::move (item));
                    skipSpace();

                    if (pos >= text.size())
                        return fail ("unterminated array");

                    const char c = text[pos++];
                    if (c == ']')
                        return true;
                    if (c != ',')
                        return fail ("expected , or ] in an array");
                }
            }

            bool parseObject (JsonValue& out)
            {
                out.type = JsonValue::Type::Object;
                ++pos; // '{'
                skipSpace();

                if (pos < text.size() && text[pos] == '}')
                {
                    ++pos;
                    return true;
                }

                for (;;)
                {
                    skipSpace();
                    std::string name;
                    if (! parseString (name))
                        return false;

                    skipSpace();
                    if (pos >= text.size() || text[pos] != ':')
                        return fail ("expected : after a member name");
                    ++pos;

                    JsonValue value;
                    if (! parseValue (value))
                        return false;

                    out.members.emplace_back (std::move (name), std::move (value));
                    skipSpace();

                    if (pos >= text.size())
                        return fail ("unterminated object");

                    const char c = text[pos++];
                    if (c == '}')
                        return true;
                    if (c != ',')
                        return fail ("expected , or } in an object");
                }
            }
        };
    }

    bool parseJson (const std::string& text, JsonValue& out, std::string& error)
    {
        Parser parser (text);
        if (! parser.parseValue (out))
        {
            error = parser.error;
            return false;
        }

        parser.skipSpace();
        if (parser.pos != text.size())
        {
            error = "trailing characters at offset " + std::to_string (parser.pos);
            return false;
        }

        return true;
    }

    std::string jsonEscape (const std::string& text)
    {
        std::string result;
        result.reserve (text.size() + 8);

        for (const char c : text)
        {
            switch (c)
            {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:
                    if (static_cast<unsigned char> (c) < 0x20)
                    {
                        char buffer[8];
                        std::snprintf (buffer, sizeof (buffer), "\\u%04x", static_cast<unsigned char> (c));
                        result += buffer;
                    }
                    else
                    {
                        result += c;
                    }
                    break;
            }
        }

        return result;
    }
}
