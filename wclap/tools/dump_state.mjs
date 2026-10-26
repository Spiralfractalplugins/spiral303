// Writes the state the plugin sends to its interface, so that the page can be
// looked at without a host: the file is fed to the page as if the plugin had
// sent it.
//
//   node dump_state.mjs <module.wasm> <host-half.wasm> <out.json>

import fs from 'node:fs';
import { WasmModule, WclapHost } from './wclap-host.mjs';

const [wasmPath, halfPath, outPath] = process.argv.slice (2);
if (!wasmPath || !halfPath || !outPath) {
  console.error ('usage: node dump_state.mjs <module.wasm> <host-half.wasm> <out.json>');
  process.exit (2);
}

const plugin = WasmModule.load (wasmPath);
const hostHalf = WasmModule.load (halfPath);

const host = new WclapHost (plugin, hostHalf);
host.init ();

const instance = host.factory ().create ('com.spiral303.plugin');
instance.init ();
instance.activate (48000, 1, 512);
instance.startProcessing ();

// The handshake: the page says hello, the plugin answers.
const ext = instance.extension ('clap.webview/3');
const message = Buffer.from (JSON.stringify ({ t: 'hello' }), 'utf8');
const messagePtr = plugin.writeBytes (plugin.alloc (message.length), message);
instance.callExt (ext, 8, instance.ptr, messagePtr, message.length);

// The file is the pair of messages the page is answered with, in the order the
// plugin sent them: the bank list, then the state.
const messages = host.webviewLog.map ((text) => JSON.parse (text));
fs.writeFileSync (outPath, JSON.stringify (messages));
console.log ('the state is', messages.length, 'messages,', JSON.stringify (messages).length, 'bytes');
