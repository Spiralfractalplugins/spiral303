#pragma once

// The WebAssembly sources are compiled against the JUCE-free core. JUCE itself
// does not build for wasm, and the stand-in in Source/core is what the audio
// path is written against there.

#if ! defined(SPIRAL303_CORE_ONLY)
 #error "The WebAssembly sources need the JUCE-free core: build them with SPIRAL303_CORE_ONLY."
#endif

#include "../core/CoreInclude.h"
