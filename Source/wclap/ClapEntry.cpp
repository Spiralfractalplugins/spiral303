// The CLAP entry point of the WebAssembly build.
//
// A WCLAP is a CLAP plugin compiled to WebAssembly: the module exports
// `clap_entry` (an i32 global holding the address of the clap_plugin_entry),
// a growable function table, the memory and `malloc`. The host reads the entry
// out of the memory, calls init(), asks for the factory and creates the plugin.

#include <clap/clap.h>

#include <cstring>

#include "Spiral303Clap.h"

namespace
{
    constexpr const char* kPluginId = "com.spiral303.plugin";

    uint32_t CLAP_ABI factory_count (const clap_plugin_factory*)
    {
        return 1;
    }

    const clap_plugin_descriptor_t* CLAP_ABI factory_descriptor (const clap_plugin_factory*, uint32_t index)
    {
        return index == 0 ? spiral303::ClapPlugin::descriptor() : nullptr;
    }

    const clap_plugin_t* CLAP_ABI factory_create (const clap_plugin_factory*,
                                                 const clap_host_t* host,
                                                 const char* id)
    {
        if (id != nullptr && std::strcmp (id, kPluginId) != 0)
            return nullptr;

        auto* plugin = new spiral303::ClapPlugin (host);
        return plugin->clapPlugin();
    }

    const clap_plugin_factory_t kFactory { factory_count, factory_descriptor, factory_create };

    bool CLAP_ABI entry_init (const char*)
    {
        return true;
    }

    void CLAP_ABI entry_deinit()
    {
    }
}

extern "C"
{
    CLAP_EXPORT const clap_plugin_entry_t clap_entry
    {
        CLAP_VERSION,
        entry_init,
        entry_deinit,
        [] (const char*) -> const void* { return &kFactory; }
    };
}
