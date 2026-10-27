// A smoke test for the wasm page.
//
// The interface is a canvas, so the check runs the page's own code against a
// recording canvas: it answers a state message, clicks where a user would
// click, and looks at what the page drew and what it sent back. It needs
// nothing but node, so it runs in CI.
//
//   node check_ui.mjs <state.json> [web-dir]

import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const [statePath, webDir] = process.argv.slice (2);
if (!statePath) {
  console.error ('usage: node check_ui.mjs <state.json> [web-dir]');
  process.exit (2);
}

const root = webDir || path.join (path.dirname (fileURLToPath (import.meta.url)), '..', '..', 'web');
const DESIGN = { w: 1180, h: 780 };

// --- the recording canvas -----------------------------------------------------
const calls = {};
const shapes = [];
const transforms = [];
const count = (name) => { calls[name] = (calls[name] || 0) + 1; };

const context2d = new Proxy ({}, {
  get (target, property) {
    if (property === 'canvas') return { width: DESIGN.w, height: DESIGN.h };
    if (property === 'measureText')
      return (text) => ({ width: String (text).length * 6 });
    if (property === 'createLinearGradient' || property === 'createRadialGradient')
      return () => ({ addColorStop () {} });
    if (property === 'setTransform')
      return (...args) => { transforms.push (args); };
    if (property in target)
      return target[property];

    return (...args) => {
      count (String (property));
      if (property === 'fillRect' || property === 'strokeRect' || property === 'rect')
        shapes.push ({ kind: property, x: args[0], y: args[1], w: args[2], h: args[3] });
      else if (property === 'fillText' || property === 'strokeText')
        shapes.push ({ kind: property, x: args[1], y: args[2], text: String (args[0]),
                       size: Number (String (target.font || '').match (/([\d.]+)px/) ?. [1] || 10) });
      else if (property === 'arc' || property === 'ellipse')
        shapes.push ({ kind: 'arc', x: args[0], y: args[1], r: args[2] });
      else if (property === 'moveTo' || property === 'lineTo')
        shapes.push ({ kind: 'point', x: args[0], y: args[1] });
    };
  },
  set (target, property, value) { target[property] = value; return true; },
});

const listeners = {};
const store = new Map ();
// A material skin asks the page to build its grain on an offscreen canvas;
// the recording context is the one under test, so an offscreen one stays
// silent and nothing extra lands in shapes.
const silentContext = new Proxy ({}, {
  get (target, property) {
    if (property === 'canvas') return { width: DESIGN.w, height: DESIGN.h };
    if (property === 'createLinearGradient' || property === 'createRadialGradient')
      return () => ({ addColorStop () {} });
    if (property in target) return target[property];
    return () => {};
  },
  set (target, property, value) { target[property] = value; return true; },
});
const element = (tag) => ({
  tag, style: {}, type: '', files: [], href: '', download: '',
  addEventListener: (name, fn) => { (listeners['element:' + tag + ':' + name] ||= []).push (fn); },
  appendChild: () => {}, click: () => { count ('element-click'); }, focus: () => {},
  select: () => {}, getBoundingClientRect: () => ({ left: 0, top: 0 }),
  getContext: () => silentContext,
});

// A drag holds the pointer: with a real browser the canvas keeps getting the
// moves even when the mouse leaves the panel. The stub says what it captured, so
// a page that never asked for the capture shows up as one that drops its drags.
const captured = new Set ();
const canvas = {
  ...element ('canvas'),
  width: DESIGN.w, height: DESIGN.h, clientWidth: DESIGN.w, clientHeight: DESIGN.h,
  getContext: () => context2d,
  addEventListener: (name, fn) => { (listeners['canvas:' + name] ||= []).push (fn); },
  getBoundingClientRect: () => ({ left: 0, top: 0, width: DESIGN.w, height: DESIGN.h }),
  setPointerCapture: (id) => { captured.add (id); },
  releasePointerCapture: (id) => { captured.delete (id); },
  hasPointerCapture: (id) => captured.has (id),
};

const posted = [];
const context = {
  console, Math, JSON, Date, Object, Array, String, Number, Boolean, Error, Map, Set,
  parseInt, parseFloat, isNaN, Infinity, NaN, TextEncoder, TextDecoder,
  ArrayBuffer, Uint8Array, Int16Array, DataView, Promise,
  atob: (text) => Buffer.from (text, 'base64').toString ('binary'),
  performance: { now: () => 0 },
  requestAnimationFrame: () => 0,
  setInterval: () => 0, clearInterval: () => {},
  setTimeout: () => {},
  URL: { createObjectURL: () => 'blob:', revokeObjectURL: () => {} },
  Blob: class { constructor (parts) { this.parts = parts; } },
  document: {
    getElementById: (id) => (id === 'ui' ? canvas : element (id)),
    createElement: element,
    body: { appendChild: () => {} },
    activeElement: null,
  },
  window: {
    innerWidth: DESIGN.w, innerHeight: DESIGN.h, devicePixelRatio: 1,
    addEventListener: (name, fn) => { (listeners['window:' + name] ||= []).push (fn); },
    parent: { postMessage: (buffer) => posted.push (JSON.parse (new TextDecoder ().decode (new Uint8Array (buffer)))) },
    localStorage: { getItem: (k) => (store.has (k) ? store.get (k) : null),
                    setItem: (k, v) => store.set (k, String (v)),
                    removeItem: (k) => store.delete (k) },
  },
};
context.window.window = context.window;
context.globalThis = context;
vm.createContext (context);

const failures = [];
const check = (ok, what, detail) => {
  console.log ((ok ? 'PASS  ' : 'FAIL  ') + what + (detail === undefined ? '' : '  [' + detail + ']'));
  if (! ok) failures.push (what);
};

try {
  vm.runInContext (fs.readFileSync (path.join (root, 'app.js'), 'utf8'), context, { filename: 'app.js' });
} catch (error) {
  console.log ('FAIL  the page loads: ' + error.stack);
  process.exit (1);
}

const fire = (key, event) => { for (const fn of (listeners[key] || [])) fn (event); };
const feed = (message) => fire ('window:message',
  { data: new TextEncoder ().encode (JSON.stringify (message)).buffer });

// A fixture holds the two messages a hello is answered with, the bank first;
// an older single-object dump still works.
const fixture = JSON.parse (fs.readFileSync (statePath, 'utf8'));
const messages = Array.isArray (fixture) ? fixture : [fixture];
const state = messages.find ((m) => m.t === 'state') || messages[0];
const bank = messages.find ((m) => m.t === 'presets') || { names: [], credits: [] };
const setState = (override) => feed (Object.assign ({}, state, override));

check (true, 'the page loads');

for (const message of messages) {
  try {
    feed (message);
  } catch (error) {
    console.log ('FAIL  a plugin message threw: ' + error.stack);
    process.exit (1);
  }
}
check (true, 'the page takes the state and the bank');
const L = context.window.__layout;
check (L !== undefined, 'the page publishes its layout');

// --- the design surface -------------------------------------------------------
const drawn = () => shapes.filter ((s) => Number.isFinite (s.x) && Number.isFinite (s.y));
check (drawn ().every ((s) => ! Number.isNaN (s.x) && ! Number.isNaN (s.y)),
       'nothing is drawn at NaN');

const outside = drawn ().filter ((s) => {
  if (s.kind === 'fillText' || s.kind === 'strokeText' || s.kind === 'arc' || s.kind === 'point')
    return s.x < -0.5 || s.x > DESIGN.w + 0.5 || s.y < -0.5 || s.y > DESIGN.h + 0.5;
  return s.kind !== 'rect'
    && (s.x < -0.5 || s.y < -0.5 || s.x + s.w > DESIGN.w + 0.5 || s.y + s.h > DESIGN.h + 0.5);
});
for (const s of outside.slice (0, 8)) console.log ('      stray: ' + JSON.stringify (s));
check (outside.length === 0, 'nothing is drawn outside the cabinet', outside.length + ' stray');

check ((calls.arc || 0) > 20, 'the knobs are drawn as arcs', calls.arc);
check ((calls.fillText || 0) > 60, 'the labels are written', calls.fillText);

// --- the layout tables --------------------------------------------------------
check (L.knobs.length === 18, 'the native knob rack is on the page', L.knobs.length);
check (L.gateCells.length === 16, 'the gate is a grid of sixteen squares', L.gateCells.length);
check (L.flagRows.length === 4 && L.flagRows.map ((r) => r.label).join () === 'TIE,SL,AC,VEL',
       'the flag rows are TIE, SL, AC and VEL');
check (L.faders.length === 5, 'the FX strip has its five faders', L.faders.length);
check (L.combos.length === 8 && L.seqCombos.length === 3, 'the dropdowns are placed',
       L.combos.length + '+' + L.seqCombos.length);
check (L.buttons.length === 6 && L.actions.length === 4, 'the toggles and bank buttons are placed');

// The native gate panel is 276x190 divided into four columns and four rows,
// each cell reduced by the 3 px gap (GateCircleComponent::cellBounds).
const cols = new Set (L.gateCells.map ((c) => c.x.toFixed (1))).size;
const rows = new Set (L.gateCells.map ((c) => c.y.toFixed (1))).size;
const sameSize = new Set (L.gateCells.map ((c) => c.w.toFixed (1) + 'x' + c.h.toFixed (1))).size;
check (cols === 4 && rows === 4 && sameSize === 1 && L.gateCells[0].w > 40,
       'the gate cells tile the panel in four by four', cols + 'x' + rows + ' ' + sameSize);
const cell = { w: (276 - 2) / 4 - 6, h: (190 - 2) / 4 - 6 };
check (Math.abs (L.gateCells[0].w - cell.w) < 0.1 && Math.abs (L.gateCells[0].h - cell.h) < 0.1,
       'a gate cell matches the native one', L.gateCells[0].w.toFixed (1) + 'x' + L.gateCells[0].h.toFixed (1));

// Nothing round is left in the panel: the wheel used to draw its sectors there.
const gatePanel = { x: 888, y: 80, w: 276, h: 190 };
const inGate = (s) => s.x >= gatePanel.x && s.x <= gatePanel.x + gatePanel.w
  && s.y >= gatePanel.y && s.y <= gatePanel.y + gatePanel.h;
check (! shapes.filter ((s) => s.kind === 'arc').some (inGate), 'the gate panel holds no wheel');

// Labels must not land on a neighbour: every knob's two text rows stay in its cell.
const cellClash = L.knobs.filter ((entry) => {
  const value = entry.valueRect;
  return value.y + value.h > entry.cell.y + entry.cell.h + 0.5;
});
check (cellClash.length === 0, 'the knob labels stay inside their cell', cellClash.length);

const collide = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
const textRects = [];
for (const entry of L.knobs) { textRects.push (entry.nameRect, entry.valueRect); }
let overlaps = 0;
for (let i = 0; i < textRects.length; ++i)
  for (let j = i + 1; j < textRects.length; ++j)
    if (collide (textRects[i], textRects[j])) ++overlaps;
check (overlaps === 0, 'no label sits on another label', overlaps);

const onTopOfAControl = L.knobs.filter ((entry) =>
  L.combos.some ((c) => collide (entry.valueRect, c.rect))
  || L.seqCombos.some ((c) => collide (entry.valueRect, c.rect)));
check (onTopOfAControl.length === 0, 'no knob label sits on a dropdown', onTopOfAControl.length);

// The pattern grid keeps its own gutter, and the flags sit below the pitches.
check (L.grid.x > 16, 'the grid starts after the note gutter', L.grid.x);
check (L.flagRows[0].y >= L.pitchBottom, 'the flag rows sit below the pitch rows');

// --- gestures -----------------------------------------------------------------
const at = (x, y, extra) => Object.assign ({ clientX: x, clientY: y, button: 0, pointerId: 1,
  shiftKey: false, preventDefault () {}, deltaY: 0 }, extra);
// A press and a release at the same point: the page paints on a drag only when
// the pointer actually moved, so a click that never moved must not paint twice.
const click = (x, y, extra) => {
  fire ('canvas:pointerdown', at (x, y, extra));
  fire ('canvas:pointerup', at (x, y, extra));
};
const lastSent = (type) => {
  for (let i = posted.length - 1; i >= 0; --i) if (posted[i].t === type) return posted[i];
  return null;
};
const stepsOf = (m) => m.text.split (':')[3].split (',').map (Number);
const velOf = (m) => m.text.split (':')[4].split (',').map (Number);

const NUM_STEPS = 16;
const STEPS = Array.from ({ length: NUM_STEPS }, () => 0).join (',');
setState ({ seq: 'v1:36:1:' + STEPS, gate: 'g1:0000:0:16' });

// The bank buttons run their action; they must not be mistaken for a parameter
// toggle (a toggle would send a 'param' message under the button's own id).
{
  const bankButton = L.actions.find ((a) => a.id === 'clear').rect;
  // Whatever an earlier check left open (a menu, a card, a dialog) would take
  // the press itself; Escape closes all three.
  fire ('window:keydown', { key: 'Escape', preventDefault () {} });
  posted.length = 0;
  click (bankButton.x + bankButton.w / 2, bankButton.y + bankButton.h / 2);
  const toggles = posted.filter ((m) => m.t === 'param' && m.id === 'clear');
  check (toggles.length === 0, 'a bank button runs its action instead of toggling a parameter');
  // Clear asks for confirmation; dismiss it so the checks below see a free canvas.
  fire ('window:keydown', { key: 'Escape', preventDefault () {} });
  posted.length = 0;
}

posted.length = 0;
const cellCentre = { x: L.grid.x + L.grid.w / NUM_STEPS / 2 };
click (cellCentre.x, L.pitchBottom - L.rowH / 2);
const painted = lastSent ('seq');
check (painted !== null && stepsOf (painted)[0] === 0x80, 'a click paints the base note',
       painted && stepsOf (painted)[0]);
click (cellCentre.x, L.pitchBottom - L.rowH / 2);
check (stepsOf (lastSent ('seq'))[0] === 0, 'a second click erases it');

// The flag rows: TIE on a rest, then SL and AC on a note.
click (cellCentre.x, L.flagRows[0].y + L.rowH / 2);
check (stepsOf (lastSent ('seq'))[0] === 1, 'the TIE row writes a tie');
click (cellCentre.x, L.pitchBottom - L.rowH / 2);
click (cellCentre.x, L.flagRows[1].y + L.rowH / 2);
check (stepsOf (lastSent ('seq'))[0] === 0x80 + 0x100, 'the SL row adds a slide');
click (cellCentre.x, L.flagRows[2].y + L.rowH / 2);
check (stepsOf (lastSent ('seq'))[0] === 0x80 + 0x100 + 0x200, 'the AC row adds an accent');

// The gate: one square flips, and the mask travels to the plugin.
const gateBit = (message) => parseInt (message.text.split (':')[1], 16);
posted.length = 0;
click (L.gateCells[5].x + 2, L.gateCells[5].y + 2);
check (gateBit (lastSent ('gate')) === (1 << 5), 'a gate square closes', lastSent ('gate')?.text);
click (L.gateCells[5].x + 2, L.gateCells[5].y + 2);
check (gateBit (lastSent ('gate')) === 0, 'a second click opens it again');

// --- the menus, anchored where their control is -------------------------------
const openPresetMenu = () => { click (L.header.presetField.x + 4, L.header.presetField.y + 4, { button: 2 }); };
shapes.length = 0;
openPresetMenu ();
const menuTexts = shapes.filter ((s) => s.kind === 'fillText').map ((s) => s.text);
check (menuTexts.some ((t) => t === bank.names[0]), 'the bank menu lists the factory presets');
const firstRow = shapes.find ((s) => s.kind === 'fillText' && s.text === bank.names[0]);
check (firstRow !== undefined && firstRow.y > L.header.presetField.y,
       'the bank menu opens under the preset field', firstRow && firstRow.y.toFixed (1));

posted.length = 0;
click (firstRow.x + 4, firstRow.y - 4);
check (lastSent ('preset')?.name === bank.names[0], 'a row loads that preset');
check (shapes.length > 0 && posted.length > 0, 'the menu closed after the pick');

// The credit column: the native editor keeps the attribution of the preset in
// play at 884..1168 × 470..492, two wrapped lines.
const credited = bank.names.findIndex ((n, i) => (bank.credits[i] || '') !== '');
shapes.length = 0;
setState ({ preset: credited >= 0 ? bank.names[credited] : 'Init' });
const inCreditColumn = (s) => s.x >= 882 && s.x <= 1170 && s.y >= 460 && s.y <= 500;
check (credited >= 0 && shapes.some ((s) => s.kind === 'fillText' && inCreditColumn (s)),
       'the credit of the loaded preset shows in its column');

shapes.length = 0;
click (L.header.logo.x + 4, L.header.logo.y + 4);
const infoTexts = shapes.filter ((s) => s.kind === 'fillText').map ((s) => s.text);
check (infoTexts.some ((t) => t.indexOf ('About') === 0) && infoTexts.some ((t) => t.indexOf ('User manual') === 0),
       'the logo opens the info menu');
const aboutRow = shapes.find ((s) => s.kind === 'fillText' && s.text.indexOf ('About') === 0);
check (Math.abs (aboutRow.y - (L.header.logo.y + L.header.logo.h)) < 30,
       'the info menu is anchored at the logo', aboutRow.y.toFixed (1));

click (aboutRow.x + 2, aboutRow.y - 6);
const aboutLine = (pattern) => shapes.some ((s) => s.kind === 'fillText' && pattern.test (s.text));
check (aboutLine (/konstantin\.qi/), 'the about card shows the author');
shapes.length = 0;
fire ('window:keydown', { key: 'Escape', preventDefault () {} });
check (! aboutLine (/konstantin\.qi/), 'Escape closes the card');

// A combo opens its own list, at its own rectangle.
shapes.length = 0;
const sync = L.combos.find ((c) => c.id === 'Sync');
click (sync.rect.x + 4, sync.rect.y + 4);
const choiceTexts = shapes.filter ((s) => s.kind === 'fillText').map ((s) => s.text);
check (choiceTexts.includes ('Manual') || choiceTexts.includes ('manual'),
       'the sync combo lists its choices', choiceTexts.slice (0, 6).join ('|'));
const manualRow = shapes.find ((s) => s.kind === 'fillText' && /^manual$/i.test (s.text));
check (manualRow !== undefined && Math.abs (manualRow.x - sync.rect.x) < 60,
       'the combo list sits on the combo', manualRow && manualRow.x.toFixed (1));
posted.length = 0;
click (manualRow.x + 2, manualRow.y - 6);
check (lastSent ('param')?.id === 'Sync', 'picking a choice writes the parameter');

// The skin combo sits at the top of the header, and its list opens centred on
// the row in play: while Metal is chosen the earlier skins would be carried
// above the canvas, where no pointer reaches them.
{
  const skinCombo = L.combos.find ((c) => c.id === 'UiSkin');
  setState ({ params: Object.assign ({}, state.params, { UiSkin: 3 }), skin: 3 });
  shapes.length = 0;
  click (skinCombo.rect.x + 4, skinCombo.rect.y + 4);
  const darkRow = shapes.find ((s) => s.kind === 'fillText' && s.text === 'Dark');
  check (darkRow !== undefined && darkRow.y > 0,
         'the last skin still lists the first one where it can be clicked',
         darkRow && darkRow.y.toFixed (1));
  posted.length = 0;
  click (darkRow.x + 2, darkRow.y - 6);
  check (lastSent ('param')?.id === 'UiSkin' && lastSent ('param').v === 0,
         'the previous skin is selectable', JSON.stringify (lastSent ('param')));
  setState ({});
}

// A step menu from the right click on the grid.
shapes.length = 0;
click (cellCentre.x, L.pitchBottom - L.rowH / 2, { button: 2 });
const stepTexts = shapes.filter ((s) => s.kind === 'fillText').map ((s) => s.text);
check (stepTexts.some ((t) => t.indexOf ('Tie') === 0) && stepTexts.includes ('Rest'),
       'the step menu offers the flags');
const restRow = shapes.find ((s) => s.kind === 'fillText' && s.text === 'Rest');
posted.length = 0;
click (restRow.x + 2, restRow.y - 6);
check (stepsOf (lastSent ('seq'))[0] === 0, 'a step-menu row writes its flag');

// The header marks what the page changed, the way isProgramDirty() does.
shapes.length = 0;
click (cellCentre.x, L.pitchBottom - L.rowH * 3.5);
check (shapes.some ((s) => s.kind === 'fillText' && s.text === 'User*'),
       'an edit turns the field into User*');

// --- the knobs ----------------------------------------------------------------
const cutoff = L.knobs.find ((k) => k.knob.id === 'Cutoff');
posted.length = 0;
fire ('canvas:wheel', at (cutoff.rect.x + cutoff.rect.w / 2, cutoff.rect.y + cutoff.rect.h / 2,
                          { deltaY: -1 }));
const wheelStep = lastSent ('param');
// One notch is one per cent of the range in the parameter's own units, as the
// native KnobSlider steps it.
check (wheelStep?.id === 'Cutoff'
       && Math.abs (wheelStep.v - (state.params.Cutoff + (8000 - 40) * 0.01)) < 0.01,
       'the wheel nudges the knob under it', wheelStep && wheelStep.v);

const bpm = L.knobs.find ((k) => k.knob.id === 'BPM');
posted.length = 0;
fire ('canvas:wheel', at (bpm.rect.x + bpm.rect.w / 2, bpm.rect.y + bpm.rect.h / 2, { deltaY: -1 }));
check (lastSent ('param') === null, 'a greyed knob ignores the wheel');
posted.length = 0;
fire ('canvas:dblclick', at (cutoff.rect.x + 4, cutoff.rect.y + 4));
shapes.length = 0;
for (const ch of '555')
  fire ('window:keydown', { key: ch, preventDefault () {} });
fire ('window:keydown', { key: 'Enter', preventDefault () {} });
check (lastSent ('param')?.id === 'Cutoff' && Math.abs (lastSent ('param').v - 555) < 1,
       'a double-click takes an exact value', lastSent ('param')?.v);

// Dragging a knob moves it in the parameter's own direction: up is more, and
// two hundred pixels are the whole range. The page used to port JUCE's velocity
// curve, under which three pixels were worth a tenth of a per cent.
const wave = L.knobs.find ((k) => k.knob.id === 'Wave');
const waveRange = state.ranges.Wave || { min: 0, max: 1 };
const before = state.params.Wave;
posted.length = 0;
fire ('canvas:pointerdown', at (wave.rect.x + wave.rect.w / 2, wave.rect.y + wave.rect.h / 2));
check (captured.has (1), 'the drag captures the pointer, so it survives the edge of the panel');
for (let dy = 10; dy <= 200; dy += 10)
  fire ('canvas:pointermove', at (wave.rect.x + wave.rect.w / 2, wave.rect.y + wave.rect.h / 2 - dy));
fire ('canvas:pointerup', at (0, 0));
const dragged = lastSent ('param');
check (dragged?.id === 'Wave' && dragged.v > before, 'dragging up opens the knob', dragged?.v);
check (Math.abs (dragged.v - waveRange.max) < 1e-6,
       'two hundred pixels are the whole range', dragged?.v + ' of ' + waveRange.max);
check (! captured.has (1), 'the release gives the pointer back');

// The hand movement is one gesture: the host wraps a knob turn in a pair of
// events, or the automation lane fills with a point per mouse move.
{
  const gestures = posted.filter ((m) => m.t === 'gesture' && m.id === 'Wave');
  const first = posted.findIndex ((m) => m.t === 'gesture' && m.id === 'Wave');
  const values = posted.map ((m, i) => ({ m, i })).filter ((e) => e.m.t === 'param' && e.m.id === 'Wave');
  const lastGesture = posted.map ((m, i) => ({ m, i })).filter ((e) => e.m.t === 'gesture').pop ();
  check (gestures.length === 2 && gestures[0].begin === true && gestures[1].begin === false,
         'a drag opens and closes its gesture', gestures.length);
  check (values.every ((e) => e.i > first && e.i < lastGesture.i),
         'every value sits inside the gesture');
}

// A small move still moves: three pixels must be worth a hundredth of a range.
posted.length = 0;
fire ('canvas:pointerdown', at (wave.rect.x + wave.rect.w / 2, wave.rect.y + wave.rect.h / 2));
fire ('canvas:pointermove', at (wave.rect.x + wave.rect.w / 2 + 3, wave.rect.y + wave.rect.h / 2));
const nudge = lastSent ('param');
fire ('canvas:pointerup', at (0, 0));
const nudgeSpan = waveRange.max - waveRange.min;
check (Math.abs (nudge.v - (before + nudgeSpan * 3 / 200)) < nudgeSpan * 0.005,
       'three pixels move the knob by three two-hundredths', nudge.v);

// Shift is the fine pass: five times less for the same distance.
posted.length = 0;
fire ('canvas:pointerdown', at (wave.rect.x + wave.rect.w / 2, wave.rect.y + wave.rect.h / 2));
fire ('canvas:pointermove', at (wave.rect.x + wave.rect.w / 2 + 3, wave.rect.y + wave.rect.h / 2,
                                { shiftKey: true }));
const fine = lastSent ('param');
fire ('canvas:pointerup', at (0, 0));
check (Math.abs (fine.v - (before + nudgeSpan * 3 / 1000)) < nudgeSpan * 0.002,
       'Shift drags the knob five times finer', fine.v);

// The response plot is a control: a drag right raises the cutoff.
const plot = context.window.__layout.filterPlot;
posted.length = 0;
fire ('canvas:pointerdown', at (plot.x + plot.w / 2, plot.y + plot.h / 2));
fire ('canvas:pointermove', at (plot.x + plot.w / 2 + 40, plot.y + plot.h / 2));
fire ('canvas:pointerup', at (0, 0));
const plotted = posted.filter ((m) => m.t === 'param' && m.id === 'Cutoff').pop ();
check (plotted !== undefined && plotted.v > state.params.Cutoff,
       'dragging the plot right opens the filter', plotted?.v);
check (posted.some ((m) => m.t === 'gesture' && m.id === 'Res' && m.begin),
       'the plot drags resonance inside a gesture too');

// --- the keyboard -------------------------------------------------------------
// The knob under the pointer answers the arrows, one interval a press.
fire ('canvas:pointermove', at (wave.rect.x + wave.rect.w / 2, wave.rect.y + wave.rect.h / 2));
posted.length = 0;
fire ('window:keydown', { key: 'ArrowUp', preventDefault () {} });
check (lastSent ('param')?.id === 'Wave', 'the arrows step the knob under the pointer');

// Play belongs to the manual clock: under Sync = Host the host's transport is the
// run line, and the page must not send a command that nothing obeys.
{
  const hostSync = Object.assign ({}, state, { params: Object.assign ({}, state.params, { Sync: 0 }) });
  feed (hostSync);
  posted.length = 0;
  fire ('window:keydown', { key: ' ', preventDefault () {} });
  check (lastSent ('param') === null, 'the space bar does nothing under Sync = Host');

  const manual = Object.assign ({}, state, { params: Object.assign ({}, state.params, { Sync: 1 }) });
  feed (manual);
  posted.length = 0;
  fire ('window:keydown', { key: ' ', preventDefault () {} });
  check (lastSent ('param')?.id === 'Play', 'the space bar runs the sequencer in Manual');
  feed (state);
}

posted.length = 0;
fire ('window:keydown', { key: 'ArrowRight', preventDefault () {} });
check (lastSent ('preset') !== null, 'the arrows in the header leaf through the bank');

// --- the velocity of a note bar ------------------------------------------------
const mid = (n) => Array.from ({ length: NUM_STEPS }, (_, i) => (i === 0 ? n : 0)).join (',');
setState ({ seq: 'v2:36:1:' + mid (128) + ':' + mid (64) });
const noteY = L.pitchBottom - L.rowH / 2;

// A drag that stays inside the row rides the bar: the note keeps its pitch.
posted.length = 0;
fire ('canvas:pointerdown', at (cellCentre.x, noteY));
fire ('canvas:pointermove', at (cellCentre.x, noteY - 4));
fire ('canvas:pointermove', at (cellCentre.x, noteY - 6));
fire ('canvas:pointerup', at (cellCentre.x, noteY - 6));
const ridden = lastSent ('seq');
check (ridden !== null && stepsOf (ridden)[0] === 128 && velOf (ridden)[0] > 64,
       'dragging a note bar raises its velocity', ridden && velOf (ridden)[0]);

// A drag that leaves the row paints instead, the way a sweep of the pencil does.
setState ({ seq: 'v1:36:1:' + mid (128) });
posted.length = 0;
fire ('canvas:pointerdown', at (cellCentre.x, noteY));
fire ('canvas:pointermove', at (cellCentre.x, noteY - 40));
fire ('canvas:pointerup', at (cellCentre.x, noteY - 40));
const swept = lastSent ('seq');
check (swept !== null && stepsOf (swept)[0] > 128, 'a drag out of the row moves the note',
       swept && stepsOf (swept)[0]);

// --- the bank the page keeps itself -------------------------------------------
// The factory bank has over a hundred rows, so the menu is taller than the
// window and scrolls: reach a row the way a user does, with the wheel.
function rowAfterScroll (text) {
  for (let i = 0; i < 300; ++i) {
    shapes.length = 0;
    fire ('canvas:wheel', at (300, 400, { deltaY: 1 }));
    const row = shapes.find ((s) => s.kind === 'fillText' && s.text === text);
    if (row !== undefined)
      return row;
  }
  return undefined;
}

shapes.length = 0;
openPresetMenu ();
const saveRow = rowAfterScroll ('Save current settings as preset...');
check (saveRow !== undefined, 'the wheel scrolls a menu taller than the window');
click (saveRow.x + 2, saveRow.y - 6);
for (const ch of 'My Line')
  fire ('window:keydown', { key: ch, preventDefault () {} });
fire ('window:keydown', { key: 'Enter', preventDefault () {} });
const saved = JSON.parse (context.window.localStorage.getItem ('spiral303.userPresets') || '{}');
check (saved['My Line'] !== undefined && JSON.parse (saved['My Line']).name === 'My Line',
       'a saved preset is kept as the native file format');

shapes.length = 0;
click (L.header.presetField.x + 4, L.header.presetField.y + 4, { button: 2 });
const userRow = rowAfterScroll ('My Line');
check (userRow !== undefined, 'the bank menu lists what the page saved');
posted.length = 0;
click (userRow.x + 2, userRow.y - 6);
check (lastSent ('presetText')?.json.indexOf ('"My Line"') > 0,
       'a stored preset goes back to the plugin as text');

// --- what the plugin answers ---------------------------------------------------
// A mouse move must not cost the whole state: the ranges, the choices and the
// version never change, so they travel with hello and nothing else.
{
  const page = context.window.__state;
  const stripped = { t: 'state', params: Object.assign ({}, page.params, { Drive: 0.4 }),
                     seq: page.seq, gate: page.gate, preset: page.preset, note: page.note,
                     skin: page.skin, curve: [1, 2, 3] };
  feed (stripped);
  check (page.params.Drive === 0.4, 'a state without the statics still writes its values');

  shapes.length = 0;
  click (sync.rect.x + 4, sync.rect.y + 4);
  check (shapes.some ((s) => s.kind === 'fillText' && /^manual$/i.test (s.text)),
         'the dropdowns keep their lists after a state without the choices', shapes.length);
  fire ('window:keydown', { key: 'Escape', preventDefault () {} });

  feed ({ t: 'param', id: 'Cutoff', v: 1234, curve: [0, 1, 2] });
  check (page.params.Cutoff === 1234 && page.curve.length === 3,
         'one parameter comes back as one value, with its curve for the filter');

  // The host moved these while the page was not looking: the meter carries them.
  const hostMoved = Object.assign ({}, state, { params: Object.assign ({}, state.params, { Sync: 0 }) });
  feed (hostMoved);
  shapes.length = 0;
  feed ({ t: 'meter', peak: 0.5, rms: 0.2, gr: 1, step: 3, sector: 0, running: true,
          tempo: 142.5, params: { Drive: 0.7, Wave: 0.25 } });
  check (page.params.Drive === 0.7 && page.params.Wave === 0.25,
         'the meter carries what the host moved');
  check (shapes.some ((s) => s.kind === 'fillText' && /142\.5 BPM\s+host/.test (s.text)),
         'the header prints the host tempo, not the knob', shapes.map ((s) => s.text).find ((t) => /BPM/.test (t || '')));
  feed (state);
}

// --- the window stretches -----------------------------------------------------
context.window.innerWidth = 1770;
context.window.innerHeight = 1170;
canvas.clientWidth = 1770;
canvas.clientHeight = 1170;
canvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 1770, height: 1170 });
shapes.length = 0;
transforms.length = 0;
fire ('window:resize', {});
const last = transforms[transforms.length - 1];
check (last !== undefined && Math.abs (last[0] - 1.5) < 0.01,
       'a bigger window draws the same layout scaled up', last && last[0]);
check (context.window.__layout.grid.x === L.grid.x, 'the layout stays in design units');

// --- MIDI export ----------------------------------------------------------------
// The writer is the page's twin of Source/dsp/MidiExport.h: a standard file with
// one track, the tempo as a meta event, and every note-on closed by a note-off.
{
  const bpm = (context.window.__state.params || {}).BPM || 120;
  const bytes = context.window.__midi.build (bpm, 0.25);
  const text = (from, len) => String.fromCharCode (...bytes.slice (from, from + len));
  const u32 = (at) => (bytes[at] << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8 | bytes[at + 3]) >>> 0;
  check (text (0, 4) === 'MThd' && u32 (4) === 6, 'the MIDI export starts with a header chunk');
  check (bytes[8] === 0 && bytes[9] === 0 && bytes[10] === 0 && bytes[11] === 1 && bytes[12] === 1 && bytes[13] === 0xe0,
         'the MIDI export is format 0, one track, 480 ticks to the quarter');
  check (text (14, 4) === 'MTrk' && u32 (18) === bytes.length - 22, 'the track chunk covers the whole file');
  // The first event is the tempo meta at delta 0: 00 FF 51 03 tt tt tt (7 bytes).
  const tempoAt = 22;
  const usPerQuarter = (bytes[tempoAt + 4] << 16) | (bytes[tempoAt + 5] << 8) | bytes[tempoAt + 6];
  check (bytes[tempoAt + 1] === 0xff && bytes[tempoAt + 2] === 0x51 && Math.abs (60000000 / usPerQuarter - bpm) < 0.5,
         'the tempo meta carries the BPM the page runs at');
  let ons = 0, offs = 0;
  for (let i = tempoAt + 7; i < bytes.length - 2; ++i) {
    if (bytes[i] === 0x90 && bytes[i + 2] > 0) ++ons;
    if (bytes[i] === 0x80) ++offs;
  }
  check (ons > 0 && ons === offs, 'every note-on has its note-off (' + ons + ' notes)');
  check (text (bytes.length - 3, 3) === '\xff\x2f\x00', 'the track ends with its end-of-track meta');
}


console.log ('');
if (failures.length > 0) {
  console.log (failures.length + ' CHECK(S) FAILED');
  process.exit (1);
}
console.log ('ALL UI CHECKS PASSED');
