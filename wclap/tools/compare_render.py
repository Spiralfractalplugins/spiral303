#!/usr/bin/env python3
"""Compares two raw float renders: the native build and the WebAssembly one.

    python3 compare_render.py <native.f32> <wasm.f32> [tolerance]

The files are stereo, interleaved, 32 bit floats. The two renders do not have
to be identical: the two builds use different maths libraries, so a sine or an
exponential can differ in the last bit. What matters is that the difference
stays far below anything audible.
"""

import math
import struct
import sys


def read(path):
    with open(path, "rb") as handle:
        data = handle.read()
    return struct.unpack("<%df" % (len(data) // 4), data)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2

    native = read(sys.argv[1])
    wasm = read(sys.argv[2])
    tolerance = float(sys.argv[3]) if len(sys.argv) > 3 else 1e-3

    if len(native) != len(wasm):
        print(f"FAIL: the renders have different lengths ({len(native)} and {len(wasm)})")
        return 1

    peak_native = peak_wasm = 0.0
    worst = 0.0
    worst_at = -1
    sum_squares = 0.0

    for index, (a, b) in enumerate(zip(native, wasm)):
        peak_native = max(peak_native, abs(a))
        peak_wasm = max(peak_wasm, abs(b))
        difference = abs(a - b)
        if difference > worst:
            worst = difference
            worst_at = index
        sum_squares += difference * difference

    rms = math.sqrt(sum_squares / len(native)) if native else 0.0

    print(f"samples:        {len(native)}")
    print(f"native peak:    {peak_native:.6f}")
    print(f"wasm peak:      {peak_wasm:.6f}")
    print(f"max difference: {worst:.3e} (sample {worst_at})")
    print(f"rms difference: {rms:.3e}")

    if worst > tolerance:
        print(f"FAIL: the difference is larger than the tolerance {tolerance:.1e}")
        return 1

    if peak_native < 0.01:
        print("FAIL: the native render is silent, so the comparison means nothing")
        return 1

    print(f"OK: the WebAssembly build sounds like the native one (tolerance {tolerance:.1e})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
