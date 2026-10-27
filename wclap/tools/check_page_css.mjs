// A guard for the page chrome of the shipped interface.
//
//   node wclap/tools/check_page_css.mjs [web-dir]
//
// The interface is a canvas, so check_ui.mjs runs its code and looks at what it
// draws. This file looks at what is around the canvas: the files a browser reads
// before the plugin paints a single pixel. They are part of the bundle, and a
// host serves them straight out of the module's memory, so four things have to
// hold, and none of them is visible in a canvas check:
//
//  - the page brings nothing in from outside the bundle, because there is no
//    network where it runs, and the bundle carries nothing that is not the page;
//  - the letterbox around the canvas is the same colour as the panel it frames,
//    and both come from one table rather than from a literal typed twice;
//  - what a browser does with no wasm host - the text a user sees when the module
//    cannot run - is actually visible, and the one real DOM control of the page,
//    the field a double-click opens, can be seen to have the keyboard;
//  - the sheet says who checked it, so the next reader knows which rules it
//    answers to.
//
// The gates named in the comments are Hallmark's (the design skill the interface
// is checked against); `sloplint.mjs web --scope app` prints the same numbers, and
// this file keeps them from drifting back.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = process.argv[2]
  || path.join (path.dirname (fileURLToPath (import.meta.url)), '..', '..', 'web');

const html = fs.readFileSync (path.join (root, 'index.html'), 'utf8');
const css = fs.readFileSync (path.join (root, 'style.css'), 'utf8');
const js = fs.readFileSync (path.join (root, 'app.js'), 'utf8');

const failures = [];
const check = (ok, what, detail) => {
  console.log ((ok ? 'PASS  ' : 'FAIL  ') + what + (detail === undefined ? '' : '  [' + detail + ']'));
  if (!ok) failures.push (what);
};

// The stylesheet without its comments: a word in a comment is not a rule.
const code = css.replace (/\/\*[\s\S]*?\*\//g, '');
const blockOf = (selector) => {
  const at = code.indexOf (selector);
  return at < 0 ? '' : code.slice (at, code.indexOf ('}', at) + 1);
};

// ------------------------------------------------------------------ outside --

check (!/https?:\/\/|url\s*\(\s*['"]?\/|@import/.test (html + code),
       'the page asks for nothing outside its own bundle',
       'it runs from the memory of the plugin');

// ------------------------------------------------------------------ the skin --

// The Dark palette, read from the page's own table: the first entry of SKINS.
const darkTable = (js.match (/const SKINS = \[\s*\{([\s\S]*?)\}/) || [, ''])[1];
const skinValue = (name) => (darkTable.match (new RegExp (name + ":\\s*'#([0-9a-fA-F]{6})'")) || [, ''])[1];

const tokens = (code.match (/(:root|html)\s*\{([\s\S]*?)\}/) || [, '', ''])[2];
const tokenValue = (name) => {
  const at = tokens.indexOf ('--' + name);
  if (at < 0) return '';
  const value = tokens.slice (at).match (/:\s*([^;]+)/);
  return value ? value[1].trim () : '';
};

// Gate 48: no colour and no font-family below the token block.
const pastTokens = code.slice (tokens.length > 0 ? code.indexOf (tokens) + tokens.length : 0);
const strayColour = (pastTokens.match (/#[0-9a-fA-F]{3,8}\b/g) || []);
check (strayColour.length === 0, 'every colour of the page chrome goes through a token (48)',
       strayColour.join (', ') || 'none past the block');
check (!/font-family:\s*[^;]*Helvetica/.test (pastTokens),
       'the font of the page chrome is a token too (48)');

// The letterbox has to be the panel's own dark, from the table the canvas paints
// with - one decision, in one place.
check (tokenValue ('page-bg').toLowerCase () === '#' + skinValue ('faceLo').toLowerCase (),
       'the letterbox around the canvas is the Dark skin\'s own darkest face',
       tokenValue ('page-bg') + ' against faceLo #' + skinValue ('faceLo'));
check (tokenValue ('page-fg').toLowerCase () === '#' + skinValue ('text').toLowerCase (),
       'the fallback text is the Dark skin\'s own ink',
       tokenValue ('page-fg') + ' against text #' + skinValue ('text'));

// ------------------------------------------------------------------- geometry --

// Gate 34: hidden on the root clips what overflows to sticky positioning later.
const htmlBody = blockOf ('html, body') + blockOf ('html') + blockOf ('body');
check (/overflow-x:\s*clip/.test (htmlBody) && !/overflow(-x)?:\s*hidden/.test (htmlBody),
       'the root clips rather than hides its overflow (34)', htmlBody.trim ().slice (0, 60));
check (!/\b100(vw|vh)\b/.test (code), 'nothing is sized in viewport units (34)',
       (code.match (/\b100(vw|vh)\b/g) || []).join (', ') || 'no vw/vh');

// -------------------------------------------------------------------- people --

// Gate 33: a canvas is nothing at all to a screen reader unless it is named.
const canvas = (html.match (/<canvas[^>]*>/i) || [''])[0];
check (/aria-label=|aria-hidden=/.test (canvas), 'the canvas says what it is (33)', canvas);

// The message for a host that cannot run the module has to be readable before
// any script has had a turn: visible in the markup, hidden by the page once it
// paints. A `hidden` attribute with nothing that removes it shows a black window.
const fallback = (html.match (/<div id="fallback"[^>]*>/i) || [''])[0];
check (!/hidden/.test (fallback), 'the fallback message is visible with scripting off (27)', fallback.trim ());
check (/getElementById\s*\(\s*['"]fallback['"]/.test (js),
       'and the page hides it once the instrument paints', 'app.js touches #fallback');

// Gate 26a: the one DOM control of the page is the field a double-click opens.
check (/:focus-visible/.test (code), 'the value field shows the keyboard where it is (26a)');
check (!/outline:\s*(none|0)\b/.test (code), 'and nothing takes the ring away without giving one back (26a)');

// Gate 27 with nothing to slow down: the page moves only when the plugin plays.
const motion = (code.match (/(transition|animation)\s*:/g) || []);
check (motion.length === 0, 'no decorative motion to disable for reduced motion (27)',
       motion.join (', ') || 'no transition or animation in the sheet');

// Gate 20: the stamp is the first non-empty line, so a reader knows the sheet
// was checked rather than guessed at.
const firstLine = css.split ('\n').find ((line) => line.trim ().length > 0) || '';
check (/^\/\* Hallmark /.test (firstLine), 'the stylesheet states who checked it (20)', firstLine.slice (0, 70));

// ------------------------------------------------------------------ the bundle --

// CMake copies web/ into the bundle as it stands, and embed_web.py turns what it
// finds into memory of the module: one stray screenshot, and every host that
// opens the plugin carries it. Design artifacts belong outside the shipped page.
const shipped = ['app.js', 'index.html', 'style.css'];
const strays = fs.readdirSync (root).filter ((name) => !shipped.includes (name));
check (strays.length === 0, 'the interface directory holds only the three shipped files',
       strays.join (', ') || shipped.join (', '));

console.log ('');
if (failures.length > 0) {
  console.log (failures.length + ' CHECK(S) FAILED: ' + root);
  process.exit (1);
}
console.log ('ALL PAGE CHECKS PASSED');
