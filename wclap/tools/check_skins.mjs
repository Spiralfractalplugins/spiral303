// The two copies of the four skins, and whether they are fit to be seen.
//
//   node wclap/tools/check_skins.mjs [web-dir] [source-dir]
//
// The interface is painted from a colour table in web/app.js and from a second,
// hand-written table in Source/gui/Palette.h. The comment in app.js says the one
// is copied letter for letter from the other; nothing but this file checks it, and
// a skin that drifts means the browser shows one plugin and the native editor
// another. So the first thing here is that both tables hold the same 38 colours
// under the same names.
//
// The second is contrast. A pair below is a line in the painter: the two names are
// the colours the call passes, the numbers in brackets are the web lines that draw
// them, and the surface is whatever is underneath at that moment - a panel, the
// cabinet face, a chip, the meter's pocket. Nothing a user only glances past (a
// grid line, a bevel, a panel edge) carries a line here; WCAG draws its line under
// what has to be read or found.
//
// The thresholds are WCAG 2's: 4.5 to 1 for text - every string this interface
// prints is 8.5 to 14 px, below the large-text bar, so none of them gets the 3:1
// discount - and 3 to 1 for a mark that states something: a pointer, an arc, a
// level, a lit cell.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname (fileURLToPath (import.meta.url));
const webDir = process.argv[2] || path.join (here, '..', '..', 'web');
const sourceDir = process.argv[3] || path.join (here, '..', '..', 'Source', 'gui');

const js = fs.readFileSync (path.join (webDir, 'app.js'), 'utf8');
const h = fs.readFileSync (path.join (sourceDir, 'Palette.h'), 'utf8');

const failures = [];
const check = (ok, what, detail) => {
  console.log ((ok ? 'PASS  ' : 'FAIL  ') + what + (detail === undefined ? '' : '  [' + detail + ']'));
  if (! ok) failures.push (what);
};

// --- the colour arithmetic --------------------------------------------------
const rgb = (hex) => { const c = hex.replace ('#', ''); return [0, 2, 4].map ((at) => parseInt (c.slice (at, at + 2), 16)); };
const hex = (bytes) => bytes.map ((v) => Math.round (Math.min (255, Math.max (0, v))).toString (16).padStart (2, '0')).join ('');
const channel = (byte) => { const s = byte / 255; return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4; };
const luminance = (c) => { const [r, g, b] = rgb (c); return 0.2126 * channel (r) + 0.7152 * channel (g) + 0.0722 * channel (b); };
const ratio = (a, b) => { const [hi, lo] = [luminance (a), luminance (b)].sort ((x, y) => y - x); return (hi + 0.05) / (lo + 0.05); };
// Paint over paint, the way the browser composites it: withAlpha (fg, a) on an
// opaque surface, and the darker()/brighter() mixes of app.js:973.
const over = (fg, bg, a) => hex (rgb (fg).map ((v, i) => v * a + rgb (bg)[i] * (1 - a)));

// --- the web table ----------------------------------------------------------
const table = (js.match (/const SKINS = \[([\s\S]*?)\n\];/) || [, ''])[1];
const skins = [...table.matchAll (/\{([^{}]*)\}/g)].map ((entry) => {
  const fields = {};
  for (const [, key, word] of entry[1].matchAll (/(\w+):\s*'([^']+)'/g))
    fields[key] = word[0] === '#' ? word.slice (1) : word;
  for (const [, key, flag] of entry[1].matchAll (/(\w+):\s*(true|false)/g)) fields[key] = flag === 'true';
  return { name: fields.name || '?', values: fields };
});

check (skins.length === 4, 'the web table holds four skins', skins.map ((s) => s.name).join (' '));

// --- the native table -------------------------------------------------------
// `wood.textDim = juce::Colour (0xff544b3c)`, and the aliases the native table
// writes instead of repeating a literal (`wood.chipText = wood.textDim`).
const native = {};
{
  const assigned = new Map();
  for (const [, skin, field, value] of h.matchAll (/(\w+)\.(\w+)\s*=\s*juce::Colour\s*\(\s*0xff([0-9a-fA-F]{6})\s*\)/g))
    assigned.set (skin + '.' + field, value.toLowerCase ());
  for (const [, skin, field, other, otherField] of h.matchAll (/(\w+)\.(\w+)\s*=\s*(\w+)\.(\w+)\s*;/g))
    if (assigned.has (other + '.' + otherField)) assigned.set (skin + '.' + field, assigned.get (other + '.' + otherField));
  for (const name of ['dark', 'console', 'wood', 'metal']) {
    const values = {};
    for (const [key, value] of assigned) if (key.startsWith (name + '.')) values[key.slice (name.length + 1)] = value;
    native[name] = values;
  }
}

const COLOURS = ['background', 'panel', 'panelOutline', 'text', 'textDim', 'accent', 'accentAlt',
                 'pitch', 'gate', 'gridLine', 'gridBeat', 'knobTrack', 'playhead', 'faceInk',
                 'faceBase', 'faceHi', 'faceLo', 'bevelHi', 'bevelLo', 'shadow', 'spec',
                 'bodyTop', 'bodyBottom', 'skirt', 'washer', 'pointer', 'screw', 'screwHi',
                 'ink', 'grain', 'ledOn', 'ledOff', 'hazard', 'meterGreen', 'meterAmber',
                 'meterRed', 'chip', 'chipText'];

const byNativeName = { Dark: 'dark', Console: 'console', Wood: 'wood', Metal: 'metal' };
for (const skin of skins) {
  const other = native[byNativeName[skin.name]];
  if (! other) { check (false, skin.name + ': Palette.h has no table for it'); continue; }
  // A field neither table writes would compare undefined to undefined and pass,
  // so a name the tables do not carry at all is drift of its own.
  const drift = COLOURS.filter ((field) => skin.values[field] !== other[field]
                                           || skin.values[field] === undefined);
  check (drift.length === 0, skin.name + ': the two tables hold the same colours',
         drift.length === 0 ? COLOURS.length + ' fields'
                            : drift.map ((f) => f + ' web #' + skin.values[f] + ' native #' + other[f]).join (', '));
}

// --- what sits on what ------------------------------------------------------
// A pair with a `when` only exists in a skin that draws it: an engraved or a
// glowing panel prints its title in ink, a flat one prints it in textDim.
const PAIRS = [
  ['text', 'panel', 'text', 'the preset name, a menu row, a card title and its body (1682 1659 2672 2750)'],
  ['textDim', 'panel', 'text', 'the pitch names, the TIE SL AC VEL row labels, a flat panel title (2036 2084 1359)'],
  ['faceInk', 'face', 'text', 'the wordmark and the credit, printed on the cabinet (1388 2125)'],
  ['chipText', 'chip', 'text', 'a chip legend (1659)'],
  ['ink', 'panel', 'text', 'a panel title, engraved under its own highlight (1352)', (v) => v.engraved, 0.85],
  ['ink', 'panel', 'text', 'a panel title, lit on a glowing skin (1357)', (v) => v.glowing],
  ['accent', 'panel', 'text', 'the base row note name, and the arc of every knob (2035 1437)'],
  ['accentAlt', 'panel', 'text', 'the AC flag, the tie dash, the manual tempo (2073 2067 1693)'],
  ['pitch', 'panel', 'text', 'the SL flag (2071)'],
  ['gate', 'panel', 'mark', 'a closed gate cell (1991)'],
  ['playhead', 'panel', 'mark', 'the step strip above the grid (2027)'],
  ['pointer', 'bodyTop', 'mark', 'the pointer across the body of a knob (1543)'],
  ['meterGreen', 'well', 'mark', 'the meter at the bottom of its travel (1747)'],
  ['meterAmber', 'well', 'mark', 'the held peak line, the middle of the scale (1757 1750)'],
  ['meterRed', 'well', 'mark', 'the meter at the top, the gain-reduction strip (1758 1764)'],
];

for (const skin of skins) {
  const v = skin.values;
  // The cabinet a bare string is printed on: a flat skin paints background over
  // the whole face (app.js:1205), a textured one paints its base tone (3525).
  const face = v.faceMaterial === 'flat' ? v.background : v.faceBase;
  // The meter pocket: the lamp behind the scale (1727 1731).
  const well = over (v.spec, v.faceLo, 0.12);
  const surfaces = { face, well };
  console.log ('\n' + skin.name);
  for (const [fore, back, kind, what, when, alpha] of PAIRS) {
    if (when && ! when (v))
      continue;
    const a = v[fore], b = surfaces[back] || v[back];
    if (! a || ! b) { check (false, skin.name + ': ' + fore + ' on ' + back + ' is not a colour'); continue; }
    const got = ratio (alpha === undefined ? a : over (a, b, alpha), b);
    const need = kind === 'text' ? 4.5 : 3.0;
    const ok = got >= need;
    console.log ('  ' + (ok ? 'PASS  ' : 'FAIL  ') + got.toFixed (2).padStart (5) + ':1   '
                 + fore.padEnd (11) + 'on ' + back.padEnd (10) + what);
    if (! ok) failures.push (skin.name + ': ' + fore + ' on ' + back + ' is ' + got.toFixed (2) + ':1, needs ' + need);
  }
}

console.log ('');
if (failures.length > 0) {
  console.log (failures.length + ' PROBLEM(S) IN THE SKINS');
  process.exit (1);
}
console.log ('BOTH TABLES AGREE, AND EVERY PAIR CLEARS ITS LINE');
