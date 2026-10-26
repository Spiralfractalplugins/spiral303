#!/usr/bin/env python3
"""Packs a Spiral303 WCLAP bundle directory into the .wclap.tar.gz hosts import.

The archive holds one top directory named like the bundle (Spiral303.wclap/module.wasm,
Spiral303.wclap/index.html -- the page), plain ustar entries only: no pax headers, no
directory entries, no macOS ._ metadata files. A system `tar czf` on macOS writes
exactly those, and they break the hosts: openDAW drops the top directory only when
it is the single one and every path contains a "/" -- the AppleDouble entry at the
root has no "/", so the paths keep the prefix and the host cannot find module.wasm.

Paths are sorted, mtime/uid/gid are zero, gzip has no file name and mtime 0, so the
same bundle always packs into the same bytes and hosts that address bundles by their
hash see the same plugin.

--flat drops the top directory (module.wasm, index.html at the root). openDAW wants
the prefix and strips it; the plinken.org browser-test-host does not -- its loader
looks up "<pluginPath>/module.wasm" and never finds it under a root folder, so the
registry artifact has to be flat.

Usage: tools/pack_wclap.py <bundle dir> [output] [--flat]
       (default output: <bundle dir>.tar.gz)
"""

import gzip
import io
import os
import sys
import tarfile


def pack(bundle, output, flat=False):
    root = "" if flat else os.path.basename(os.path.normpath(bundle)) + "/"
    paths = []
    for directory, dirs, files in os.walk(bundle):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            if not name.startswith("."):
                paths.append(os.path.join(directory, name))
    if "module.wasm" not in sorted(os.path.basename(p) for p in paths):
        raise SystemExit(f"{bundle}: no module.wasm")

    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for path in paths:
            info = tarfile.TarInfo(root + os.path.relpath(path, bundle).replace(os.sep, "/"))
            info.size = os.path.getsize(path)
            info.mode = 0o644
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with open(path, "rb") as f:
                tar.addfile(info, f)

    with open(output, "wb") as out, gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as gz:
        gz.write(buffer.getvalue())
    print(f"{output}: {len(paths)} files, {os.path.getsize(output) / 1048576:.2f} MB")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--flat"]
    flat = "--flat" in sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    bundle = args[0]
    pack(bundle, args[1] if len(args) > 1 else os.path.normpath(bundle) + ".tar.gz", flat)
