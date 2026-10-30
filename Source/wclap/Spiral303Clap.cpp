#include "Spiral303Clap.h"

#include <clap/fixedpoint.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace spiral303
{
    namespace
    {
        constexpr const char* kPluginId = "com.spiral303.plugin";
        constexpr const char* kPluginName = "Spiral303";
        constexpr const char* kVendor = "Spiralfractal";
        constexpr const char* kPluginVersion = SPIRAL303_VERSION;

        const char* const kFeatures[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                          CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                          "bass", "lead", "acid", "monophonic",
                                          nullptr };

        const clap_plugin_descriptor_t kDescriptor
        {
            CLAP_VERSION,
            kPluginId, kPluginName, kVendor,
            nullptr, nullptr, nullptr,
            kPluginVersion,
            "A Roland TB-303 in a box",
            kFeatures
        };

        ClapPlugin* self (const clap_plugin* plugin)
        {
            return static_cast<ClapPlugin*> (plugin->plugin_data);
        }
    }

    const clap_plugin_descriptor_t* ClapPlugin::descriptor() noexcept
    {
        return &kDescriptor;
    }

    const clap_plugin_audio_ports_t ClapPlugin::kAudioPorts
    {
        &ClapPlugin::ext_audio_ports_count,
        &ClapPlugin::ext_audio_ports_get
    };

    const clap_plugin_params_t ClapPlugin::kParams
    {
        &ClapPlugin::ext_params_count,
        &ClapPlugin::ext_params_info,
        &ClapPlugin::ext_params_value,
        &ClapPlugin::ext_params_value_to_text,
        &ClapPlugin::ext_params_text_to_value,
        &ClapPlugin::ext_params_flush
    };

    const clap_plugin_state_t ClapPlugin::kState
    {
        &ClapPlugin::ext_state_save,
        &ClapPlugin::ext_state_load
    };

    const clap_plugin_webview_t ClapPlugin::kWebview
    {
        &WebView::get_uri,
        &WebView::get_resource,
        &WebView::receive
    };

    const clap_plugin_gui_t ClapPlugin::kGui
    {
        &WebView::gui_is_api_supported,
        &WebView::gui_get_preferred_api,
        &WebView::gui_create,
        &WebView::gui_destroy,
        &WebView::gui_set_scale,
        &WebView::gui_get_size,
        &WebView::gui_can_resize,
        &WebView::gui_get_resize_hints,
        &WebView::gui_adjust_size,
        &WebView::gui_set_size,
        &WebView::gui_set_parent,
        &WebView::gui_set_transient,
        &WebView::gui_suggest_title,
        &WebView::gui_show,
        &WebView::gui_hide
    };

    //==============================================================================
    ClapPlugin::ClapPlugin (const clap_host_t* hostIn) : host (hostIn)
    {
        webViewOwner = std::make_unique<WebView> (*this, host);

        plugin.desc = &kDescriptor;
        plugin.plugin_data = this;
        plugin.init = [] (const clap_plugin* p) { return self (p)->init(); };
        plugin.destroy = [] (const clap_plugin* p) { self (p)->destroy(); };
        plugin.activate = [] (const clap_plugin* p, double sr, uint32_t minFrames, uint32_t maxFrames)
        {
            return self (p)->activate (sr, minFrames, maxFrames);
        };
        plugin.deactivate = [] (const clap_plugin* p) { self (p)->deactivate(); };
        plugin.start_processing = [] (const clap_plugin* p) { return self (p)->startProcessing(); };
        plugin.stop_processing = [] (const clap_plugin* p) { self (p)->stopProcessing(); };
        plugin.reset = [] (const clap_plugin* p) { self (p)->reset(); };
        plugin.process = [] (const clap_plugin* p, const clap_process_t* proc)
        {
            return self (p)->process (proc);
        };
        plugin.get_extension = [] (const clap_plugin* p, const char* id) { return self (p)->getExtension (id); };
        plugin.on_main_thread = [] (const clap_plugin* p) { self (p)->onMainThread(); };
    }

    ClapPlugin::~ClapPlugin() = default;

    //==============================================================================
    bool ClapPlugin::init()
    {
        if (host != nullptr && host->get_extension != nullptr)
        {
            hostParams_ = static_cast<const clap_host_params_t*> (host->get_extension (host, CLAP_EXT_PARAMS));
            hostState_ = static_cast<const clap_host_state_t*> (host->get_extension (host, CLAP_EXT_STATE));
        }

        processor.prepare (48000.0);
        return true;
    }

    void ClapPlugin::destroy()
    {
        delete this;
    }

    bool ClapPlugin::activate (double sampleRate, uint32_t, uint32_t)
    {
        processor.prepare (sampleRate);
        processor.reset();
        return true;
    }

    void ClapPlugin::deactivate()
    {
    }

    bool ClapPlugin::startProcessing()
    {
        return true;
    }

    void ClapPlugin::stopProcessing()
    {
        transportRolling.store (false, std::memory_order_relaxed);
    }

    void ClapPlugin::reset()
    {
        processor.reset();
    }

    void ClapPlugin::onMainThread()
    {
    }

    //==============================================================================
    // The page's changes, on their way to the host.
    void ClapPlugin::pushOutEventLocked (uint16_t type, clap_id paramId, double value)
    {
        // A host that stopped taking events must not fill the queue forever: the
        // page's values converge, so dropping the oldest is enough.
        if (outWrite - outRead == static_cast<uint32_t> (kOutEventCapacity))
            ++outRead;

        outEvents[outWrite % static_cast<uint32_t> (kOutEventCapacity)] = OutEvent { type, paramId, value };
        ++outWrite;
    }

    void ClapPlugin::queueGestureFromPage (int index, bool begin)
    {
        if (index < 0 || index >= paramCount())
            return;

        const clap_id id = paramId (index);
        const std::lock_guard<std::mutex> lock (outLock);

        if (begin)
        {
            if (gestureOpen[index])
                return;
            gestureOpen[index] = true;
            pushOutEventLocked (CLAP_EVENT_PARAM_GESTURE_BEGIN, id, 0.0);
        }
        else
        {
            if (! gestureOpen[index])
                return;
            gestureOpen[index] = false;
            pushOutEventLocked (CLAP_EVENT_PARAM_GESTURE_END, id, 0.0);
        }
    }

    void ClapPlugin::queueParamFromPage (int index, double value)
    {
        if (index < 0 || index >= paramCount())
            return;

        const clap_id id = paramId (index);
        const std::lock_guard<std::mutex> lock (outLock);

        // A change the page did not wrap in a gesture is a single move: give the
        // host the pair itself, so a gesture never stays open by accident.
        const bool opensHere = ! gestureOpen[index];
        if (opensHere)
        {
            gestureOpen[index] = true;
            pushOutEventLocked (CLAP_EVENT_PARAM_GESTURE_BEGIN, id, 0.0);
        }

        pushOutEventLocked (CLAP_EVENT_PARAM_VALUE, id, value);

        if (opensHere)
        {
            gestureOpen[index] = false;
            pushOutEventLocked (CLAP_EVENT_PARAM_GESTURE_END, id, 0.0);
        }
    }

    void ClapPlugin::queueAllParamsFromPage()
    {
        // Loading a preset is not a hand movement, so no gestures: just the values.
        const std::lock_guard<std::mutex> lock (outLock);
        for (int i = 0; i < paramCount(); ++i)
        {
            if (gestureOpen[i])
            {
                gestureOpen[i] = false;
                pushOutEventLocked (CLAP_EVENT_PARAM_GESTURE_END, paramId (i), 0.0);
            }
            pushOutEventLocked (CLAP_EVENT_PARAM_VALUE, paramId (i),
                                static_cast<double> (processor.uiParamValue (i)));
        }
    }

    void ClapPlugin::drainOutEvents (const clap_output_events_t* out)
    {
        if (out == nullptr || out->try_push == nullptr)
            return;

        for (;;)
        {
            OutEvent ev {};
            {
                const std::lock_guard<std::mutex> lock (outLock);
                if (outRead == outWrite)
                    return;
                ev = outEvents[outRead % static_cast<uint32_t> (kOutEventCapacity)];
                ++outRead;
            }

            clap_event_param_value_t value {};
            clap_event_param_gesture_t gesture {};
            clap_event_header_t* header = nullptr;

            if (ev.type == CLAP_EVENT_PARAM_VALUE)
            {
                value.header.size = sizeof (value);
                value.header.time = 0;
                value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                value.header.type = CLAP_EVENT_PARAM_VALUE;
                value.param_id = ev.paramId;
                value.note_id = -1;
                value.port_index = -1;
                value.channel = -1;
                value.key = -1;
                value.value = ev.value;
                header = &value.header;
            }
            else
            {
                gesture.header.size = sizeof (gesture);
                gesture.header.time = 0;
                gesture.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                gesture.header.type = ev.type;
                gesture.param_id = ev.paramId;
                header = &gesture.header;
            }

            if (! out->try_push (out, header))
            {
                // The host's queue is full: keep this one for the next drain.
                const std::lock_guard<std::mutex> lock (outLock);
                --outRead;
                return;
            }
        }
    }

    void ClapPlugin::readTransport (const clap_event_transport_t* transport, HostTransport& out)
    {
        out = HostTransport();
        if (transport == nullptr)
            return;

        const uint32_t flags = transport->flags;
        out.playing = (flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        if ((flags & CLAP_TRANSPORT_HAS_TEMPO) != 0)
            out.bpm = transport->tempo;
        if ((flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0)
        {
            out.beats = static_cast<double> (transport->song_pos_beats) / CLAP_BEATTIME_FACTOR;
            out.hasBeats = true;
        }
    }

    //==============================================================================
    const void* ClapPlugin::getExtension (const char* id)
    {
        if (id == nullptr)
            return nullptr;

        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)
            return &kAudioPorts;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)
            return &kParams;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)
            return &kState;
        if (std::strcmp (id, CLAP_EXT_WEBVIEW) == 0)
            return &kWebview;
        if (std::strcmp (id, CLAP_EXT_GUI) == 0)
            return &kGui;

        return nullptr;
    }

    //==============================================================================
    uint32_t CLAP_ABI ClapPlugin::ext_audio_ports_count (const clap_plugin*, bool isInput)
    {
        // One note input, one stereo output.
        return isInput ? 1u : 1u;
    }

    bool CLAP_ABI ClapPlugin::ext_audio_ports_get (const clap_plugin*, uint32_t index, bool isInput,
                                                   clap_audio_port_info_t* info)
    {
        if (index != 0 || info == nullptr)
            return false;

        std::memset (info, 0, sizeof (*info));
        info->id = isInput ? 1u : 2u;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = isInput ? 1u : 2u;
        info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_STEREO;

        if (isInput)
            std::snprintf (info->name, CLAP_NAME_SIZE, "Notes In");
        else
            std::snprintf (info->name, CLAP_NAME_SIZE, "Out");

        return true;
    }

    //==============================================================================
    uint32_t CLAP_ABI ClapPlugin::ext_params_count (const clap_plugin*)
    {
        return static_cast<uint32_t> (paramCount());
    }

    bool CLAP_ABI ClapPlugin::ext_params_info (const clap_plugin*, uint32_t index, clap_param_info_t* info)
    {
        if (info == nullptr || index >= static_cast<uint32_t> (paramCount()))
            return false;

        const auto& def = paramDef (static_cast<int> (index));

        std::memset (info, 0, sizeof (*info));
        info->id = paramId (static_cast<int> (index));
        info->cookie = nullptr;
        std::snprintf (info->name, CLAP_NAME_SIZE, "%s", def.name);
        info->min_value = def.minValue;
        info->max_value = def.maxValue;
        info->default_value = def.defaultValue;

        if (def.isChoice)
            info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
        else
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;

        return true;
    }

    bool CLAP_ABI ClapPlugin::ext_params_value (const clap_plugin* plugin, clap_id id, double* value)
    {
        if (value == nullptr)
            return false;

        const int index = paramIndexForHostId (id);
        if (index < 0)
            return false;

        *value = static_cast<double> (self (plugin)->processor.paramValue (index));
        return true;
    }

    bool CLAP_ABI ClapPlugin::ext_params_value_to_text (const clap_plugin* plugin, clap_id id, double value,
                                                       char* buffer, uint32_t capacity)
    {
        if (buffer == nullptr || capacity == 0)
            return false;

        const int index = paramIndexForHostId (id);
        if (index < 0)
            return false;

        const auto& def = paramDef (index);

        if (def.isChoice)
        {
            const int choice = static_cast<int> (juce::jlimit (0.0, static_cast<double> (def.numChoices - 1), value));
            std::snprintf (buffer, capacity, "%s", def.choices[choice]);
            return true;
        }

        char number[64];
        std::snprintf (number, sizeof (number), "%.*f", def.decimals, value);

        if (def.unit != nullptr && def.unit[0] != 0)
            std::snprintf (buffer, capacity, "%s%s", number, def.unit);
        else
            std::snprintf (buffer, capacity, "%s", number);

        (void) plugin;
        return true;
    }

    bool CLAP_ABI ClapPlugin::ext_params_text_to_value (const clap_plugin* plugin, clap_id id,
                                                       const char* text, double* value)
    {
        if (text == nullptr || value == nullptr)
            return false;

        const int index = paramIndexForHostId (id);
        if (index < 0)
            return false;

        const auto& def = paramDef (index);

        if (def.isChoice)
        {
            for (int i = 0; i < def.numChoices; ++i)
                if (std::strcmp (def.choices[i], text) == 0)
                {
                    *value = static_cast<double> (i);
                    return true;
                }

            return false;
        }

        *value = std::atof (text);
        (void) plugin;
        return true;
    }

    void CLAP_ABI ClapPlugin::ext_params_flush (const clap_plugin* plugin, const clap_input_events_t* in,
                                                const clap_output_events_t* out)
    {
        auto* me = self (plugin);
        if (me == nullptr)
            return;

        if (in != nullptr && in->size != nullptr && in->get != nullptr)
        {
            const uint32_t count = in->size (in);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto* header = in->get (in, i);
                if (header == nullptr || header->type != CLAP_EVENT_PARAM_VALUE)
                    continue;

                const auto* event = reinterpret_cast<const clap_event_param_value_t*> (header);
                me->applyParamEvent (event);
            }
        }

        // The host came for the changes the page made, so it is the moment to hand
        // them over.
        me->drainOutEvents (out);
    }

    void ClapPlugin::applyParamEvent (const clap_event_param_value_t* event)
    {
        const int index = paramIndexForHostId (event->param_id);
        if (index < 0)
            return;

        const auto& def = paramDef (index);
        const double value = def.isChoice
                                 ? std::floor (event->value + 0.5)
                                 : juce::jlimit (def.minValue, def.maxValue, event->value);

        processor.setParamImmediate (index, static_cast<float> (value));

        // The host moved something: the page's knob has to follow.
        if (auto* web = webViewPtr())
            web->noteParamFromHost (index);
    }

    //==============================================================================
    bool CLAP_ABI ClapPlugin::ext_state_save (const clap_plugin* plugin, const clap_ostream_t* stream)
    {
        if (stream == nullptr || stream->write == nullptr)
            return false;

        const PluginState state = self (plugin)->processor.getState();
        const auto bytes = encodeState (state);

        return stream->write (stream, bytes.data(), bytes.size()) == bytes.size();
    }

    bool CLAP_ABI ClapPlugin::ext_state_load (const clap_plugin* plugin, const clap_istream_t* stream)
    {
        if (stream == nullptr || stream->read == nullptr)
            return false;

        std::vector<uint8_t> bytes;
        uint8_t chunk[4096];
        for (;;)
        {
            const int64_t read = stream->read (stream, chunk, sizeof (chunk));
            if (read <= 0)
                break;

            bytes.insert (bytes.end(), chunk, chunk + read);
            if (bytes.size() > 4u * 1024u * 1024u)
                break;
        }

        if (bytes.empty())
            return false;

        return self (plugin)->processor.setState (bytes.data(), bytes.size());
    }

    //==============================================================================
    clap_process_status ClapPlugin::process (const clap_process_t* proc)
    {
        if (proc == nullptr || proc->audio_outputs_count == 0)
            return CLAP_PROCESS_ERROR;

        // The events: notes, controllers, parameter changes and the transport.
        juce::MidiBuffer midi;

        HostTransport transport;
        readTransport (proc->transport, transport);

        const auto* events = proc->in_events;
        if (events != nullptr)
        {
            const uint32_t count = events->size (events);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto* header = events->get (events, i);
                if (header == nullptr)
                    continue;

                switch (header->type)
                {
                    case CLAP_EVENT_NOTE_ON:
                    {
                        const auto* note = reinterpret_cast<const clap_event_note_t*> (header);
                        midi.addEvent (juce::MidiMessage::noteOn (note->channel + 1, note->key,
                                                                   static_cast<float> (note->velocity)),
                                       static_cast<int> (note->header.time));
                        break;
                    }

                    case CLAP_EVENT_NOTE_OFF:
                    {
                        const auto* note = reinterpret_cast<const clap_event_note_t*> (header);
                        midi.addEvent (juce::MidiMessage::noteOff (note->channel + 1, note->key),
                                       static_cast<int> (note->header.time));
                        break;
                    }

                    case CLAP_EVENT_PARAM_VALUE:
                        applyParamEvent (reinterpret_cast<const clap_event_param_value_t*> (header));
                        break;

                    case CLAP_EVENT_PARAM_GESTURE_BEGIN:
                    case CLAP_EVENT_PARAM_GESTURE_END:
                        // The host starts and stops an automation pass; the value
                        // events in between speak for themselves.
                        break;

                    case CLAP_EVENT_TRANSPORT:
                        readTransport (reinterpret_cast<const clap_event_transport_t*> (header), transport);
                        break;

                    case CLAP_EVENT_MIDI:
                    {
                        // Raw MIDI: the controllers drive the MIDI map.
                        const auto* raw = reinterpret_cast<const clap_event_midi_t*> (header);
                        const uint8_t status = raw->data[0] & 0xf0u;
                        const int channel = (raw->data[0] & 0x0fu) + 1;

                        if (status == 0xb0)
                            midi.addEvent (juce::MidiMessage::controllerEvent (channel, raw->data[1],
                                                                             raw->data[2]),
                                           static_cast<int> (raw->header.time));
                        else if (status == 0x90 && raw->data[2] > 0)
                            midi.addEvent (juce::MidiMessage::noteOn (channel, raw->data[1],
                                                                       raw->data[2] / 127.0f),
                                           static_cast<int> (raw->header.time));
                        else if (status == 0x80 || (status == 0x90 && raw->data[2] == 0))
                            midi.addEvent (juce::MidiMessage::noteOff (channel, raw->data[1]),
                                           static_cast<int> (raw->header.time));
                        else if (status == 0xf0 && raw->data[0] >= 0xf8u)
                            // System realtime: the clock's Start/Continue/Stop and
                            // the 24-PPQ ticks, for Sync = Midi Clock.
                            midi.addEvent (juce::MidiMessage::realtimeStatus (raw->data[0]),
                                           static_cast<int> (raw->header.time));
                        break;
                    }

                    default:
                        break;
                }
            }
        }

        // The output buffer: the first port, in the host's layout.
        auto& output = proc->audio_outputs[0];
        const int numChannels = output.channel_count > 0 ? static_cast<int> (output.channel_count) : 2;
        const int numSamples = static_cast<int> (proc->frames_count);

        float* channels[2] { nullptr, nullptr };
        for (int i = 0; i < numChannels && i < 2; ++i)
            channels[i] = output.data32[i];

        transportRolling.store (transport.playing, std::memory_order_relaxed);

        processor.process (channels, numChannels, numSamples, midi, &transport);

        // Whatever the page changed since the last block goes to the host here.
        drainOutEvents (proc->out_events);

        return CLAP_PROCESS_CONTINUE;
    }
}
