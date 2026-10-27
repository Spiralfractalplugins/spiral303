#include "WebView.h"

#include <cstdio>
#include <cstring>

#include "JsonLite.h"
#include "Spiral303Clap.h"
#include "WebAssets.h"

#include "../dsp/FilterResponse.h"

namespace spiral303
{
    namespace
    {
        // The native design rectangle, so a host that asks for the default size
        // gets the same 1:1 view as the JUCE editor.
        constexpr uint32_t kUiWidth = 1180;
        constexpr uint32_t kUiHeight = 780;

        // The same window the JUCE editor sets with setResizeLimits.
        constexpr uint32_t kUiMinWidth = 900, kUiMinHeight = 600;
        constexpr uint32_t kUiMaxWidth = 3200, kUiMaxHeight = 2000;

        // The draft "clap.param-hovered/1" extension: the plugin reports the
        // hovered parameter to the host.
        constexpr const char* kExtParamHovered = "clap.param-hovered/1";

        const char* mimeTypeFor (const char* path)
        {
            if (path == nullptr)
                return "application/octet-stream";

            const std::string p (path);
            const auto endsWith = [&p] (const char* suffix)
            {
                const size_t length = std::strlen (suffix);
                return p.size() >= length && p.compare (p.size() - length, length, suffix) == 0;
            };

            if (endsWith (".html")) return "text/html";
            if (endsWith (".js")) return "text/javascript";
            if (endsWith (".css")) return "text/css";
            if (endsWith (".json")) return "application/json";

            return "text/plain";
        }

        std::string numberToText (double value)
        {
            char buffer[64];
            std::snprintf (buffer, sizeof (buffer), "%.6g", value);
            return std::string (buffer);
        }

        /** Base64 of 16-bit signed little-endian mono PCM (the samples are
            interleaved floats; they are clamped and scaled here). */
        void base64Encode (const std::vector<float>& pcm, std::string& out)
        {
            static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            out.clear();
            out.reserve ((pcm.size() * 2 + 2) / 3 * 4);

            std::vector<uint8_t> bytes;
            bytes.reserve (pcm.size() * 2);
            for (float v : pcm)
            {
                const float clamped = std::max (-1.0f, std::min (1.0f, v));
                const int16_t s = static_cast<int16_t> (std::lround (clamped * 32767.0f));
                bytes.push_back (static_cast<uint8_t> (s & 0xff));
                bytes.push_back (static_cast<uint8_t> ((s >> 8) & 0xff));
            }

            for (size_t i = 0; i < bytes.size(); i += 3)
            {
                const uint32_t b0 = bytes[i];
                const uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
                const uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
                const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
                out.push_back (table[(n >> 18) & 63]);
                out.push_back (table[(n >> 12) & 63]);
                out.push_back (i + 1 < bytes.size() ? table[(n >> 6) & 63] : '=');
                out.push_back (i + 2 < bytes.size() ? table[n & 63] : '=');
            }
        }
    }

    //==============================================================================
    WebView::WebView (ClapPlugin& pluginIn, const clap_host_t* hostIn)
        : plugin (pluginIn), host (hostIn)
    {
        if (host != nullptr && host->get_extension != nullptr)
        {
            hostWebview = static_cast<const clap_host_webview_t*> (
                host->get_extension (host, CLAP_EXT_WEBVIEW));

            hostParamHovered = static_cast<const HostParamHovered*> (
                host->get_extension (host, kExtParamHovered));
        }
    }

    //==============================================================================
    namespace
    {
        WebView* viewOf (const clap_plugin* plugin)
        {
            auto* owner = static_cast<ClapPlugin*> (plugin->plugin_data);
            return owner != nullptr ? owner->webViewPtr() : nullptr;
        }
    }

    int32_t CLAP_ABI WebView::get_uri (const clap_plugin*, char* uri, uint32_t capacity)
    {
        // The page is served from the plugin's memory; the host asks for the
        // resources by path.
        static const char* const kUri = "/index.html";

        const size_t length = std::strlen (kUri);
        if (capacity == 0)
            return static_cast<int32_t> (length + 1);

        if (capacity < length + 1)
            return static_cast<int32_t> (length + 1);   // the caller comes back with more room

        std::memcpy (uri, kUri, length + 1);
        return static_cast<int32_t> (length + 1);
    }

    bool CLAP_ABI WebView::get_resource (const clap_plugin*, const char* path, char* mime,
                                         uint32_t mime_capacity, const clap_ostream_t* stream)
    {
        if (path == nullptr || stream == nullptr || stream->write == nullptr)
            return false;

        std::string wanted (path);
        if (! wanted.empty() && wanted[0] == '/')
            wanted.erase (0, 1);

        for (int i = 0; i < WebAssetData::assetCount; ++i)
        {
            const auto& asset = WebAssetData::assets[i];
            std::string name (asset.path);
            if (! name.empty() && name[0] == '/')
                name.erase (0, 1);

            if (wanted != name)
                continue;

            if (mime != nullptr && mime_capacity > 0)
            {
                const char* type = mimeTypeFor (asset.path);
                std::snprintf (mime, mime_capacity, "%s", type);
            }

            return stream->write (stream, asset.text, static_cast<uint64_t> (asset.size))
                   == static_cast<uint64_t> (asset.size);
        }

        return false;
    }

    bool CLAP_ABI WebView::receive (const clap_plugin* plugin, const void* buffer, uint32_t size)
    {
        auto* self = viewOf (plugin);
        if (self == nullptr || buffer == nullptr || size == 0)
            return false;

        self->handleMessage (std::string (static_cast<const char*> (buffer), size));
        return true;
    }

    //==============================================================================
    bool CLAP_ABI WebView::gui_is_api_supported (const clap_plugin*, const char* api, bool isFloating)
    {
        if (api == nullptr)
            return false;

        return ! isFloating && std::strcmp (api, CLAP_WINDOW_API_WEBVIEW) == 0;
    }

    bool CLAP_ABI WebView::gui_get_preferred_api (const clap_plugin*, const char** api, bool* isFloating)
    {
        if (api == nullptr || isFloating == nullptr)
            return false;

        *api = CLAP_WINDOW_API_WEBVIEW;
        *isFloating = false;
        return true;
    }

    bool CLAP_ABI WebView::gui_create (const clap_plugin* plugin, const char* api, bool isFloating)
    {
        auto* self = viewOf (plugin);
        if (self == nullptr || api == nullptr)
            return false;

        if (isFloating || std::strcmp (api, CLAP_WINDOW_API_WEBVIEW) != 0)
            return false;

        self->created = true;
        return true;
    }

    void CLAP_ABI WebView::gui_destroy (const clap_plugin* plugin)
    {
        auto* self = viewOf (plugin);
        if (self == nullptr)
            return;

        self->created = false;
        self->visible = false;
    }

    bool CLAP_ABI WebView::gui_set_scale (const clap_plugin*, double)
    {
        // The page measures itself in the window it is given, so there is no
        // scale to apply.
        return true;
    }

    bool CLAP_ABI WebView::gui_get_size (const clap_plugin*, uint32_t* width, uint32_t* height)
    {
        if (width == nullptr || height == nullptr)
            return false;

        *width = kUiWidth;
        *height = kUiHeight;
        return true;
    }

    bool CLAP_ABI WebView::gui_can_resize (const clap_plugin*)
    {
        return true;
    }

    bool CLAP_ABI WebView::gui_get_resize_hints (const clap_plugin*, clap_gui_resize_hints_t* hints)
    {
        if (hints == nullptr)
            return false;

        std::memset (hints, 0, sizeof (*hints));
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = false;
        return true;
    }

    bool CLAP_ABI WebView::gui_adjust_size (const clap_plugin*, uint32_t* width, uint32_t* height)
    {
        // CLAP's resize hints carry no minimum, so the limits live here: a host
        // that asks for a tiny window gets the smallest one the layout survives.
        if (width == nullptr || height == nullptr)
            return false;

        *width  = std::min (std::max (*width,  kUiMinWidth),  kUiMaxWidth);
        *height = std::min (std::max (*height, kUiMinHeight), kUiMaxHeight);
        return true;
    }

    bool CLAP_ABI WebView::gui_set_size (const clap_plugin*, uint32_t, uint32_t)
    {
        return true;
    }

    bool CLAP_ABI WebView::gui_set_parent (const clap_plugin*, const clap_window_t*)
    {
        return true;
    }

    bool CLAP_ABI WebView::gui_set_transient (const clap_plugin*, const clap_window_t*)
    {
        return true;
    }

    void CLAP_ABI WebView::gui_suggest_title (const clap_plugin*, const char*)
    {
    }

    bool CLAP_ABI WebView::gui_show (const clap_plugin* plugin)
    {
        auto* self = viewOf (plugin);
        if (self == nullptr)
            return false;

        self->visible = true;
        self->sendState();
        return true;
    }

    bool CLAP_ABI WebView::gui_hide (const clap_plugin* plugin)
    {
        auto* self = viewOf (plugin);
        if (self == nullptr)
            return false;

        self->visible = false;
        return true;
    }

    //==============================================================================
    void WebView::send (const std::string& message)
    {
        if (hostWebview == nullptr || hostWebview->send == nullptr)
            return;

        // The message is kept alive until the next one: a host copies the data
        // during the call, but this way the buffer is still valid afterwards.
        lastMessage = message;
        hostWebview->send (host, lastMessage.data(), static_cast<uint64_t> (lastMessage.size()));
    }

    void WebView::sendState()
    {
        sendStateBody (true);
    }

    void WebView::sendPresetEcho()
    {
        sendStateBody (false);
    }

    std::string WebView::staticsBlock()
    {
        // The parts of the state that never move: the parameter table's ranges
        // and names, the choices of the enum knobs, the version. The page gets
        // them once, with the state that answers hello.
        std::string json = "\"choices\":{";

        bool first = true;
        for (int i = 0; i < paramCount(); ++i)
        {
            const auto& def = paramDef (i);
            if (! def.isChoice)
                continue;

            if (! first)
                json += ',';
            first = false;
            json += '"';
            json += def.id;
            json += "\":[";
            for (int c = 0; c < def.numChoices; ++c)
            {
                if (c > 0)
                    json += ',';
                json += '"';
                json += jsonEscape (def.choices[c]);
                json += '"';
            }
            json += ']';
        }
        json += "},\"ranges\":{";

        // The ranges, so that the page draws the knobs on the same scale the
        // plugin uses.
        first = true;
        for (int i = 0; i < paramCount(); ++i)
        {
            const auto& def = paramDef (i);
            if (! first)
                json += ',';
            first = false;
            json += '"';
            json += def.id;
            json += "\":{\"min\":";
            json += numberToText (def.minValue);
            json += ",\"max\":";
            json += numberToText (def.maxValue);
            json += ",\"default\":";
            json += numberToText (def.defaultValue);
            // The unit and the digit count are what the native getText() puts in
            // the knob's value label, so the page writes the same string.
            json += ",\"unit\":\"";
            json += jsonEscape (def.unit);
            json += "\",\"decimals\":";
            json += std::to_string (def.decimals);
            json += ",\"name\":\"";
            json += jsonEscape (def.name);
            json += '"';
            json += '}';
        }
        json += '}';

        // The header title carries the version, so the page cannot invent one.
        // The macro expands to a C++ string literal, whose quotes are not part
        // of the bytes: JSON still needs its own.
        json += ",\"version\":\"" SPIRAL303_VERSION "\"";

        return json;
    }

    std::string WebView::curveBlock()
    {
        // The response curve, the way the scope draws it.
        auto& core = plugin.core();
        const float cutoff = core.uiParamValue (kCutoff);
        const float res = core.uiParamValue (kRes);
        const float slope = core.uiParamValue (kTone);

        std::string json;
        for (int i = 0; i < 96; ++i)
        {
            if (i > 0)
                json += ',';
            const float freq = 20.0f * std::pow (1000.0f, static_cast<float> (i) / 95.0f);
            json += numberToText (filterResponseDb (freq, cutoff, res, slope));
        }
        return json;
    }

    void WebView::sendStateBody (bool withStatics)
    {
        auto& core = plugin.core();

        std::string json = "{\"t\":\"state\",\"params\":{";
        bool first = true;
        for (int i = 0; i < paramCount(); ++i)
        {
            if (! first)
                json += ',';
            first = false;
            json += '"';
            json += paramDef (i).id;
            json += "\":";
            json += numberToText (static_cast<double> (core.uiParamValue (i)));
        }
        json += "},\"seq\":\"";

        const auto sequence = core.getSequence();
        json += jsonEscape (sequence.encode().raw());

        json += "\",\"gate\":\"";
        const auto gate = core.getGate();
        json += jsonEscape (gate.encode().raw());

        json += "\",\"preset\":\"" + jsonEscape (core.currentPresetName()) + "\"";
        json += ",\"note\":\"" + jsonEscape (core.presetNote()) + "\"";
        json += ",\"skin\":" + numberToText (core.uiParamValue (kUiSkin));
        json += ",\"curve\":[" + curveBlock() + "]";

        if (withStatics)
            json += "," + staticsBlock();

        json += "}";

        send (json);
    }

    void WebView::sendParamEcho (int index, double value)
    {
        if (index < 0 || index >= paramCount())
            return;

        std::string json = std::string ("{\"t\":\"param\",\"id\":\"") + jsonEscape (paramDef (index).id)
                         + "\",\"v\":" + numberToText (value);

        // The scope redraws from the curve, so the three knobs that shape the
        // filter bring it along; nothing else can move it.
        if (index == kCutoff || index == kRes || index == kTone)
            json += ",\"curve\":[" + curveBlock() + "]";

        json += "}";
        send (json);
    }

    void WebView::sendSeqEcho()
    {
        send ("{\"t\":\"seq\",\"seq\":\"" + jsonEscape (plugin.core().getSequence().encode().raw()) + "\"}");
    }

    void WebView::sendGateEcho()
    {
        send ("{\"t\":\"gate\",\"gate\":\"" + jsonEscape (plugin.core().getGate().encode().raw()) + "\"}");
    }

    void WebView::noteParamFromHost (int index)
    {
        if (index < 0 || index >= paramCount())
            return;

        const std::lock_guard<std::mutex> lock (hostParamLock);
        for (const int dirty : hostParamDirty)
            if (dirty == index)
                return;

        hostParamDirty.push_back (index);
    }

    void WebView::askHostForFlush()
    {
        // While the host rolls, its next process() call takes the queue with it.
        // Standing still, it comes only if asked.
        if (plugin.transportIsRolling())
            return;

        const auto* params = plugin.hostParams();
        if (params != nullptr && params->request_flush != nullptr)
            params->request_flush (host);
    }

    void WebView::sendMeter()
    {
        auto& core = plugin.core();

        std::string json = "{\"t\":\"meter\"";
        json += ",\"peak\":" + numberToText (core.takeMeterPeak());
        json += ",\"rms\":" + numberToText (core.meterRms());
        json += ",\"gr\":" + numberToText (core.limiterGain());
        json += ",\"step\":" + std::to_string (core.playingStep());
        json += ",\"sector\":" + std::to_string (core.playingGateSector());
        json += ",\"running\":" + std::string (core.sequencerRunning() ? "true" : "false");
        // The clock's tempo: the host's in Sync = Host, the knob's in Manual.
        json += ",\"tempo\":" + numberToText (core.tempoBpm());

        std::vector<int> dirty;
        {
            const std::lock_guard<std::mutex> lock (hostParamLock);
            dirty.swap (hostParamDirty);
        }

        if (! dirty.empty())
        {
            // The host moved these while the page was not looking.
            json += ",\"params\":{";
            for (size_t i = 0; i < dirty.size(); ++i)
            {
                if (i > 0)
                    json += ',';
                json += '"';
                json += paramDef (dirty[i]).id;
                json += "\":";
                json += numberToText (static_cast<double> (core.paramValue (dirty[i])));
            }
            json += '}';
        }

        json += "}";

        send (json);
    }

    void WebView::sendPresetList()
    {
        // The page shows the bank menu with the credit column, like the native
        // editor, so the list carries both the name and its attribution.
        std::string json = "{\"t\":\"presets\",\"names\":[";
        const auto& presets = factoryPresets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (i > 0)
                json += ',';
            json += '"';
            json += jsonEscape (presets[i].name);
            json += '"';
        }
        json += "],\"credits\":[";
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (i > 0)
                json += ',';
            json += '"';
            json += jsonEscape (presets[i].credit);
            json += '"';
        }
        json += "]}";

        send (json);
    }

    //==============================================================================
    void WebView::setHoveredParam (const char* name)
    {
        if (hostParamHovered == nullptr || hostParamHovered->update == nullptr)
            return;

        // An empty name means "nothing is hovered"; the host then puts its own
        // menu away.
        uint32_t id = UINT32_MAX;   // CLAP_INVALID_ID
        if (name != nullptr && name[0] != 0)
        {
            const int index = paramIndexForId (name);
            if (index < 0)
                return;

            id = paramId (index);
        }

        hostParamHovered->update (host, id);
    }

    //==============================================================================
    void WebView::handleMessage (const std::string& message)
    {
        JsonValue root;
        std::string error;
        if (! parseJson (message, root, error) || ! root.isObject())
            return;

        const auto* type = root.member ("t");
        if (type == nullptr || ! type->isString())
            return;

        auto& core = plugin.core();

        if (type->text == "hello")
        {
            // The preset list first, so that the state is the last message: the
            // page draws the state as soon as it arrives.
            sendPresetList();
            sendState();
            return;
        }

        if (type->text == "meter")
        {
            sendMeter();
            return;
        }

        if (type->text == "param")
        {
            const auto* id = root.member ("id");
            const auto* value = root.member ("v");
            if (id == nullptr || ! id->isString() || value == nullptr || ! value->isNumber())
                return;

            const int index = paramIndexForId (id->text.c_str());
            if (index < 0)
                return;

            const auto& def = paramDef (index);
            double v = value->number;
            if (def.isChoice)
                v = std::floor (v + 0.5);
            else
                v = std::min (def.maxValue, std::max (def.minValue, v));

            core.setParam (index, static_cast<float> (v));

            // The host has to hear about it, or the DAW's automation and the
            // page's knob disagree and the change is lost with the session.
            plugin.queueParamFromPage (index, v);
            plugin.markHostDirty();
            askHostForFlush();

            sendParamEcho (index, v);
            return;
        }

        if (type->text == "gesture")
        {
            // The page's pointer went down or up on a control. The host wraps a
            // hand movement in a pair of gestures, so it knows where one edit
            // ends and the next begins.
            const auto* id = root.member ("id");
            const auto* begin = root.member ("begin");
            if (id == nullptr || ! id->isString() || begin == nullptr)
                return;

            const int index = paramIndexForId (id->text.c_str());
            if (index < 0)
                return;

            plugin.queueGestureFromPage (index, begin->isBool() ? begin->boolean : begin->number != 0.0);
            return;
        }

        if (type->text == "seq")
        {
            const auto* text = root.member ("text");
            if (text == nullptr || ! text->isString())
                return;

            core.setSequence (Sequence::decode (juce::String (text->text)));
            plugin.markHostDirty();
            sendSeqEcho();
            return;
        }

        if (type->text == "gate")
        {
            const auto* text = root.member ("text");
            if (text == nullptr || ! text->isString())
                return;

            core.setGate (GatePattern::decode (juce::String (text->text)));
            plugin.markHostDirty();
            sendGateEcho();
            return;
        }

        if (type->text == "preset")
        {
            const auto* name = root.member ("name");
            if (name == nullptr || ! name->isString())
                return;

            for (const auto& preset : factoryPresets())
            {
                if (preset.name == name->text)
                {
                    core.applyPreset (preset);
                    plugin.queueAllParamsFromPage();
                    plugin.markHostDirty();
                    askHostForFlush();
                    sendPresetEcho();
                    return;
                }
            }

            return;
        }

        if (type->text == "presetText")
        {
            // A user preset: the text comes from the page, which keeps it in
            // the browser's storage.
            const auto* json = root.member ("json");
            if (json == nullptr || ! json->isString())
                return;

            PresetDef preset;
            if (parsePresetText (json->text, preset, error))
            {
                core.applyPreset (preset);
                plugin.queueAllParamsFromPage();
                plugin.markHostDirty();
                askHostForFlush();
            }

            sendPresetEcho();
            return;
        }

        if (type->text == "hover")
        {
            // Which control the pointer is over: the host can then offer its own
            // menu and value entry on top of ours.
            const auto* id = root.member ("id");
            if (id != nullptr && id->isString())
                setHoveredParam (id->text.c_str());

            return;
        }

        if (type->text == "note")
        {
            const auto* text = root.member ("text");
            if (text != nullptr && text->isString())
                core.setPresetNote (text->text);

            return;
        }

        if (type->text == "render")
        {
            // Bounce the current pattern offline, the way the native export does:
            // a fresh core instance renders on the calling (message) thread, so it
            // never races the live audio path. The result goes back as base64 of
            // 16-bit mono PCM; the page wraps it in a WAV file and downloads it.
            ProcessorCore bounce;
            bounce.prepare (48000.0);
            for (int i = 0; i < paramCount(); ++i)
                bounce.setParam (i, core.uiParamValue (i));
            // Force a manual, running transport: with Sync=Host a bounce would
            // depend on who calls process, which is nobody here.
            bounce.setParam (paramIndexForId ("Sync"), 1.0f);
            bounce.setParam (paramIndexForId ("Play"), 1.0f);
            bounce.setParam (paramIndexForId ("SeqOn"), 1.0f);
            bounce.setSequence (core.getSequence());
            bounce.setGate (core.getGate());

            const double bpm = std::max (20.0, static_cast<double> (core.uiParamValue (kBpm)));
            const double seconds = std::min (10.0, 2.0 * 4.0 * 60.0 / bpm + 0.2);
            constexpr int blockSamples = 512;
            juce::AudioBuffer<float> buffer (2, blockSamples);
            std::vector<float> pcm;
            const size_t total = static_cast<size_t> (seconds * 48000.0);
            pcm.reserve (total);
            juce::MidiBuffer noNotes;
            float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
            while (pcm.size() < total)
            {
                bounce.process (channels, 2, blockSamples, noNotes);
                for (int i = 0; i < blockSamples; ++i)
                    pcm.push_back (0.5f * (channels[0][i] + channels[1][i]));
            }
            pcm.resize (total);

            std::string encoded;
            base64Encode (pcm, encoded);
            send (std::string ("{\"t\":\"audio\",\"rate\":48000,\"samples\":\"") + encoded + "\"}");
            return;
        }
    }
}
