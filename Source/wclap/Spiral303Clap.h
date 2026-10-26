#pragma once

#include <clap/clap.h>

#include <memory>

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

    private:
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

        clap_plugin_t plugin {};
        const clap_host_t* host = nullptr;
        ProcessorCore processor;
        std::unique_ptr<WebView> webViewOwner;

        static const clap_plugin_audio_ports_t kAudioPorts;
        static const clap_plugin_params_t kParams;
        static const clap_plugin_state_t kState;
        static const clap_plugin_webview_t kWebview;
        static const clap_plugin_gui_t kGui;
    };
}
