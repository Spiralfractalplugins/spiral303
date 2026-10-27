#pragma once

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <mutex>
#include <string>
#include <vector>

namespace spiral303
{
    /** The host side of the draft "clap.param-hovered/1" extension. It is newer
        than the CLAP headers we vendor, so it is declared here: the plugin
        tells the host which control the pointer is over, and the host can then
        offer its own menu and value entry on top of the plugin's interface. */
    struct HostParamHovered
    {
        void (CLAP_ABI* update) (const clap_host_t* host, uint32_t paramId);
    };

    class ClapPlugin;

    /** The interface of the WebAssembly build.
        A plugin inside a wasm sandbox cannot draw a native window, so the
        interface is a web page: the plugin serves it from its own memory and
        the two talk over a small message protocol. */
    class WebView
    {
    public:
        WebView (ClapPlugin& plugin, const clap_host_t* host);

        //==============================================================================
        // The clap.webview extension.

        static int32_t CLAP_ABI get_uri (const clap_plugin* plugin, char* uri, uint32_t capacity);
        static bool CLAP_ABI get_resource (const clap_plugin* plugin, const char* path, char* mime,
                                           uint32_t mime_capacity, const clap_ostream_t* stream);
        static bool CLAP_ABI receive (const clap_plugin* plugin, const void* buffer, uint32_t size);

        //==============================================================================
        // The clap.gui extension, with the webview window api.
        //
        // Every slot is filled in: a host that reads a function pointer without
        // checking it for null (openDAW does) would trap on an empty one, and
        // the error it reports is a bare address in the module's memory.

        static bool CLAP_ABI gui_is_api_supported (const clap_plugin* plugin, const char* api, bool isFloating);
        static bool CLAP_ABI gui_get_preferred_api (const clap_plugin* plugin, const char** api, bool* isFloating);
        static bool CLAP_ABI gui_create (const clap_plugin* plugin, const char* api, bool isFloating);
        static void CLAP_ABI gui_destroy (const clap_plugin* plugin);
        static bool CLAP_ABI gui_set_scale (const clap_plugin* plugin, double scale);
        static bool CLAP_ABI gui_get_size (const clap_plugin* plugin, uint32_t* width, uint32_t* height);
        static bool CLAP_ABI gui_can_resize (const clap_plugin* plugin);
        static bool CLAP_ABI gui_get_resize_hints (const clap_plugin* plugin, clap_gui_resize_hints_t* hints);
        static bool CLAP_ABI gui_adjust_size (const clap_plugin* plugin, uint32_t* width, uint32_t* height);
        static bool CLAP_ABI gui_set_size (const clap_plugin* plugin, uint32_t width, uint32_t height);
        static bool CLAP_ABI gui_set_parent (const clap_plugin* plugin, const clap_window_t* window);
        static bool CLAP_ABI gui_set_transient (const clap_plugin* plugin, const clap_window_t* window);
        static void CLAP_ABI gui_suggest_title (const clap_plugin* plugin, const char* title);
        static bool CLAP_ABI gui_show (const clap_plugin* plugin);
        static bool CLAP_ABI gui_hide (const clap_plugin* plugin);

        //==============================================================================
        // Talking to the page.
        //
        // The page is a guest on a slow link: every message is a function call
        // through the host and a JSON string, so what goes back is only what
        // moved. The ranges, the choices and the version never change and travel
        // with the full state, which the page asks for once, at hello.

        /** Sends the whole state: the parameters, the pattern, the gate, the
            preset, the response curve and the parts that never change. */
        void sendState();

        /** Everything a preset load moved, without the statics. */
        void sendPresetEcho();

        /** One parameter back to the page, with the curve when the filter moved. */
        void sendParamEcho (int index, double value);

        /** The pattern or the gate, and nothing else. */
        void sendSeqEcho();
        void sendGateEcho();

        /** Sends the meter readings, and with them any parameter the host moved. */
        void sendMeter();

        /** Sends the list of the factory presets, for the preset menu. */
        void sendPresetList();

        /** The host changed this parameter: the page's knob has to follow at the
            next meter tick. */
        void noteParamFromHost (int index);

        /** Tells the host which parameter the pointer is over, so that the host
            can offer its own menu and value entry on top of ours. */
        void setHoveredParam (const char* paramId);

    private:
        void send (const std::string& message);
        void handleMessage (const std::string& message);
        void sendStateBody (bool withStatics);
        std::string staticsBlock();
        std::string curveBlock();

        /** The host only comes for the queued changes when it processes, so if it
            stands still it has to be asked. */
        void askHostForFlush();

        ClapPlugin& plugin;
        const clap_host_t* host = nullptr;
        const clap_host_webview_t* hostWebview = nullptr;
        const HostParamHovered* hostParamHovered = nullptr;
        bool visible = false;
        bool created = false;
        std::string lastMessage;

        std::mutex hostParamLock;
        std::vector<int> hostParamDirty;
    };
}
