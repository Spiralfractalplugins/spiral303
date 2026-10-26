// Minimal WCLAP host: loads a CLAP-in-WebAssembly module, drives it like a DAW
// would and reports what came out. Used as the smoke test for the wasm build -
// it needs nothing but node, so it runs in CI without a browser.
//
//   node wclap-host.mjs <module.wasm> [--host-half host-half.wasm]
//
// The host follows the WebCLAP rules: the module exports `clap_entry` (an i32
// global holding the address of the clap_plugin_entry), a growable function
// table, the memory, and `malloc`. Function pointers stored in the module's
// memory are table indices, so calls into the plugin go through the table.
//
// A WebAssembly table only holds WebAssembly functions, so the callbacks the
// plugin needs (event list, host extensions) come from a tiny companion module,
// the "host half", whose exports are copied into the plugin's table.

import fs from 'node:fs';
import path from 'node:path';

// ---------------------------------------------------------------- WASI shim --

function wasiImports (getMemory) {
  const mem = () => getMemory().buffer;

  const writeU64 = (ptr, value) => {
    new DataView(mem()).setBigUint64 (ptr, BigInt (value), true);
  };

  return {
    wasi_snapshot_preview1: {
      clock_time_get: (_id, _precision, outPtr) => {
        writeU64 (outPtr, Math.round (performance.now () * 1e6));
        return 0;
      },
      fd_close: () => 0,
      fd_seek: (_fd, _offset, _whence, outPtr) => {
        writeU64 (outPtr, 0n);
        return 0;
      },
      fd_write: (fd, iovsPtr, iovsLen, nwrittenPtr) => {
        const view = new DataView (mem());
        let total = 0;
        for (let i = 0; i < iovsLen; ++i) {
          const base = view.getUint32 (iovsPtr + i * 8, true);
          const len = view.getUint32 (iovsPtr + i * 8 + 4, true);
          const bytes = new Uint8Array (mem(), base, len);
          // Synchronous, so that the output survives a trap.
          if (fd === 1 || fd === 2) fs.writeSync (fd, Buffer.from (bytes));
          total += len;
        }
        new DataView (mem()).setUint32 (nwrittenPtr, total, true);
        return 0;
      },
      proc_exit: (code) => {
        throw new Error (`module called proc_exit(${code})`);
      },
    },
  };
}

// ----------------------------------------------------------------- module ----

export class WasmModule {
  constructor (instance) {
    this.instance = instance;
    this.memory = instance.exports.memory;
    this.exports = instance.exports;
  }

  static load (file) {
    const bytes = fs.readFileSync (file);
    const module = new WebAssembly.Module (bytes);
    let memory = null;
    const instance = new WebAssembly.Instance (module, wasiImports (() => memory));
    memory = instance.exports.memory;
    const result = new WasmModule (instance);
    if (typeof result.exports._initialize === 'function') result.exports._initialize ();
    return result;
  }

  get buffer () {
    return this.memory.buffer;
  }

  view () {
    return new DataView (this.buffer);
  }

  u32 (addr) { return this.view ().getUint32 (addr, true); }
  i32 (addr) { return this.view ().getInt32 (addr, true); }
  u64 (addr) { return this.view ().getBigUint64 (addr, true); }
  f32 (addr) { return this.view ().getFloat32 (addr, true); }
  f64 (addr) { return this.view ().getFloat64 (addr, true); }

  writeU32 (addr, value) { this.view ().setUint32 (addr, value, true); }
  writeI32 (addr, value) { this.view ().setInt32 (addr, value, true); }
  writeU64 (addr, value) { this.view ().setBigUint64 (addr, value, true); }
  writeF32 (addr, value) { this.view ().setFloat32 (addr, value, true); }
  writeF64 (addr, value) { this.view ().setFloat64 (addr, value, true); }

  writeBytes (addr, bytes) {
    new Uint8Array (this.buffer).set (bytes, addr);
    return addr;
  }

  writeString (addr, text) {
    const bytes = Buffer.from (text + '\0', 'utf8');
    this.writeBytes (addr, bytes);
    return addr;
  }

  readString (addr) {
    if (addr === 0) return '';
    const bytes = new Uint8Array (this.buffer);
    let end = addr;
    while (end < bytes.length && bytes[end] !== 0) ++end;
    return Buffer.from (bytes.subarray (addr, end)).toString ('utf8');
  }

  /** Reads a fixed-size character array, the way the parameter info carries
      its name. */
  readFixedString (addr, size) {
    const bytes = new Uint8Array (this.buffer, addr, size);
    let end = 0;
    while (end < size && bytes[end] !== 0) ++end;
    return Buffer.from (bytes.subarray (0, end)).toString ('utf8');
  }

  /** Finds the address of a NUL-terminated string in the module's memory. */
  findString (text) {
    const needle = Buffer.from (text + '\0', 'utf8');
    const haystack = Buffer.from (this.buffer);
    return haystack.indexOf (needle);
  }

  /** Every copy of a string in the module's memory. A header with a static
      string constant gives each translation unit its own copy, so the host has
      to know about all of them. */
  findAllStrings (text) {
    const needle = Buffer.from (text + '\0', 'utf8');
    const haystack = Buffer.from (this.buffer);
    const found = [];
    let at = haystack.indexOf (needle);
    while (at >= 0) {
      found.push (at);
      at = haystack.indexOf (needle, at + 1);
    }
    return found;
  }

  alloc (size) {
    const ptr = this.exports.malloc (size);
    if (!ptr) throw new Error (`malloc(${size}) failed`);
    return ptr;
  }
}

// ------------------------------------------------------------------- host ----

const CLAP_PLUGIN_ENTRY_SIZE = 24;
const CLAP_HOST_SIZE = 48;
const CLAP_PROCESS_SIZE = 40;
const CLAP_AUDIO_BUFFER_SIZE = 24;
const CLAP_INPUT_EVENTS_SIZE = 12;
const CLAP_EVENT_NOTE_SIZE = 36;
const CLAP_EVENT_PARAM_VALUE_SIZE = 48;
const CLAP_PARAM_INFO_SIZE = 1320;

const CLAP_EVENT_NOTE_ON = 0;
const CLAP_EVENT_NOTE_OFF = 1;
const CLAP_EVENT_PARAM_VALUE = 5;

/** A nine-byte-shaped wasm function that forwards to a JS one.
 *
 *  The plugin reaches the host callbacks through call_indirect, and a WebAssembly
 *  table accepts only WebAssembly functions: a JS closure cannot be put there.
 *  To watch every message the plugin sends (a hello is answered with two, and the
 *  payload of the first one is freed before the test reads anything), the callback
 *  slot holds this trampoline, whose body is `call $js_send` of the imported
 *  function. Types: (i32,i32,i32) -> i32, exactly clap_webview_t::send.
 */
function makeTrampoline (moduleName, funcName, onCall) {
  const modName = [...Buffer.from (moduleName, 'utf8')];
  const fnName = [...Buffer.from (funcName, 'utf8')];
  const mod = new WebAssembly.Module (new Uint8Array ([
    // magic, version
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    // type: (i32,i32,i32) -> i32
    0x01, 0x08, 0x01, 0x60, 0x03, 0x7f, 0x7f, 0x7f, 0x01, 0x7f,
    // import: the JS callback
    0x02, 0x05 + modName.length + fnName.length, 0x01,
    modName.length, ...modName, fnName.length, ...fnName, 0x00, 0x00,
    // one function of that type
    0x03, 0x02, 0x01, 0x00,
    // export: the wasm function the table can hold
    0x07, 0x04 + fnName.length, 0x01, fnName.length, ...fnName, 0x00, 0x00,
    // body: local.get 0..2, call $js, end
    0x0a, 0x0c, 0x01, 0x0a, 0x00, 0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0x10, 0x00, 0x0b,
  ]));
  const instance = new WebAssembly.Instance (mod, { [moduleName]: { [funcName]: onCall } });
  return { instance, fn: instance.exports[funcName] };
}

export class WclapHost {
  constructor (plugin, hostHalf) {
    this.plugin = plugin;
    this.hostHalf = hostHalf;
    this.table = plugin.exports.__indirect_function_table;

    // Reserve table slots for the host callbacks and copy the host half's
    // exports into them: the plugin reaches them through call_indirect.
    const base = this.table.grow (16);
    this.slots = {
      getExtension: base + 0,
      eventSize: base + 1,
      eventGet: base + 2,
      paramsRescan: base + 3,
      paramsClear: base + 4,
      paramsRequestFlush: base + 5,
      webviewSend: base + 6,
      streamWrite: base + 7,
      paramHoveredUpdate: base + 8,
    };

    const he = hostHalf.exports;
    this.table.set (this.slots.getExtension, he.host_get_extension);
    this.table.set (this.slots.eventSize, he.host_event_size);
    this.table.set (this.slots.eventGet, he.host_event_get);
    this.table.set (this.slots.paramsRescan, he.host_params_rescan);
    this.table.set (this.slots.paramsClear, he.host_params_clear);
    this.table.set (this.slots.paramsRequestFlush, he.host_params_request_flush);
    this.table.set (this.slots.streamWrite, he.host_stream_write);
    this.table.set (this.slots.paramHoveredUpdate, he.host_param_hovered_update);

    // A hello is answered with two messages, the bank list first, and the plugin
    // reuses one buffer for both. So the copy has to be taken during the call:
    // the slot holds a wasm trampoline that calls back into JS.
    this.webviewLog = [];
    this.webviewTrampoline = makeTrampoline ('env', 'webview_send', (hostPtr, buffer, size) => {
      if (this.webviewLog.length < 64)
        this.webviewLog.push (Buffer.from (new Uint8Array (plugin.buffer, buffer, size)).toString ('utf8'));
      return he.host_webview_send (hostPtr, buffer, size);
    });
    this.table.set (this.slots.webviewSend, this.webviewTrampoline.fn);

    this.hostPtr = plugin.alloc (CLAP_HOST_SIZE);
    this.setupHostStruct ();
    this.setupExtensions ();
  }

  setupHostStruct () {
    const m = this.plugin;
    const namePtr = m.alloc (32);
    const vendorPtr = m.alloc (32);
    const urlPtr = m.alloc (64);
    const versionPtr = m.alloc (32);
    m.writeString (namePtr, 'node-wclap-host');
    m.writeString (vendorPtr, 'Spiralfractal');
    m.writeString (urlPtr, 'https://github.com/spiralfractal');
    m.writeString (versionPtr, '0.1.0');

    m.writeU32 (this.hostPtr + 0, 1);   // clap_version.major
    m.writeU32 (this.hostPtr + 4, 2);   // minor
    m.writeU32 (this.hostPtr + 8, 7);   // revision
    m.writeU32 (this.hostPtr + 12, 0);  // host_data
    m.writeU32 (this.hostPtr + 16, namePtr);
    m.writeU32 (this.hostPtr + 20, vendorPtr);
    m.writeU32 (this.hostPtr + 24, urlPtr);
    m.writeU32 (this.hostPtr + 28, versionPtr);
    m.writeU32 (this.hostPtr + 32, this.slots.getExtension);
    m.writeU32 (this.hostPtr + 36, 0);  // request_restart
    m.writeU32 (this.hostPtr + 40, 0);  // request_process
    m.writeU32 (this.hostPtr + 44, 0);  // request_callback
  }

  /** Points the host half at the extension ids the plugin uses. */
  setupExtensions () {
    const m = this.plugin;
    const he = this.hostHalf.exports;

    const paramsIds = m.findAllStrings ('clap.params');
    const webviewIds = m.findAllStrings ('clap.webview/3');
    const hoveredIds = m.findAllStrings ('clap.param-hovered/1');

    if (paramsIds.length > 0) {
      const structPtr = m.alloc (12);
      m.writeU32 (structPtr + 0, this.slots.paramsRescan);
      m.writeU32 (structPtr + 4, this.slots.paramsClear);
      m.writeU32 (structPtr + 8, this.slots.paramsRequestFlush);
      he.host_set_extension_struct (1, structPtr);
      for (const idPtr of paramsIds)
        he.host_register_id (idPtr, 1);
    }

    if (webviewIds.length > 0) {
      const structPtr = m.alloc (4);
      m.writeU32 (structPtr + 0, this.slots.webviewSend);
      he.host_set_extension_struct (2, structPtr);
      for (const idPtr of webviewIds)
        he.host_register_id (idPtr, 2);
    }

    if (hoveredIds.length > 0) {
      const structPtr = m.alloc (4);
      m.writeU32 (structPtr + 0, this.slots.paramHoveredUpdate);
      he.host_set_extension_struct (3, structPtr);
      for (const idPtr of hoveredIds)
        he.host_register_id (idPtr, 3);
    }
  }

  entryAddress () {
    const e = this.plugin.exports.clap_entry;
    if (e instanceof WebAssembly.Global) return e.value;
    if (typeof e === 'function') return e ();
    throw new Error ('clap_entry is neither a global nor a function');
  }

  init () {
    const m = this.plugin;
    const entry = this.entryAddress ();
    this.entry = {
      address: entry,
      version: [m.u32 (entry), m.u32 (entry + 4), m.u32 (entry + 8)],
      initPtr: m.u32 (entry + 12),
      deinitPtr: m.u32 (entry + 16),
      factoryPtr: m.u32 (entry + 20),
    };
    const ok = m.exports.__indirect_function_table.get (this.entry.initPtr) ('/plugin/module.wasm');
    if (ok === false) throw new Error ('clap_entry.init() returned false');
    return this.entry;
  }

  factory () {
    const m = this.plugin;
    const call = (index, ...args) => m.exports.__indirect_function_table.get (index) (...args);
    // get_factory is a table index; calling it returns the factory struct address.
    const f = call (this.entry.factoryPtr);
    if (!f) throw new Error ('get_factory returned null');
    this.factoryStruct = f;
    return {
      count: call (m.u32 (f + 0)),
      descriptor: (index) => {
        const descPtr = call (m.u32 (f + 4), index);
        if (!descPtr) return null;
        // clap_plugin_descriptor starts with its own clap_version (12 bytes).
        return {
          id: m.readString (m.u32 (descPtr + 12)),
          name: m.readString (m.u32 (descPtr + 16)),
          vendor: m.readString (m.u32 (descPtr + 20)),
          version: m.readString (m.u32 (descPtr + 36)),
        };
      },
      create: (id) => {
        const m = this.plugin;
        const idPtr = m.writeString (m.alloc (id.length + 1), id);
        const ptr = call (m.u32 (f + 8), f, this.hostPtr, idPtr);
        return ptr ? new WclapPlugin (this, ptr) : null;
      },
    };
  }
}

export class WclapPlugin {
  constructor (host, ptr) {
    this.host = host;
    this.plugin = host.plugin;
    this.ptr = ptr;
  }

  get descriptor () {
    const m = this.plugin;
    const descPtr = m.u32 (this.ptr + 0);
    // clap_plugin_descriptor starts with its own clap_version (12 bytes).
    return {
      id: m.readString (m.u32 (descPtr + 12)),
      name: m.readString (m.u32 (descPtr + 16)),
      version: m.readString (m.u32 (descPtr + 36)),
    };
  }

  call (offset, ...args) {
    return this.plugin.exports.__indirect_function_table.get (this.plugin.u32 (this.ptr + offset)) (...args);
  }

  extension (id) {
    // get_extension takes the plugin and a pointer to the id, which lives in
    // the plugin's own memory.
    const m = this.plugin;
    const idPtr = m.writeString (m.alloc (id.length + 1), id);
    return this.call (40, this.ptr, idPtr) || null;
  }

  init () { return this.call (8, this.ptr) !== false; }
  activate (sr, min, max) { return this.call (16, this.ptr, sr, min, max) !== false; }
  deactivate () { this.call (20, this.ptr); }
  startProcessing () { return this.call (24, this.ptr) !== false; }
  stopProcessing () { this.call (28, this.ptr); }
  destroy () { this.call (12, this.ptr); }

  /** Runs one audio block; returns the peak of the first output port.
      `notes` are MIDI notes, `paramEvents` are parameter changes. */
  process (frames, notes = [], paramEvents = []) {
    const m = this.plugin;
    const he = this.host.hostHalf.exports;

    // Event list: the structs live in the plugin's memory, the host half hands
    // them out through the table slots.
    const events = [];
    for (const note of notes)
      events.push ({ kind: 'note', note });
    for (const param of paramEvents)
      events.push ({ kind: 'param', param });

    const count = events.length;
    // Every event gets the biggest slot: a parameter event is larger than a
    // note event, and the host half hands them out by this stride.
    const stride = CLAP_EVENT_PARAM_VALUE_SIZE;
    const eventBuf = m.alloc (stride * Math.max (1, count));

    events.forEach((entry, i) => {
      const at = eventBuf + i * stride;
      if (entry.kind === 'note') {
        const note = entry.note;
        m.writeU32 (at + 0, CLAP_EVENT_NOTE_SIZE);
        m.writeU32 (at + 4, note.time ?? 0);
        m.writeU32 (at + 8, 0);                      // space_id (u16) + type (u16)
        m.writeU32 (at + 10, note.on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF);
        m.writeU32 (at + 12, 0);                     // flags
        m.writeI32 (at + 16, note.id ?? 1);           // note_id
        m.writeI32 (at + 20, 0);                     // port_index + channel
        m.writeI32 (at + 24, note.key ?? 60);        // key
        m.writeF64 (at + 28, note.velocity ?? 1.0);
      } else {
        const param = entry.param;
        m.writeU32 (at + 0, CLAP_EVENT_PARAM_VALUE_SIZE);
        m.writeU32 (at + 4, param.time ?? 0);
        m.writeU32 (at + 8, 0);
        m.writeU32 (at + 10, CLAP_EVENT_PARAM_VALUE);
        m.writeU32 (at + 12, 0);                     // flags
        m.writeU32 (at + 16, param.id);              // param_id
        m.writeU32 (at + 20, 0);                     // cookie
        m.writeI32 (at + 24, -1);                    // note_id
        m.writeI32 (at + 28, -1);                    // port_index + channel
        m.writeI32 (at + 32, -1);                    // key
        m.writeF64 (at + 40, param.value);
      }
    });
    he.host_events_begin (eventBuf, count, stride);

    const eventsPtr = m.alloc (CLAP_INPUT_EVENTS_SIZE);
    m.writeU32 (eventsPtr + 0, 0);
    m.writeU32 (eventsPtr + 4, this.host.slots.eventSize);
    m.writeU32 (eventsPtr + 8, this.host.slots.eventGet);

    // Audio buffers: one stereo output, the samples live in module memory.
    const outBufPtr = m.alloc (CLAP_AUDIO_BUFFER_SIZE);
    const outDataPtr = m.alloc (8);
    const leftPtr = m.alloc (frames * 4);
    const rightPtr = m.alloc (frames * 4);
    m.writeU32 (outDataPtr + 0, leftPtr);
    m.writeU32 (outDataPtr + 4, rightPtr);
    m.writeU32 (outBufPtr + 0, outDataPtr);
    m.writeU32 (outBufPtr + 4, 0);
    m.writeU32 (outBufPtr + 8, 2);
    m.writeU32 (outBufPtr + 12, 0);
    m.writeU64 (outBufPtr + 16, 0n);

    const processPtr = m.alloc (CLAP_PROCESS_SIZE);
    m.writeU64 (processPtr + 0, 0n);
    m.writeU32 (processPtr + 8, frames);
    m.writeU32 (processPtr + 12, 0);
    m.writeU32 (processPtr + 16, 0);
    m.writeU32 (processPtr + 20, outBufPtr);
    m.writeU32 (processPtr + 24, 0);
    m.writeU32 (processPtr + 28, 1);
    m.writeU32 (processPtr + 32, eventsPtr);   // in_events
    m.writeU32 (processPtr + 36, 0);           // out_events

    const status = this.call (36, this.ptr, processPtr);

    let peak = 0;
    const view = m.view ();
    for (let i = 0; i < frames; ++i) {
      peak = Math.max (peak, Math.abs (view.getFloat32 (leftPtr + i * 4, true)));
      peak = Math.max (peak, Math.abs (view.getFloat32 (rightPtr + i * 4, true)));
    }

    // A copy of the block, so that the caller can keep it.
    this.lastLeft = new Float32Array (frames);
    this.lastRight = new Float32Array (frames);
    for (let i = 0; i < frames; ++i) {
      this.lastLeft[i] = view.getFloat32 (leftPtr + i * 4, true);
      this.lastRight[i] = view.getFloat32 (rightPtr + i * 4, true);
    }

    return { status, peak };
  }

  /** The last message the plugin sent to the webview, as text. */
  lastWebviewMessage () {
    const he = this.host.hostHalf.exports;
    const ptr = he.host_last_message_ptr ();
    const size = he.host_last_message_size ();
    if (!ptr || !size) return '';
    return Buffer.from (new Uint8Array (this.plugin.buffer, ptr, size)).toString ('utf8');
  }

  //==============================================================================
  // Parameters

  /** Calls a function pointer inside an extension struct. */
  callExt (extPtr, offset, ...args) {
    const index = this.plugin.u32 (extPtr + offset);
    return this.plugin.exports.__indirect_function_table.get (index) (...args);
  }

  /** The parameters the plugin offers, as { id, name, min, max, default }. */
  params () {
    const m = this.plugin;
    const ext = this.extension ('clap.params');
    if (!ext) return [];

    const infoPtr = m.alloc (CLAP_PARAM_INFO_SIZE);
    const result = [];

    const count = this.callExt (ext, 0, this.ptr);
    for (let i = 0; i < count; ++i) {
      if (!this.callExt (ext, 4, this.ptr, i, infoPtr)) continue;
      result.push ({
        index: i,
        id: m.u32 (infoPtr + 0),
        name: m.readFixedString (infoPtr + 12, 256),
        min: m.f64 (infoPtr + 1296),
        max: m.f64 (infoPtr + 1304),
        default: m.f64 (infoPtr + 1312),
      });
    }

    return result;
  }

  /** Finds a parameter by the name the host shows. */
  paramByName (name) {
    return this.params ().find ((p) => p.name === name) || null;
  }

  /** The current value of a parameter. */
  paramValue (param) {
    const m = this.plugin;
    const ext = this.extension ('clap.params');
    if (!ext) return 0;

    const valuePtr = m.alloc (8);
    if (!this.callExt (ext, 8, this.ptr, param.id, valuePtr)) return 0;
    return m.f64 (valuePtr);
  }
}

// ------------------------------------------------------------------- main ----

// The extensions the plugin offers, and how many function pointers each has.
const EXTENSION_SLOTS = {
  'clap.gui': 15,
  'clap.webview/3': 3,
  'clap.params': 6,
  'clap.audio-ports': 2,
  'clap.state': 2,
};

/** The same hash the plugin uses for its parameter ids (FNV-1a). */
function paramIdHash (text) {
  let hash = 2166136261 >>> 0;
  for (let i = 0; i < text.length; ++i) {
    hash = (hash ^ (text.charCodeAt (i) & 0xff)) >>> 0;
    hash = Math.imul (hash, 16777619) >>> 0;
  }
  return hash >>> 0;
}

/** Every function pointer of every extension has to be there: a host that
    reads one without checking it for null traps, and the error it reports is
    a bare address (openDAW: "null function pointer at 117660"). */
export function extensionSlotsAreFilled (pluginInstance) {
  const problems = [];
  const module = pluginInstance.plugin;
  const table = module.exports.__indirect_function_table;

  for (const [id, count] of Object.entries (EXTENSION_SLOTS)) {
    const ext = pluginInstance.extension (id);
    if (! ext) {
      problems.push (id + ': the plugin does not offer it');
      continue;
    }

    for (let i = 0; i < count; ++i) {
      const index = module.u32 (ext + i * 4);
      if (index === 0) {
        problems.push (id + ': slot ' + i + ' is empty');
        continue;
      }
      if (index >= table.length || typeof table.get (index) !== 'function')
        problems.push (id + ': slot ' + i + ' does not point at a function');
    }
  }

  return problems;
}

async function main () {
  const args = process.argv.slice (2);
  const wasmPath = args[0];
  if (!wasmPath) {
    console.error ('usage: node wclap-host.mjs <module.wasm> [--host-half host-half.wasm] [--render file]');
    process.exit (2);
  }
  const halfIndex = args.indexOf ('--host-half');
  const halfPath = halfIndex >= 0 ? args[halfIndex + 1]
                                  : path.join (path.dirname (wasmPath), 'host-half.wasm');
  const renderIndex = args.indexOf ('--render');
  const renderPath = renderIndex >= 0 ? args[renderIndex + 1] : null;
  const webview = args.includes ('--webview');
  const api = args.includes ('--api');
  const stateIndex = args.indexOf ('--state-out');
  const stateOut = stateIndex >= 0 ? args[stateIndex + 1] : null;

  const plugin = WasmModule.load (wasmPath);
  const hostHalf = WasmModule.load (halfPath);

  const host = new WclapHost (plugin, hostHalf);
  const entry = host.init ();
  console.log ('clap_entry version:', entry.version.join ('.'));
  console.log ('clap_entry address:', entry.address);

  const factory = host.factory ();
  console.log ('plugins:', factory.count);
  for (let i = 0; i < factory.count; ++i) console.log ('  ', factory.descriptor (i));

  const id = factory.descriptor (0).id;
  const pluginInstance = factory.create (id);
  if (!pluginInstance) throw new Error ('create_plugin failed');

  pluginInstance.init ();
  const frames = 512;
  pluginInstance.activate (48000, 1, frames);
  pluginInstance.startProcessing ();

  if (webview) {
    // The webview: the plugin serves the page from its own memory and answers
    // the messages the page sends.
    const ext = pluginInstance.extension ('clap.webview/3');
    if (!ext) throw new Error ('the plugin has no webview extension');

    const uriPtr = plugin.alloc (256);
    const uriLength = pluginInstance.callExt (ext, 0, pluginInstance.ptr, uriPtr, 256);
    console.log ('get_uri ->', JSON.stringify (plugin.readString (uriPtr)), '(%d bytes)', uriLength);

    // The page itself.
    const pathPtr = plugin.writeString (plugin.alloc (32), '/index.html');
    const mimePtr = plugin.alloc (64);
    const streamPtr = plugin.alloc (16);

    // The stream: the plugin writes the resource through it.
    plugin.writeU32 (streamPtr + 0, 0);
    plugin.writeU32 (streamPtr + 4, host.slots.streamWrite);

    const served = pluginInstance.callExt (ext, 4, pluginInstance.ptr, pathPtr, mimePtr, 64, streamPtr);
    const writePtr = hostHalf.exports.host_last_write_ptr ();
    const writeSize = hostHalf.exports.host_last_write_size ();
    const text = writePtr && writeSize
      ? Buffer.from (new Uint8Array (plugin.buffer, writePtr, writeSize)).toString ('utf8')
      : '';
    console.log ('get_resource ->', served, plugin.readString (mimePtr), text.length, 'bytes');
    console.log ('the page starts with:', JSON.stringify (text.slice (0, 60)));

    // The handshake: the page says hello, the plugin answers with the state.
    const message = Buffer.from (JSON.stringify ({ t: 'hello' }), 'utf8');
    const messagePtr = plugin.writeBytes (plugin.alloc (message.length), message);
    const received = pluginInstance.callExt (ext, 8, pluginInstance.ptr, messagePtr, message.length);
    console.log ('receive ->', received);

    const answer = pluginInstance.lastWebviewMessage ();
    console.log ('the plugin answered with', answer.length, 'bytes');
    const parsed = JSON.parse (answer);
    console.log ('  type:    ', parsed.t);
    console.log ('  params:  ', Object.keys (parsed.params || {}).length);
    console.log ('  choices: ', Object.keys (parsed.choices || {}).length);
    console.log ('  sequence:', parsed.seq);
    console.log ('  gate:    ', parsed.gate);
    console.log ('  preset:  ', parsed.preset);
    console.log ('  curve:   ', (parsed.curve || []).length, 'points');

    // Every extension has to offer all of its functions: a host that reads a
    // pointer without checking it for null traps (openDAW did, and reported
    // "null function pointer at 117660").
    const emptySlots = extensionSlotsAreFilled (pluginInstance);
    for (const problem of emptySlots)
      console.log ('  empty:', problem);
    console.log (emptySlots.length === 0
      ? 'every extension offers all of its functions'
      : emptySlots.length + ' extension slot(s) are empty');

    // The hovered parameter: the page tells the plugin, the plugin tells the
    // host.
    const hoverMessage = Buffer.from (JSON.stringify ({ t: 'hover', id: 'Cutoff' }), 'utf8');
    const hoverPtr = plugin.writeBytes (plugin.alloc (hoverMessage.length), hoverMessage);
    pluginInstance.callExt (ext, 8, pluginInstance.ptr, hoverPtr, hoverMessage.length);
    const hovered = hostHalf.exports.host_last_hovered_param () >>> 0;   // i32 in, unsigned out
    const expected = paramIdHash ('Cutoff');
    console.log ('the hovered parameter reaches the host:', hovered, '(expected', expected + ')');

    const ok = served && received && parsed.t === 'state' && text.includes ('<canvas')
      && emptySlots.length === 0 && hovered === expected;
    console.log (ok ? 'OK: the interface is served and answers' : 'FAIL: the webview is broken');
    process.exit (ok ? 0 : 1);
  }

  if (api) {
    // The API level: every extension the plugin offers, the window functions
    // the hosts call, and the state.
    const problems = extensionSlotsAreFilled (pluginInstance);
    for (const problem of problems)
      console.log ('  empty:', problem);
    console.log (problems.length === 0
      ? 'every extension offers all of its functions'
      : problems.length + ' extension slot(s) are empty');

    // The window functions a host calls without checking them first.
    const gui = pluginInstance.extension ('clap.gui');
    const apiPtr = plugin.writeString (plugin.alloc (32), 'webview');
    const otherApiPtr = plugin.writeString (plugin.alloc (32), 'cocoa');
    const created = pluginInstance.callExt (gui, 8, pluginInstance.ptr, apiPtr, 0);
    const refused = pluginInstance.callExt (gui, 8, pluginInstance.ptr, otherApiPtr, 0);
    console.log ('gui create (webview) ->', created, ', another api ->', refused);

    const sizePtr = plugin.alloc (8);
    pluginInstance.callExt (gui, 20, pluginInstance.ptr, sizePtr, sizePtr + 4);
    console.log ('gui size ->', plugin.u32 (sizePtr), 'x', plugin.u32 (sizePtr + 4));
    console.log ('gui can_resize ->', pluginInstance.callExt (gui, 24, pluginInstance.ptr));

    const hintsPtr = plugin.alloc (12);
    pluginInstance.callExt (gui, 28, pluginInstance.ptr, hintsPtr);
    console.log ('gui resize hints ->',
      plugin.view ().getUint8 (hintsPtr) ? 'horizontally' : 'fixed',
      plugin.view ().getUint8 (hintsPtr + 1) ? 'vertically' : 'fixed');

    console.log ('gui set_size ->', pluginInstance.callExt (gui, 36, pluginInstance.ptr, 900, 420));
    console.log ('gui set_parent ->', pluginInstance.callExt (gui, 40, pluginInstance.ptr, 0));
    console.log ('gui destroy ->', pluginInstance.callExt (gui, 12, pluginInstance.ptr), '(no return value)');

    // The state: the same format the native build writes. The values are
    // distinctive, so that the round trip through the native build can be
    // checked.
    if (stateOut) {
      const cutoff = pluginInstance.paramByName ('Cutoff');
      pluginInstance.process (512, [], [{ id: cutoff.id, value: 1234.5 }]);

      const webview = pluginInstance.extension ('clap.webview/3');
      const seqMessage = Buffer.from (JSON.stringify ({ t: 'seq', text: 'v1:40:2:640,128,388,128,642,132,389,1' }), 'utf8');
      const seqPtr = plugin.writeBytes (plugin.alloc (seqMessage.length), seqMessage);
      pluginInstance.callExt (webview, 8, pluginInstance.ptr, seqPtr, seqMessage.length);

      const state = pluginInstance.extension ('clap.state');
      const streamPtr = plugin.alloc (16);
      plugin.writeU32 (streamPtr + 0, 0);
      plugin.writeU32 (streamPtr + 4, host.slots.streamWrite);
      const saved = pluginInstance.callExt (state, 0, pluginInstance.ptr, streamPtr);
      const ptr = hostHalf.exports.host_last_write_ptr ();
      const size = hostHalf.exports.host_last_write_size ();
      fs.writeFileSync (stateOut, Buffer.from (new Uint8Array (plugin.buffer, ptr, size)));
      console.log ('the state was written:', stateOut, size, 'bytes, saved =', saved !== 0);
    }

    // An id nobody knows must come back empty rather than as a broken pointer.
    const unknownPtr = plugin.writeString (plugin.alloc (32), 'clap.does-not-exist');
    const unknown = pluginInstance.call (40, pluginInstance.ptr, unknownPtr);
    console.log ('an unknown extension ->', unknown, unknown === 0 ? '(empty, as it should be)' : '(NOT EMPTY)');

    // A second instance of the plugin has to work as well: hosts open more than one.
    const second = host.factory ().create (id);
    second.init ();
    second.activate (48000, 1, 512);
    second.startProcessing ();
    // The sequencer needs a step to reach the first note, so a few blocks.
    let secondPeak = 0;
    for (let i = 0; i < 40; ++i) {
      const notes = i === 0 ? [{ key: 60, on: true, time: 0 }] : [];
      secondPeak = Math.max (secondPeak, second.process (512, notes).peak);
    }
    console.log ('a second instance ->', secondPeak > 0 ? 'it plays' : 'IT IS SILENT');
    second.destroy ();

    // A soak: the output has to stay finite and the level sane.
    let soakPeak = 0;
    for (let i = 0; i < 2000; ++i) {
      const notes = i === 0 ? [{ key: 60, on: true, time: 0 }] : [];
      const peak = pluginInstance.process (512, notes).peak;
      if (! Number.isFinite (peak)) {
        console.log ('the soak broke at block', i);
        break;
      }
      soakPeak = Math.max (soakPeak, peak);
    }
    console.log ('a soak of 2000 blocks -> peak', soakPeak.toFixed (6), Number.isFinite (soakPeak) ? '(finite)' : '(NOT FINITE)');

    const ok = problems.length === 0 && created === 1 && refused === 0
      && unknown === 0 && secondPeak > 0 && Number.isFinite (soakPeak) && soakPeak > 0;
    console.log (ok ? 'OK: the API is complete' : 'FAIL: the API is incomplete');
    process.exit (ok ? 0 : 1);
  }

  if (renderPath) {
    // The comparison render: the same pattern the native build dumps, so that
    // the two can be compared sample by sample.
    const sync = pluginInstance.paramByName ('Sync');
    if (!sync) throw new Error ('the plugin has no Sync parameter');

    const blocks = 192;   // two seconds
    const samples = [];

    for (let block = 0; block < blocks; ++block) {
      const paramEvents = block === 0 ? [{ id: sync.id, value: 1 }] : [];
      pluginInstance.process (frames, [], paramEvents);

      const left = pluginInstance.lastLeft;
      const right = pluginInstance.lastRight;
      for (let i = 0; i < frames; ++i) {
        samples.push (left[i], right[i]);
      }
    }

    fs.writeFileSync (renderPath, Buffer.from (new Float32Array (samples).buffer));
    console.log ('render written to', renderPath, samples.length, 'values');
    process.exit (0);
  }

  let silentPeak = 0;
  let soundingPeak = 0;

  // A block with no note: the sequencer may still be running.
  for (let i = 0; i < 4; ++i)
    silentPeak = Math.max (silentPeak, pluginInstance.process (frames, []).peak);

  // Then a note, and let the pattern run.
  for (let i = 0; i < 96; ++i)
  {
    const notes = i === 0 ? [{ key: 60, on: true, time: 0 }] : [];
    soundingPeak = Math.max (soundingPeak, pluginInstance.process (frames, notes).peak);
  }

  console.log ('peak before note:', silentPeak.toFixed (6));
  console.log ('peak after note: ', soundingPeak.toFixed (6));

  const ok = soundingPeak > 0.0001;
  console.log (ok ? 'OK: the plugin made sound' : 'FAIL: silence');
  process.exit (ok ? 0 : 1);
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main ().catch ((e) => {
    console.error (e);
    process.exit (1);
  });
}
