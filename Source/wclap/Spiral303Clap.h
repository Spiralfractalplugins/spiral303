#pragma once

#include <clap/clap.h>

#include <atomic>
#include <memory>
#include <mutex>

#include "ProcessorCore.h"
#include "WebView.h"

namespace spiral303
{
    /** The CLAP face of Spiral303: the same audio core the native build uses,
        behind the CLAP API instead of JUCE's. */
    class ClapPlugin
    {
    public:
        explicit ClapPlugin (const clap_host_t* host);
        ~ClapPlugin();

        /** The one descriptor of this plugin: the factory and the plugin share it. */
        static const clap_plugin_descriptor_t* descriptor() noexcept;

        const clap_plugin_t* clapPlugin() const noexcept { return &plugin; }

        bool init();
        void destroy();
        bool activate (double sampleRate, uint32_t minFrames, uint32_t maxFrames);
        void deactivate();
        bool startProcessing();
        void stopProcessing();
        void reset();
        clap_process_status process (const clap_process_t* process);
        const void* getExtension (const char* id);
        void onMainThread();

        ProcessorCore& core() noexcept { return processor; }
        WebView* webViewPtr() noexcept { return webViewOwner.get(); }

        /** The host's params and state interfaces, taken once at init. */
        const clap_host_params_t* hostParams() const noexcept { return hostParams_; }
        const clap_host_state_t* hostState() const noexcept { return hostState_; }

        //==============================================================================
        // The page's changes, on their way to the host.
        //
        // A host reads parameter changes from the events the plugin pushes in
        // process() or flush(), so a knob turned on the page waits in this queue
        // until the host comes for it.

        void queueParamFromPage (int index, double value);
        void queueGestureFromPage (int index, bool begin);
        void queueAllParamsFromPage();

        /** False when the host is not rolling: then it is not inside process()
            either, and has to be asked to come and take the queue. */
        bool transportIsRolling() const { return transportRolling.load (std::memory_order_relaxed); }

        /** The page changed something the host has to save again. */
        void markHostDirty()
        {
            if (hostState_ != nullptr && hostState_->mark_dirty != nullptr)
                hostState_->mark_dirty (host);
        }

    private:
        struct OutEvent
        {
            uint16_t type;      // PARAM_VALUE, or a GESTURE_BEGIN / GESTURE_END
            clap_id paramId;
            double value;
        };

        static const clap_plugin_t* create (const clap_plugin_descriptor_t* descriptor, const char* id);
        static uint32_t CLAP_ABI ext_audio_ports_count (const clap_plugin* plugin, bool isInput);
        static bool CLAP_ABI ext_audio_ports_get (const clap_plugin* plugin, uint32_t index, bool isInput,
                                                  clap_audio_port_info_t* info);
        static uint32_t CLAP_ABI ext_params_count (const clap_plugin* plugin);
        static bool CLAP_ABI ext_params_info (const clap_plugin* plugin, uint32_t index,
                                              clap_param_info_t* info);
        static bool CLAP_ABI ext_params_value (const clap_plugin* plugin, clap_id id, double* value);
        static bool CLAP_ABI ext_params_value_to_text (const clap_plugin* plugin, clap_id id, double value,
                                                      char* buffer, uint32_t capacity);
        static bool CLAP_ABI ext_params_text_to_value (const clap_plugin* plugin, clap_id id,
                                                      const char* text, double* value);
        static void CLAP_ABI ext_params_flush (const clap_plugin* plugin, const clap_input_events_t* in,
                                               const clap_output_events_t* out);
        static bool CLAP_ABI ext_state_save (const clap_plugin* plugin, const clap_ostream_t* stream);
        static bool CLAP_ABI ext_state_load (const clap_plugin* plugin, const clap_istream_t* stream);

        void applyParamEvent (const clap_event_param_value_t* event);

        /** The host's transport, read out of the process call or a transport event. */
        static void readTransport (const clap_event_transport_t* transport, HostTransport& out);

        void pushOutEventLocked (uint16_t type, clap_id paramId, double value);
        void drainOutEvents (const clap_output_events_t* out);

        clap_plugin_t plugin {};
        const clap_host_t* host = nullptr;
        const clap_host_params_t* hostParams_ = nullptr;
        const clap_host_state_t* hostState_ = nullptr;
        ProcessorCore processor;
        std::unique_ptr<WebView> webViewOwner;

        // The page's parameter changes, waiting for the host to come and take
        // them. A ring, because the page writes while the audio thread reads.
        static constexpr int kOutEventCapacity = 256;
        OutEvent outEvents[kOutEventCapacity] {};
        uint32_t outRead = 0;
        uint32_t outWrite = 0;
        bool gestureOpen[kParamCount] {};
        std::mutex outLock;
        std::atomic<bool> transportRolling { false };

        static const clap_plugin_audio_ports_t kAudioPorts;
        static const clap_plugin_params_t kParams;
        static const clap_plugin_state_t kState;
        static const clap_plugin_webview_t kWebview;
        static const clap_plugin_gui_t kGui;
    };
}
