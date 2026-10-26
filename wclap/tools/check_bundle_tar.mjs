// Checks the packed bundle archive the way openDAW parses it.
//
//   node wclap/tools/check_bundle_tar.mjs build-wclap/Spiral303.wclap <name>.tar.gz
//
// tools/check_wclap.sh runs it after the packing. The reader here is a plain
// javascript copy of WclapBundles.untar/stripRoot (packages/studio/core/src/wclap
// in openDAW), so a change that breaks the import there fails here first. It
// rejects everything the importer chokes on: macOS AppleDouble ._ files (a
// system `tar czf` on macOS writes them, and one of them at the root makes
// openDAW keep the "Spiral303.wclap/" prefix, so it cannot find module.wasm),
// pax headers, directory entries, entries without a "/". The last check packs
// the same bundle a second time and compares the bytes -- hosts address
// bundles by their hash, so a rebuild has to give the same file.

import {readFileSync, statSync, rmSync} from "node:fs"
import {execFileSync} from "node:child_process"
import {basename, dirname, resolve} from "node:path"

const errors = []
const fail = message => errors.push(message)
const BLOCK = 512

// -- the same unpack as openDAW: gzip -> ustar -------------------------------

const untar = bytes => {
    const files = []
    const text = (offset, length) => {
        const end = bytes.indexOf(0, offset)
        return new TextDecoder().decode(bytes.subarray(offset, Math.min(end === -1 ? offset + length : end, offset + length)))
    }
    const state = {offset: 0, longName: ""}
    while (state.offset + BLOCK <= bytes.length && bytes[state.offset] !== 0) {
        const header = state.offset
        const magic = text(header + 257, 6)
        // text() stops at the first NUL, so the POSIX magic reads as "ustar"; the version "00" follows it
        if (magic !== "ustar" || bytes[header + 263] !== 0x30 || bytes[header + 264] !== 0x30) {
            fail(`entry at byte ${header}: not a plain ustar format-00 header ("${magic}")`)
        }
        const size = parseInt(text(header + 124, 12).trim() || "0", 8)
        if (!Number.isFinite(size) || size < 0) {fail("malformed tar header"); break}
        const type = String.fromCharCode(bytes[header + 156])
        const prefix = text(header + 345, 155)
        const name = state.longName.length > 0 ? state.longName : (prefix.length > 0 ? `${prefix}/` : "") + text(header, 100)
        const dataStart = header + BLOCK
        state.longName = ""
        if (type === "L") {
            state.longName = text(dataStart, size)
        } else if (type === "0" || type === "\0") {
            files.push({path: name, bytes: bytes.slice(dataStart, dataStart + size)})
        } else if (type === "x" || type === "g") {
            fail(`pax ${type === "x" ? "extended" : "global"} header for "${name}" -- a plain ustar archive must have none`)
        } else if (type === "5") {
            fail(`directory entry "${name}" -- entries must be files only`)
        } else {
            fail(`entry "${name}" has the unsupported type "${type}"`)
        }
        state.offset = dataStart + Math.ceil(size / BLOCK) * BLOCK
    }
    return files
}

// -- open --------------------------------------------------------------------

// process.argv holds node, the script itself, then the two arguments
const [, , bundleDir, archivePath] = process.argv
if (!bundleDir || !archivePath) {
    console.error("usage: check_bundle_tar.mjs <bundle dir> <bundle.tar.gz>")
    process.exit(2)
}
const name = basename(bundleDir)

const archive = readFileSync(archivePath)
const files = untar(
    new Uint8Array(await new Response(new Blob([archive]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer()))

// -- one top directory, every path has a "/", no AppleDouble -----------------

const entry = path => files.find(file => file.path === path)
const roots = new Set(files.map(({path}) => path.split("/")[0]))
if (roots.size !== 1 || files.some(({path}) => !path.includes("/"))) {
    fail(`stripRoot will not drop the top directory: roots [${[...roots].join(", ")}]`)
} else if (![...roots].every(root => root === name)) {
    fail(`the top directory is "${[...roots].join(", ")}", expected "${name}"`)
}
const metadata = files.map(file => file.path).filter(path => path.split("/").some(segment => segment.startsWith("._")))
if (metadata.length > 0) {fail(`AppleDouble ._ entries: [${metadata.join(", ")}]`)}

// -- what the host has to find after stripping the root ----------------------

const clean = files.map(({path, bytes}) => ({path: path.substring(path.indexOf("/") + 1), bytes}))
const module = entry(`${name}/module.wasm`)
if (module === undefined) {
    fail("no module.wasm in the archive")
} else if (module.bytes.subarray(0, 4).join() !== "0,97,115,109") {
    fail("module.wasm does not carry the wasm magic \\0asm")
}
const page = entry(`${name}/index.html`) ?? entry(`${name}/ui/index.html`)
if (page === undefined) {fail("no index.html in the archive -- the plugin window stays black")}
const cleanModule = clean.find(file => file.path === "module.wasm")
if (module !== undefined && cleanModule !== undefined && cleanModule.bytes.length !== module.bytes.length) {
    fail("stripRoot did not pair the paths with their bytes")
}

// -- deterministic bytes -----------------------------------------------------

const again = resolve(dirname(archivePath), `.${name}.tar.gz.tmp`)
execFileSync("python3", [resolve("tools/pack_wclap.py"), bundleDir, again], {stdio: "pipe"})
const same = statSync(again).size === archive.length && readFileSync(again).equals(archive)
rmSync(again)
if (!same) {fail("packing the same bundle a second time gave different bytes")}

if (errors.length > 0) {
    console.error(`FAIL ${errors.length} problem(s) in ${archivePath}:`)
    for (const error of errors) {console.error(`  - ${error}`)}
    process.exit(1)
}
console.log(`PASS ${archivePath}: ${files.length} entries, root ${name}/, module.wasm + page, the second packing is the same bytes`)
