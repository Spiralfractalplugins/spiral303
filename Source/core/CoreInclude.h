#pragma once

// The one place that decides where the DSP core gets its JUCE-like types from.
//
// The native build uses the real JUCE; the WebAssembly build uses the small
// stand-in in JuceLite.h, because JUCE itself cannot be compiled for wasm.
// Both paths must behave identically - the core is bit-exact sensitive - and
// tests/TestCoreParity.cpp renders the same pattern through both and compares
// the samples.

#if defined(SPIRAL303_CORE_ONLY)
 #include "JuceLite.h"

 // JUCE's generated JuceHeader.h ends with "using namespace juce;", and the core
 // relies on that: a few call sites use jlimit/jmax unqualified. The stand-in
 // has to behave the same way or those files stop compiling.
 using namespace juce;
#else
 #include <JuceHeader.h>
#endif
