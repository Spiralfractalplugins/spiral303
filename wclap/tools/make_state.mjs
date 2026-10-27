// Generates the state fixture the page check runs on, from Source/wclap/ParamTable.cpp.
// The wasm dump is the real thing; this is for working on the page without a build.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join (path.dirname (fileURLToPath (import.meta.url)), '..', '..');
const table = fs.readFileSync (path.join (root, 'Source/wclap/ParamTable.cpp'), 'utf8');
const cmake = fs.readFileSync (path.join (root, 'CMakeLists.txt'), 'utf8');
const version = (cmake.match (/project\s*\(Spiral303 VERSION ([\d.]+)/i) || [, '0.0.0'])[1];

const choiceLists = {};
for (const m of table.matchAll (/const char\* const (k\w+Choices)\[\]\s*=\s*\{([^}]*)\};/g))
  choiceLists[m[1]] = m[2].split (',').map ((s) => s.trim ().replace (/^"|"$/g, ''));

const params = {}, ranges = {}, choices = {};
for (const m of table.matchAll (/\{\s*"(\w+)",\s*"([^"]*)",\s*"([^"]*)",\s*(-?[\d.]+),\s*(-?[\d.]+),\s*(-?[\d.]+),\s*(\w+),\s*(\w+),\s*(\d+),\s*(\w+),\s*(\d+)\s*\}/g)) {
  const [, id, , unit, min, max, def, isChoice, , decimals, list, count] = m;
  params[id] = Number (def);
  ranges[id] = { min: Number (min), max: Number (max), default: Number (def),
                 unit, decimals: Number (decimals), name: id };
  if (isChoice === 'true')
    choices[id] = choiceLists[list].slice (0, Number (count));
}

const curve = [];
for (let i = 0; i < 96; ++i) {
  const f = 20 * Math.pow (1000, i / 95);
  const q = 0.5 + 18.5 * 0.62 * 0.62;
  const r = 900 / f;
  curve.push (20 * Math.log10 (1 / Math.sqrt (Math.pow (1 - r * r, 2) + r * r / q)));
}

// The two messages a hello answers with: the bank first, the state last.
const names = ['Init', 'Acid Row', 'Goa Line'];
const credits = ['the starter', '', ''];

console.log (JSON.stringify ([
  { t: 'presets', names, credits },
  { t: 'state',
    params, ranges, choices, curve,
    seq: 'v1:36:1:' + Array.from ({ length: 16 }, (_, i) => (i % 4 === 0 ? 128 + i % 14 : 0)).join (','),
    gate: 'g1:0000:0:16', preset: 'Init', note: '', skin: 0, version: '0.24.1' }
]));
