// Host half for the WCLAP smoke test.
//
// A WebAssembly table can only hold WebAssembly functions, so a host that wants
// to hand callbacks to the plugin (an event list, a get_extension hook) must
// bring its own tiny wasm module: the harness copies the exports below into the
// plugin's exported function table and the plugin calls them through
// call_indirect, exactly like the real hosts (wclap-bridge, clap-audionode) do.
//
// This module never touches the plugin's memory: everything it has to hand back
// is a pointer the harness already gave it, so the two modules can stay
// independent.

#include <cstdint>
#include <cstring>

extern "C"
{
    // --- event list ---------------------------------------------------------
    // The harness writes the event structs into the plugin's memory and tells us
    // where they are; size()/get() then hand them to the plugin.

    static uint32_t g_event_count = 0;
    static uint32_t g_event_ptr = 0;
    static uint32_t g_event_stride = 36;

    void host_events_begin (uint32_t eventPtr, uint32_t count, uint32_t stride)
    {
        g_event_ptr = eventPtr;
        g_event_count = count;
        g_event_stride = stride;
    }

    uint32_t host_event_size (uint32_t)
    {
        return g_event_count;
    }

    uint32_t host_event_get (uint32_t, uint32_t index)
    {
        if (index >= g_event_count)
            return 0;
        return g_event_ptr + index * g_event_stride;
    }

    // --- host extensions ----------------------------------------------------
    // The plugin asks for an extension by id (a pointer to a string in its own
    // memory). We cannot read that string, so the harness registers the
    // pointers it found and we match on the pointer value.

    enum { kKindNone = 0, kKindParams = 1, kKindWebview = 2, kKindHovered = 3 };

    struct IdEntry
    {
        uint32_t idPtr = 0;
        uint32_t kind = kKindNone;
    };

    static constexpr int kMaxIds = 8;
    static IdEntry g_ids[kMaxIds];

    static uint32_t g_params_struct = 0;
    static uint32_t g_webview_struct = 0;
    static uint32_t g_hovered_struct = 0;

    // The last message the plugin sent to the webview, for the test to read.
    static uint32_t g_last_message_ptr = 0;
    static uint32_t g_last_message_size = 0;

    void host_register_id (uint32_t idPtr, uint32_t kind)
    {
        for (auto& entry : g_ids)
        {
            if (entry.kind == kKindNone)
            {
                entry.idPtr = idPtr;
                entry.kind = kind;
                return;
            }
        }
    }

    void host_set_extension_struct (uint32_t kind, uint32_t structPtr)
    {
        if (kind == kKindParams)
            g_params_struct = structPtr;
        else if (kind == kKindWebview)
            g_webview_struct = structPtr;
        else if (kind == kKindHovered)
            g_hovered_struct = structPtr;
    }

    uint32_t host_get_extension (uint32_t, uint32_t idPtr)
    {
        for (const auto& entry : g_ids)
        {
            if (entry.kind == kKindNone || entry.idPtr != idPtr)
                continue;

            switch (entry.kind)
            {
                case kKindParams:  return g_params_struct;
                case kKindWebview: return g_webview_struct;
                case kKindHovered: return g_hovered_struct;
                default: break;
            }
        }
        return 0;
    }

    // The extension callbacks themselves. They live in the plugin's table, so
    // the plugin can call them through the pointers in the struct.

    void host_params_rescan (uint32_t, uint32_t)
    {
    }

    void host_params_clear (uint32_t, uint32_t, uint32_t)
    {
    }

    void host_params_request_flush (uint32_t)
    {
    }

    uint32_t host_webview_send (uint32_t, uint32_t buffer, uint32_t size)
    {
        g_last_message_ptr = buffer;
        g_last_message_size = size;
        return 1;
    }

    uint32_t host_last_message_ptr()
    {
        return g_last_message_ptr;
    }

    uint32_t host_last_message_size()
    {
        return g_last_message_size;
    }

    // --- a stream for the resources ------------------------------------------
    // The plugin hands the interface files over through a clap_ostream. The
    // write callback only has to remember where the data went: the harness
    // reads it out of the plugin's memory.

    static uint32_t g_write_ptr = 0;
    static uint32_t g_write_size = 0;

    uint64_t host_stream_write (uint32_t, uint32_t buffer, uint64_t size)
    {
        g_write_ptr = buffer;
        g_write_size = static_cast<uint32_t> (size);
        return size;
    }

    uint32_t host_last_write_ptr()
    {
        return g_write_ptr;
    }

    uint32_t host_last_write_size()
    {
        return g_write_size;
    }

    // --- the hovered parameter ----------------------------------------------
    // The plugin tells the host which control the pointer is over, so that the
    // host can offer its own menu on top of the plugin's interface.

    static uint32_t g_hovered_param = 0xffffffffu;

    void host_param_hovered_update (uint32_t, uint32_t paramId)
    {
        g_hovered_param = paramId;
    }

    uint32_t host_last_hovered_param()
    {
        return g_hovered_param;
    }
}
