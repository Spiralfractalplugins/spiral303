// The Spiral303 interface.
//
// This page is a port of the native editor, not a reinterpretation of it. Every
// rectangle, colour and caption comes from Source/PluginEditor.cpp,
// Source/gui/Palette.h and the components in Source/gui/, so the same screen
// reads the same in the DAW and in the browser. The one deliberate difference:
// the machined cabinets draw their vector relief (bevels, pockets, washers,
// knurling) but not the noise textures of SkinTextures.h, which are a JUCE
// image cache with no counterpart here.
//
// The plugin serves the page from its own memory and the two talk over a small
// message protocol: the page sends what the user changed, the plugin answers
// with the state, the preset bank and the meter.

const DESIGN_WIDTH = 1180;
const DESIGN_HEIGHT = 780;

// The four palettes, letter for letter from Source/gui/Palette.h.
const SKINS = [
  { name: 'Dark',
    background: '#181b20', panel: '#22262d', panelOutline: '#333a44',
    text: '#c8d0dc', textDim: '#7a8492', accent: '#e8a03c', accentAlt: '#4ac8e8',
    pitch: '#b06ae8', gate: '#e8564a', gridLine: '#2c313a', gridBeat: '#3d4451',
    knobTrack: '#3a4150', playhead: '#f2f5f9',
    faceBase: '#181b20', faceHi: '#2a2f37', faceLo: '#0e1115',
    bevelHi: '#4a5260', bevelLo: '#0d1013', shadow: '#000000', spec: '#ffffff',
    bodyTop: '#464e5c', bodyBottom: '#1b1e24', skirt: '#1b1e24', washer: '#8b93a0',
    pointer: '#c8d0dc', screw: '#3a4150', screwHi: '#5b6472', ink: '#7a8492',
    grain: '#2c313a', ledOn: '#e8564a', ledOff: '#3a4150', hazard: '#e8a03c',
    meterGreen: '#56d384', meterAmber: '#f0c040', meterRed: '#f04040',
    chip: '#181b20', chipText: '#7a8492',
    finish: 'plastic', panelMaterial: 'flat', faceMaterial: 'flat',
    knurl: 0, grainLines: 0, woodLines: 0, screws: 0,
    engraved: false, glowing: false, corner: 6 },
  { name: 'Console',
    background: '#0a0e16', panel: '#141d2b', panelOutline: '#2b4560',
    text: '#d9f2ff', textDim: '#7fa0bd', accent: '#38c8ff', accentAlt: '#39f0c8',
    pitch: '#d24bf0', gate: '#ff4d3a', gridLine: '#18222f', gridBeat: '#2a4056',
    knobTrack: '#1b2a3d', playhead: '#f2fbff',
    faceBase: '#101722', faceHi: '#1d2c40', faceLo: '#060a11',
    bevelHi: '#5f88ad', bevelLo: '#030609', shadow: '#000000', spec: '#eaf7ff',
    bodyTop: '#2b3b4e', bodyBottom: '#0c141d', skirt: '#16202b', washer: '#38c8ff',
    pointer: '#9ff0ff', screw: '#27455f', screwHi: '#63a2c8', ink: '#bfe6ff',
    grain: '#16222f', ledOn: '#4dff9e', ledOff: '#1c3242', hazard: '#ffc21f',
    meterGreen: '#3dffb0', meterAmber: '#ffd24d', meterRed: '#ff4d3a',
    chip: '#0b1420', chipText: '#7fa0bd',
    finish: 'glowing', panelMaterial: 'composite', faceMaterial: 'composite',
    knurl: 0, grainLines: 0, woodLines: 0, screws: 0,
    engraved: false, glowing: true, corner: 5 },
  { name: 'Wood',
    background: '#4a2c1a', panel: '#ada393', panelOutline: '#7a7161',
    text: '#241f16', textDim: '#544b3c', accent: '#8a3b0e', accentAlt: '#1c4f66',
    pitch: '#4a2674', gate: '#a11f14', gridLine: '#9d9382', gridBeat: '#7e7463',
    knobTrack: '#8c8271', playhead: '#1d1710',
    faceBase: '#5b3722', faceHi: '#7d5233', faceLo: '#2e1a0e',
    bevelHi: '#d8d0c0', bevelLo: '#453f34', shadow: '#20140a', spec: '#fff6e4',
    bodyTop: '#2a2622', bodyBottom: '#100e0c', skirt: '#e6dcc4', washer: '#c9c3b4',
    pointer: '#f2ead6', screw: '#8b8272', screwHi: '#d5cdba', ink: '#2b2519',
    grain: '#3a2012', ledOn: '#b3230f', ledOff: '#8c8271', hazard: '#d6a512',
    meterGreen: '#3f7d4e', meterAmber: '#c08a2a', meterRed: '#a11f14',
    chip: '#c9bda6', chipText: '#544b3c',
    finish: 'phenolic', panelMaterial: 'brushed', faceMaterial: 'wood',
    knurl: 0, grainLines: 26, woodLines: 26, screws: 0,
    engraved: true, glowing: false, corner: 4 },
  { name: 'Metal',
    background: '#3b4147', panel: '#242a2f', panelOutline: '#5d666e',
    text: '#e2e9f0', textDim: '#9fa9b2', accent: '#dca05a', accentAlt: '#63c6e0',
    pitch: '#a084d2', gate: '#d2564a', gridLine: '#2c3339', gridBeat: '#3d464e',
    knobTrack: '#171c20', playhead: '#f6f9fc',
    faceBase: '#474e55', faceHi: '#79828b', faceLo: '#23282c',
    bevelHi: '#aeb8c0', bevelLo: '#12161a', shadow: '#0c0f12', spec: '#fdfeff',
    bodyTop: '#838c95', bodyBottom: '#2e343a', skirt: '#454d55', washer: '#b9c2ca',
    pointer: '#f3f6f8', screw: '#6d767f', screwHi: '#c4ccd3', ink: '#e6ebf0',
    grain: '#8d979f', ledOn: '#5be07a', ledOff: '#39414a', hazard: '#d8b41f',
    meterGreen: '#5be07a', meterAmber: '#e8b84a', meterRed: '#d2564a',
    chip: '#171c20', chipText: '#9fa9b2',
    finish: 'metal', panelMaterial: 'brushed', faceMaterial: 'brushed',
    knurl: 32, grainLines: 34, woodLines: 0, screws: 4,
    engraved: true, glowing: false, corner: 4 },
];

// The ink roles the components ask for by name rather than by literal.
const ROLE = { Text: 'text', TextDim: 'textDim', Accent: 'accent',
               AccentAlt: 'accentAlt', Pitch: 'pitch', Gate: 'gate' };

function ink (role) { return skin[ROLE[role] || role]; }

// The recessed panels of the native editor, in the order it paints them.
const PANELS = [
  { rect: { x: 12, y: 52, w: 424, h: 200 }, title: 'OSCILLATOR & FILTER' },
  { rect: { x: 448, y: 52, w: 424, h: 200 }, title: 'ENVELOPE & ANALOG' },
  { rect: { x: 12, y: 258, w: 424, h: 146 }, title: 'DISTORTION & OUTPUT' },
  { rect: { x: 448, y: 258, w: 424, h: 146 }, title: '' },
  { rect: { x: 884, y: 52, w: 284, h: 344 }, title: 'GATE' },
  { rect: { x: 12, y: 408, w: 860, h: 88 }, title: 'SEQUENCER' },
  { rect: { x: 12, y: 504, w: 1156, h: 268 }, title: '' },
];

// The small captions the editor paints over the sequencer row and under the
// Cutoff knob: the same font, colour and rectangles.
const CAPTIONS = [
  { text: 'NOTE', rect: { x: 20, y: 424, w: 90, h: 12 } },
  { text: 'SCALE', rect: { x: 96, y: 424, w: 90, h: 12 } },
  { text: 'STEPS', rect: { x: 258, y: 424, w: 90, h: 12 } },
  { text: 'CLOCK', rect: { x: 330, y: 424, w: 90, h: 12 } },
  { text: 'SYNC', rect: { x: 408, y: 424, w: 90, h: 12 } },
  { text: 'FILTER', rect: { x: 186, y: 186, w: 112, h: 12 } },
  // The FX strip: small captions over each control, same dim style. The div
  // combo gets no extra title — its text is the value.
  { text: 'DIV', rect: { x: 452, y: 372, w: 56, h: 9 } },
  { text: 'DEL MIX', rect: { x: 514, y: 372, w: 62, h: 9 } },
  { text: 'FBK', rect: { x: 582, y: 372, w: 62, h: 9 } },
  { text: 'TONE', rect: { x: 650, y: 372, w: 62, h: 9 } },
  { text: 'REV MIX', rect: { x: 718, y: 372, w: 62, h: 9 } },
  { text: 'DECAY', rect: { x: 786, y: 372, w: 62, h: 9 } },
];

// The knob cells, exactly the native baseCell rectangles. `step` is the
// parameter interval the native wheel nudges by, in raw units; `log` marks the
// skewed range the native parameter carries.
const KNOBS = [
  { id: 'Wave', label: 'Wave', role: 'Accent', step: 0.002,
    cell: { x: 22, y: 104, w: 80, h: 78 } },
  { id: 'Attack', label: 'Attack', role: 'Accent', step: 0.002,
    cell: { x: 104, y: 104, w: 80, h: 78 } },
  { id: 'Cutoff', label: 'Cutoff', role: 'Accent', log: true,
    cell: { x: 186, y: 104, w: 80, h: 78 } },
  { id: 'Res', label: 'Res', role: 'Accent', step: 0.005,
    cell: { x: 268, y: 104, w: 80, h: 78 } },
  { id: 'EnvAmt', label: 'Env', role: 'Accent', step: 0.005,
    cell: { x: 350, y: 104, w: 80, h: 78 } },
  { id: 'Contour', label: 'Contour', role: 'AccentAlt', step: 0.01,
    cell: { x: 464, y: 104, w: 72, h: 78 } },
  { id: 'Accent', label: 'Accent', role: 'AccentAlt', step: 0.005,
    cell: { x: 564, y: 104, w: 72, h: 78 } },
  { id: 'Glide', label: 'Glide', role: 'AccentAlt', step: 0.005,
    cell: { x: 664, y: 104, w: 72, h: 78 } },
  { id: 'Slope', label: 'Slope', role: 'AccentAlt', step: 0.005,
    cell: { x: 764, y: 104, w: 72, h: 78 } },
  { id: 'Drive', label: 'Drive', role: 'Pitch', step: 0.005,
    cell: { x: 20, y: 300, w: 72, h: 78 } },
  { id: 'DistMix', label: 'Dist Mix', role: 'Pitch', step: 0.005,
    cell: { x: 102, y: 300, w: 72, h: 78 } },
  { id: 'Warmth', label: 'Warmth', role: 'Pitch', step: 0.005,
    cell: { x: 184, y: 300, w: 72, h: 78 } },
  { id: 'Gain', label: 'Gain', role: 'Pitch', step: 0.1,
    cell: { x: 266, y: 300, w: 72, h: 78 } },
  { id: 'GateAtk', label: 'Atk', role: 'Gate', log: true,
    cell: { x: 890, y: 278, w: 72, h: 78 } },
  { id: 'GateRel', label: 'Rel', role: 'Gate', log: true,
    cell: { x: 978, y: 278, w: 72, h: 78 } },
  { id: 'GateMix', label: 'Mix', role: 'Gate', step: 0.005,
    cell: { x: 1066, y: 278, w: 72, h: 78 } },
  { id: 'Swing', label: 'Swing', role: 'AccentAlt', step: 0.005,
    cell: { x: 744, y: 412, w: 58, h: 78 } },
  { id: 'BPM', label: 'BPM', role: 'AccentAlt', log: true,
    cell: { x: 802, y: 412, w: 60, h: 78 } },
];

// The dropdowns, on the native rectangles.
const COMBOS = [
  { id: 'UiSkin', rect: { x: 52, y: 10, w: 96, h: 26 } },
  { id: 'FilterType', rect: { x: 186, y: 198, w: 112, h: 22 } },
  { id: 'SatType', rect: { x: 16, y: 378, w: 112, h: 22 } },
  { id: 'AnalogMode', rect: { x: 188, y: 378, w: 94, h: 22 } },
  { id: 'GateDiv', rect: { x: 890, y: 360, w: 100, h: 22 } },
  { id: 'ClockDiv', rect: { x: 330, y: 438, w: 74, h: 24 } },
  { id: 'Sync', rect: { x: 408, y: 438, w: 88, h: 24 } },
  { id: 'FxDelDiv', rect: { x: 452, y: 382, w: 56, h: 20 } },
];

// The FX strip faders: horizontal mini sliders, exactly the native baseCell
// rects; the whole cell is the slider (KnobSlider LinearHorizontal, no labels).
// `step` is the parameter interval the wheel nudges by, as on the knobs.
const FADERS = [
  { id: 'FxDelMix', label: 'Delay Mix', role: 'Pitch', step: 0.005,
    cell: { x: 514, y: 382, w: 62, h: 20 } },
  { id: 'FxDelFb', label: 'Delay Fb', role: 'Pitch', step: 0.005,
    cell: { x: 582, y: 382, w: 62, h: 20 } },
  { id: 'FxDelTone', label: 'Delay Tone', role: 'Pitch', step: 0.005,
    cell: { x: 650, y: 382, w: 62, h: 20 } },
  { id: 'FxRevMix', label: 'Reverb Mix', role: 'Pitch', step: 0.005,
    cell: { x: 718, y: 382, w: 62, h: 20 } },
  { id: 'FxRevDecay', label: 'Reverb Decay', role: 'Pitch', step: 0.005,
    cell: { x: 786, y: 382, w: 62, h: 20 } },
];

// The toggles. The `chip` ones are the recessed keys of the cabinets; the rest
// are the ordinary buttons of the header row.
const BUTTONS = [
  { id: 'Play', label: 'PLAY', rect: { x: 668, y: 10, w: 84, h: 30 },
    onLabel: 'STOP', accent: 'accent' },
  { id: 'SeqOn', label: 'SEQ', rect: { x: 756, y: 10, w: 44, h: 30 },
    offLabel: 'MIDI', accent: 'accent' },
  { id: 'LimOn', label: 'Lim', rect: { x: 1136, y: 10, w: 32, h: 30 },
    accent: 'accent' },
  { id: 'GateOn', label: 'ON', rect: { x: 1108, y: 58, w: 52, h: 22 },
    chip: true, accent: 'gate' },
  { id: 'AnalogOn', label: 'Analog', rect: { x: 134, y: 378, w: 48, h: 22 },
    chip: true, accent: 'accentAlt' },
  { id: 'AnalogHiss', label: 'Hiss', rect: { x: 288, y: 378, w: 34, h: 22 },
    chip: true, accent: 'accentAlt' },
];

// The bank buttons.
const ACTIONS = [
  { id: 'export', label: 'Export WAV', rect: { x: 804, y: 10, w: 110, h: 30 } },
  { id: 'random', label: 'Random', rect: { x: 502, y: 438, w: 74, h: 24 } },
  { id: 'midi', label: 'MIDI', rect: { x: 582, y: 438, w: 74, h: 24 } },
  { id: 'clear', label: 'Clear', rect: { x: 662, y: 438, w: 74, h: 24 } },
];

// The header controls that do not map to a parameter.
const HEADER = {
  logo: { x: 12, y: 8, w: 34, h: 34 },
  version: { x: 156, y: 8, w: 284, h: 30 },
  help: { x: 444, y: 12, w: 24, h: 26 },
  presetPrev: { x: 474, y: 10, w: 24, h: 30 },
  presetField: { x: 502, y: 10, w: 132, h: 30 },
  presetNext: { x: 636, y: 10, w: 24, h: 30 },
  tempo: { x: 914, y: 14, w: 142, h: 22 },
  meter: { x: 1060, y: 10, w: 72, h: 30 },
};

// The scopes, the gate grid and the sequencer, again the native rects. The
// scopes stand a little shorter than they used to: the freed 24 px carry the
// FX strip below them, inside the same panel.
const PLOT = {
  waveScope: { x: 452, y: 268, w: 202, h: 100 },
  filterScope: { x: 662, y: 268, w: 202, h: 100 },
  gateGrid: { x: 888, y: 80, w: 276, h: 190 },
  credit: { x: 884, y: 470, w: 284, h: 22 },
  seqEditor: { x: 16, y: 508, w: 1148, h: 260 },
};

// The comboboxes the pattern carries rather than a parameter: the base note,
// the scale and the length, on the native sequencer rectangles.
const SEQ_COMBOS = [
  { key: 'base', rect: { x: 20, y: 438, w: 74, h: 24 } },
  { key: 'scale', rect: { x: 96, y: 438, w: 158, h: 24 } },
  { key: 'steps', rect: { x: 258, y: 438, w: 68, h: 24 } },
];

const STEP_COUNTS = [8, 12, 16, 24, 32];

// The knob that follows the host tempo (greyed out while Sync is Host).
const SYNC_KNOB_ID = 'BPM';

// The grid of the native editor: fourteen pitch rows and four flag rows.
const PITCH_ROWS = 14;
const FLAG_ROWS = 4;

// The step codes, the same bit field the plugin uses.
const REST = 0;
const TIE = 1;
const NOTE_FLAG = 0x80;
const SLIDE_FLAG = 0x100;
const ACCENT_FLAG = 0x200;

// ---------------------------------------------------------------------------
// The state

// What the plugin sends: every parameter as a raw number, the choice lists,
// the ranges with their unit and digits, the pattern, the gate and the meter.
const state = {
  params: {}, choices: {}, seq: '', gate: '',
  preset: '', note: '', skin: 0, version: '', curve: [], tempo: 0,
};
const bank = { names: [], credits: [] };
const paramRanges = {};

// What the test harnesses read: the layout is geometry, the state is the proof
// that the plugin's handshake was parsed at all.
window.__state = state;

const meter = { peak: 0, rms: 0, gr: 1, step: -1, sector: -1, running: false };

// The meter ballistics, the same slopes as Source/gui/MeterBallistics.h. The
// plugin answers a poll every 50 ms; between the polls the canvas animates.
const ballistics = {
  peakDb: -160, peakHoldDb: -160, peakHoldLeft: 0,
  rmsDb: -160, grDb: 0, last: 0,
  decayDbPerSecond: 40, holdDbPerSecond: 20, grDecayDbPerSecond: 40,
  holdSeconds: 1.5,
};

let sequence = { baseMidi: 36, scaleIndex: 1, steps: [], velocity: [] };
let gate = { numSectors: 16, closedMask: 0 };
let skin = SKINS[0];

// The pattern is not a parameter, so the page keeps its own edit dirty until
// the plugin echoes the state back.
let bankDirty = false;
let seenPreset = '';        // the name the last state echo carried
let loadedUser = null;      // the stored name of the user preset in play

const canvas = document.getElementById('ui');
const ctx = canvas.getContext('2d');

// The page carries its own message for a host that cannot run it, in the markup,
// because a message drawn by the script would not survive the script failing.
// Once there is a context to paint into, the message is in the way.
const fallback = document.getElementById('fallback');
if (ctx && fallback) fallback.style.display = 'none';

// A canvas that takes pointer events has to tell the browser it is not a
// surface to scroll or to zoom with, or a touch drag never reaches a knob.
canvas.style.touchAction = 'none';

let scale = 1;
let drag = null;            // the knob, strip or wheel a press is holding
let filterDrag = null;      // a drag inside the response plot
let hoverKnob = '';         // the parameter the pointer is over
let popup = null;           // the open dropdown or menu
let card = null;            // the about / manual overlay
let askBox = null;          // the prompt / confirm overlay
let stepMenuCell = -1;      // the step a context menu belongs to

// ---------------------------------------------------------------------------
// The host

function send (message) {
  const bytes = new TextEncoder().encode (JSON.stringify (message));
  window.parent.postMessage (bytes.buffer, '*');
}

// The hidden input behind every text edit: the preset name, the exact value of
// a knob, the save prompt. It belongs to the document, not the canvas.
const valueInput = document.createElement('input');
valueInput.type = 'text';
valueInput.spellcheck = false;
valueInput.style.cssText = 'position:absolute;display:none;z-index:2;' +
  'font:12px "Helvetica Neue",Helvetica,Arial,sans-serif;border:1px solid;' +
  'border-radius:3px;outline:none;padding:0 4px;';
document.body.appendChild (valueInput);
valueInput.addEventListener ('keydown', (event) => {
  if (event.key === 'Escape')
    valueInput.style.display = 'none';
  if (event.key === 'Enter' && typeof valueInput.oncommit === 'function')
    valueInput.oncommit();
});
valueInput.addEventListener ('blur', () => {
  if (typeof valueInput.oncommit === 'function') valueInput.oncommit();
});

function openValueInput (rect, initial, commit) {
  const o = origin();
  valueInput.style.display = 'block';
  valueInput.style.left = (o.x + rect.x * scale) + 'px';
  valueInput.style.top = (o.y + rect.y * scale) + 'px';
  valueInput.style.width = Math.max (24, rect.w * scale - 8) + 'px';
  valueInput.style.height = Math.max (14, rect.h * scale - 4) + 'px';
  valueInput.style.background = skin.panel;
  valueInput.style.color = skin.text;
  valueInput.style.borderColor = skin.accent;
  valueInput.value = initial;
  valueInput.oncommit = () => {
    valueInput.style.display = 'none';
    valueInput.oncommit = null;
    commit (valueInput.value.trim());
  };
  valueInput.focus();
  valueInput.select();
}

function origin () {
  return { x: (canvas.clientWidth - DESIGN_WIDTH * scale) / 2,
           y: (canvas.clientHeight - DESIGN_HEIGHT * scale) / 2 };
}

function applySkinIndex (value) {
  state.skin = Math.round (value || 0);
  skin = SKINS[Math.min (SKINS.length - 1, Math.max (0, state.skin))];
}

/** Records a parameter the plugin reported. The page's own edits come back here
    as well, one value per change: a knob the host moved arrives on a meter tick
    and has to redraw the control. */
function setParamValue (id, value) {
  state.params[id] = value;
  if (id === 'UiSkin')
    applySkinIndex (value);
}

window.addEventListener ('message', (event) => {
  const data = event.data;
  let text = '';
  if (data instanceof ArrayBuffer)
    text = new TextDecoder().decode (new Uint8Array (data));
  else if (ArrayBuffer.isView (data))
    text = new TextDecoder().decode (data);
  else if (typeof data === 'string')
    text = data;
  else
    return;

  let message;
  try { message = JSON.parse (text); } catch (error) { return; }

  if (message.t === 'state') {
    // The full state arrives with hello; a preset load sends the same message
    // without the statics. Whatever is missing keeps what the page holds, since
    // the ranges, the choices and the version never change.
    state.params = message.params || state.params;
    if (message.choices)
      state.choices = message.choices;
    if (message.ranges)
      for (const [id, range] of Object.entries (message.ranges))
        paramRanges[id] = range;
    if (message.version)
      state.version = message.version;
    if (message.seq !== undefined)
      state.seq = message.seq;
    if (message.gate !== undefined)
      state.gate = message.gate;
    if (message.curve !== undefined && message.curve.length)
      state.curve = message.curve;
    state.preset = message.preset || '';
    state.note = message.note || '';
    if (message.skin !== undefined)
      applySkinIndex (message.skin);
    parseSequence (state.seq);
    parseGate (state.gate);
    // A preset that arrived from the plugin is clean again; an edit the page
    // made itself stays marked until the next load.
    if (message.preset !== seenPreset)
      bankDirty = false;
    seenPreset = message.preset || '';
  } else if (message.t === 'param') {
    setParamValue (message.id, message.v);
    if (message.curve !== undefined && message.curve.length)
      state.curve = message.curve;
  } else if (message.t === 'seq') {
    state.seq = message.seq;
    parseSequence (state.seq);
  } else if (message.t === 'gate') {
    state.gate = message.gate;
    parseGate (state.gate);
  } else if (message.t === 'presets') {
    bank.names = message.names || [];
    bank.credits = message.credits || [];
  } else if (message.t === 'meter') {
    meter.peak = message.peak || 0;
    meter.rms = message.rms || 0;
    meter.gr = message.gr === undefined ? 1 : message.gr;
    meter.step = message.step === undefined ? -1 : message.step;
    meter.sector = message.sector === undefined ? -1 : message.sector;
    meter.running = !!message.running;
    if (message.tempo !== undefined)
      state.tempo = message.tempo;
    if (message.params)
      for (const [id, value] of Object.entries (message.params))
        setParamValue (id, value);
  } else if (message.t === 'audio') {
    downloadWav (message);
  }

  computeLayout();
  draw();
});

setInterval (() => send ({ t: 'meter' }), 50);

function animateBallistics (nowMs) {
  const dt = Math.min (0.1, Math.max (0, (nowMs - ballistics.last) / 1000));
  ballistics.last = nowMs;
  fall (ballistics, 'peakDb', 'peakHoldDb', 'peakHoldLeft', meter, 'peak', dt);
  fall (ballistics, 'rmsDb', null, null, meter, 'rms', dt);
  const grDb = 20 * Math.log10 (Math.max (1, meter.gr || 1));
  ballistics.grDb = grDb >= ballistics.grDb ? grDb
    : Math.max (grDb, ballistics.grDb - ballistics.grDecayDbPerSecond * dt);
  requestAnimationFrame (animateBallistics);
  // The native editor repaints its header 30 times a second whether or not a
  // knob moved: the meter and the playhead need it, and nothing else does.
  if (booted && nowMs - lastPaint >= 33) {
    lastPaint = nowMs;
    draw();
  }
}
let booted = false;
let lastPaint = 0;
requestAnimationFrame (animateBallistics);

/** One meter channel: an instant jump up, a dB/second fall, and a hold line
    that outlives the bar. */
function fall (b, bar, hold, holdLeft, src, key, dt) {
  const dbNow = toDb (src[key] || 0);
  if (hold === null) {
    b.rmsDb = Math.max (dbNow, b.rmsDb - b.decayDbPerSecond * dt);
    return;
  }
  if (dbNow >= b[bar]) {
    b[bar] = dbNow;
    b[hold] = dbNow;
    b[holdLeft] = b.holdSeconds;
  } else {
    b[bar] = Math.max (dbNow, b[bar] - b.decayDbPerSecond * dt);
  }
  if (b[holdLeft] > 0)
    b[holdLeft] -= dt;
  else
    b[hold] = Math.max (b[bar], b[hold] - b.holdDbPerSecond * dt);
}

function toDb (value) {
  return 20 * Math.log10 (Math.max (value, 1.0e-5));
}

// ---------------------------------------------------------------------------
// The pattern and the gate

function parseSequence (text) {
  const parts = String (text || '').split (':');
  sequence = { baseMidi: 36, scaleIndex: 1, steps: [], velocity: [] };
  const head = parts[0].toLowerCase();
  if (head !== 'v1' && head !== 'v2')
    return;

  sequence.baseMidi = parseInt (parts[1], 10) || 36;
  sequence.scaleIndex = parseInt (parts[2], 10) || 0;
  sequence.steps = parts[3].split (',').map ((v) => parseInt (v, 10) || 0);
  if (head === 'v2' && parts.length > 4)
    sequence.velocity = parts[4].split (',').map ((v) => parseInt (v, 10) || 127);
}

function sequenceText () {
  const steps = sequence.steps.join (',');
  const loud = sequence.steps.some ((_, i) => (sequence.velocity[i] ?? 127) !== 127);
  if (! loud)
    return 'v1:' + sequence.baseMidi + ':' + sequence.scaleIndex + ':' + steps;
  const vel = sequence.steps.map ((_, i) => sequence.velocity[i] ?? 127);
  return 'v2:' + sequence.baseMidi + ':' + sequence.scaleIndex + ':' + steps + ':' + vel.join (',');
}

function sendSequence () {
  send ({ t: 'seq', text: sequenceText() });
  bankDirty = true;
}

function sendGate () {
  send ({ t: 'gate', text: gateText () });
}

function parseGate (text) {
  const parts = String (text || '').split (':');
  gate = { numSectors: 16, closedMask: 0, rotation: 0 };
  if (parts.length < 2 || parts[0].toLowerCase() !== 'g1')
    return;
  gate.closedMask = parseInt (parts[1], 16) || 0;
  gate.rotation = parts.length > 2 ? (parseInt (parts[2], 10) || 0) : 0;
  if (parts.length > 3)
    gate.numSectors = Math.min (16, Math.max (1, parseInt (parts[3], 10) || 16));
}

function gateClosed (index) {
  return (gate.closedMask & (1 << index)) !== 0;
}

// The gammas of the plugin (Source/dsp/Sequence.cpp), note for note, so the
// row captions and the combo agree with what actually plays.
const SCALE_NAMES = ['Chromatic', 'Major', 'Minor', 'Harmonic Minor', 'Phrygian',
  'Phrygian Dominant', 'Dorian', 'Mixolydian', 'Minor Pentatonic',
  'Major Pentatonic', 'Ukrainian Dorian', 'Whole Tone'];

const SCALE_OFFSETS = [
  [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14],
  [0,2,4,5,7,9,11,12,14,16,17,19,21,23],
  [0,2,3,5,7,8,10,12,14,15,17,19,20,22],
  [0,2,3,5,7,8,11,12,14,15,17,19,20,23],
  [0,1,3,5,7,8,10,12,13,15,17,19,20,22],
  [0,1,4,5,7,8,10,12,13,16,17,19,20,22],
  [0,2,3,5,7,9,10,12,14,15,17,19,21,22],
  [0,2,4,5,7,9,10,12,14,16,17,19,21,22],
  [0,3,5,7,10,12,15,17,19,22,24,27,29,31],
  [0,2,4,7,9,12,14,16,19,21,24,26,28,31],
  [0,2,3,5,7,8,11,12,14,16,17,19,20,23],
  [0,2,4,6,8,10,12,14,16,18,20,22,24,26],
];

function scaleOffsets (index) {
  return SCALE_OFFSETS[Math.min (SCALE_OFFSETS.length - 1, Math.max (0, index))];
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

function noteName (midi) {
  return NOTE_NAMES[((midi % 12) + 12) % 12] + (Math.floor (midi / 12) - 1);
}

function midiForDegree (degree) {
  const offsets = scaleOffsets (sequence.scaleIndex);
  return sequence.baseMidi + offsets[Math.min (degree, offsets.length - 1)];
}

// The random pattern. The native one rolls the degree with a squared
// distribution so the line sits low, ties about one step in seven and marks
// slide and accent sparingly (Sequence::randomise).
function randomise (seed) {
  let s = BigInt (seed >>> 0) + 1n;
  const next = () => {
    s = (s * 1103515245n + 12345n) & 0x7fffffffn;
    return Number (s) / 0x7fffffffn;
  };
  const offsets = scaleOffsets (sequence.scaleIndex);
  const last = offsets.length - 1;
  const steps = [];
  const velocity = [];
  for (let i = 0; i < sequence.steps.length; ++i) {
    const roll = next();
    if (roll < 0.55) {
      let code = NOTE_FLAG | Math.floor (Math.pow (next(), 1.6) * last);
      if (next() < 0.10) code |= SLIDE_FLAG;
      if (next() < 0.12) code |= ACCENT_FLAG;
      steps.push (code);
    } else if (roll < 0.68 && steps.length > 0) {
      steps.push (TIE);
    } else {
      steps.push (REST);
    }
    velocity.push (127);
  }
  sequence.steps = steps;
  sequence.velocity = velocity;
}

// ---------------------------------------------------------------------------
// A compact MIDI file reader: one track of notes in quarter-note beats, laid
// over the grid the way MidiImport::fit does it. The lowest pitched lane
// wins, the GM drum channel is skipped, tempo metas are noise.

function parseMidiFile (buffer) {
  const view = new DataView (buffer);
  const u32 = (o) => view.getUint32 (o);
  const u16 = (o) => view.getUint16 (o);

  if (u32 (0) !== 0x4d546864) return null;   // 'MThd'
  const headLength = u32 (8) + 8;
  const numTracks = u16 (8 + 6);
  const timeFormat = u16 (8 + 8);
  if (timeFormat === 0 || (timeFormat & 0x8000) !== 0)
    return null;                             // SMPTE: no beat count to follow
  const ticksPerBeat = timeFormat;

  const events = [];
  let offset = 8 + headLength;
  for (let t = 0; t < numTracks; ++t) {
    if (offset + 8 > view.byteLength || u32 (offset) !== 0x4d54726b) return null;
    const trackEnd = offset + 8 + u32 (offset + 4);
    let pos = offset + 8;
    let tick = 0, running = 0, lastTick = 0;
    const held = new Map();

    while (pos < trackEnd) {
      let delta = 0, byte = 0;
      do {
        if (pos >= trackEnd) return null;
        byte = view.getUint8 (pos++);
        delta = (delta << 7) | (byte & 0x7f);
      } while (byte & 0x80);
      tick += delta;

      let status = view.getUint8 (pos);
      if (status < 0x80) {
        if (running === 0) return null;
        status = running;
      } else {
        pos++;
        if (status < 0xf0) running = status;
      }

      if (status === 0xff || status === 0xf0 || status === 0xf7) {
        pos++;
        let length = 0;
        do {
          if (pos >= trackEnd) return null;
          byte = view.getUint8 (pos++);
          length = (length << 7) | (byte & 0x7f);
        } while (byte & 0x80);
        pos += length;
        continue;
      }

      const kind = status & 0xf0;
      const channel = (status & 0x0f) + 1;
      const midi = view.getUint8 (pos++);

      if (kind === 0x90) {
        const velocity = view.getUint8 (pos++);
        if (channel !== 10) {
          if (velocity !== 0) {
            if (! held.has (midi)) held.set (midi, []);
            held.get (midi).push ({ onset: tick, velocity });
          } else {
            const stack = held.get (midi);
            if (stack !== undefined && stack.length > 0) {
              const note = stack.shift();
              events.push ({ midi, onsetTick: note.onset, keyUpTick: tick, velocity: note.velocity });
            }
          }
          lastTick = Math.max (lastTick, tick);
        }
      } else if (kind === 0x80) {
        const stack = held.get (midi);
        if (stack !== undefined && stack.length > 0 && channel !== 10) {
          const note = stack.shift();
          events.push ({ midi, onsetTick: note.onset, keyUpTick: tick, velocity: note.velocity });
        }
        pos += 1;
        lastTick = Math.max (lastTick, tick);
      } else if (kind === 0xc0 || kind === 0xd0) {
        pos += 0;
      } else {
        pos += 1;
      }
    }

    // A note the file never released runs to the end of its own track.
    for (const [midi, stack] of held)
      for (const note of stack)
        events.push ({ midi, onsetTick: note.onset, keyUpTick: lastTick, velocity: note.velocity });
    offset = trackEnd;
  }

  return { notes: events, ticksPerBeat };
}

/** The nearest degree of the sequence's own scale (MidiImport::nearestDegree):
    equal gaps go down, so a chromatic phrase lands on the flatter step. */
function nearestDegree (offsets, semitones) {
  const last = offsets.length - 1;
  if (semitones <= offsets[0]) return 0;
  if (semitones >= offsets[last]) return last;

  let best = 0, bestGap = Math.abs (offsets[0] - semitones);
  for (let k = 1; k <= last; ++k) {
    const gap = Math.abs (offsets[k] - semitones);
    if (gap < bestGap) { best = k; bestGap = gap; }
  }
  return best;
}

function importMidi (buffer) {
  const file = parseMidiFile (buffer);
  if (! file || file.ticksPerBeat <= 0 || file.notes.length === 0)
    return false;

  const stepBeats = stepLengthBeats();
  if (stepBeats <= 0)
    return false;

  const notes = file.notes.map ((event) => ({
    midi: event.midi, onset: event.onsetTick, velocity: event.velocity,
    span: Math.max (1, event.keyUpTick - event.onsetTick),
  }));
  notes.sort ((a, b) => a.onset !== b.onset ? a.onset - b.onset : a.midi - b.midi);

  const offsets = scaleOffsets (sequence.scaleIndex);
  const n = sequence.steps.length;
  const start = notes[0].onset;
  let lowest = 128, highest = 0;
  for (const nt of notes) {
    lowest = Math.min (lowest, nt.midi);
    highest = Math.max (highest, nt.midi);
  }

  // The base moves down only — and up once, when the whole phrase sits above
  // the reach of the rows, so the line stays inside the window either way.
  let base = sequence.baseMidi;
  if (lowest < base || lowest > base + offsets[offsets.length - 1])
    base = lowest;
  if (highest > base + offsets[offsets.length - 1])
    base = Math.max (lowest, highest - offsets[offsets.length - 1]);
  sequence.baseMidi = Math.min (72, Math.max (24, base));

  sequence.steps = new Array (n).fill (REST);
  sequence.velocity = new Array (n).fill (127);

  let heldTo = -1, heldDegree = -1, attackedAt = -1;
  for (const nt of notes) {
    const step = Math.round (((nt.onset - start) / file.ticksPerBeat) / stepBeats);
    if (step >= n) break;
    if (step <= attackedAt) continue;

    const s0 = step;
    const s1 = Math.min (n - 1, s0 + Math.max (1,
      Math.round ((nt.span / file.ticksPerBeat) / stepBeats)) - 1);
    const degree = nearestDegree (offsets, nt.midi - sequence.baseMidi);
    const legato = heldTo >= 0 && s0 <= heldTo + 1;

    if (legato && degree === heldDegree) {
      for (let k = heldTo + 1; k <= s1; ++k) sequence.steps[k] = TIE;
      heldTo = Math.max (heldTo, s1);
      continue;
    }

    sequence.steps[s0] = NOTE_FLAG | degree;
    if (legato) sequence.steps[s0] |= SLIDE_FLAG;
    for (let k = s0 + 1; k <= s1; ++k) sequence.steps[k] = TIE;
    sequence.velocity[s0] = Math.min (127, Math.max (1, nt.velocity || 127));
    // The same accent rule as MidiImport::fit: the top of the velocity range.
    if ((nt.velocity || 127) >= MIDI_ACCENT_VELOCITY)
      sequence.steps[s0] |= ACCENT_FLAG;

    heldTo = s1;
    heldDegree = degree;
    attackedAt = s0;
  }
  return true;
}

/** How long one step runs, in quarter beats: the clock divider in play. */
function stepLengthBeats () {
  const div = Math.round (state.params.ClockDiv || 0);
  return div <= 0 ? 0.25 : 0.25 * div;
}

// The MIDI writer mirrors Source/dsp/MidiExport.h: one loop, 480 ticks to the
// quarter, accent carried in velocity (at or above the importer's threshold).
const MIDI_PPQ = 480;
const MIDI_ACCENT_VELOCITY = 100;

function asciiBytes (text) {
  return Array.from (text, (c) => c.charCodeAt (0));
}

function vlqBytes (value) {
  const out = [value & 0x7f];
  let v = value >>> 7;
  while (v > 0) {
    out.unshift ((v & 0x7f) | 0x80);
    v >>>= 7;
  }
  return out;
}

function buildMidi (bpm, stepBeats) {
  const n = sequence.steps.length;
  const events = [];
  for (let i = 0; i < n; ++i) {
    if (! isNoteStep (i)) continue;

    // A note lasts until the next attack: its own step plus every tie after it.
    let length = 1;
    while (i + length < n && sequence.steps[i + length] === TIE) ++length;

    const onset = Math.round (i * stepBeats * MIDI_PPQ);
    const offset = Math.round ((i + length) * stepBeats * MIDI_PPQ);
    const level = Math.round (velocityOf (i) * 127);
    let velocity = (sequence.steps[i] & ACCENT_FLAG) !== 0
      ? Math.max (MIDI_ACCENT_VELOCITY, level)
      : Math.min (MIDI_ACCENT_VELOCITY - 1, level);
    velocity = Math.min (127, Math.max (1, velocity));

    const note = Math.min (108, Math.max (12, midiForDegree (degreeOf (i))));
    events.push ({ tick: onset, order: 1, bytes: [0x90, note, velocity] });
    events.push ({ tick: offset, order: 0, bytes: [0x80, note, 0] });
  }
  // A note-off and a note-on on the same tick: the off goes first, so a legato
  // pair stays two notes.
  events.sort ((a, b) => a.tick - b.tick || a.order - b.order);

  const tempo = Math.round (60000000 / (bpm > 1 ? bpm : 120));
  const track = [0x00, 0xff, 0x51, 0x03, (tempo >> 16) & 0xff, (tempo >> 8) & 0xff, tempo & 0xff];
  let last = 0;
  for (const e of events) {
    track.push (...vlqBytes (e.tick - last), ...e.bytes);
    last = e.tick;
  }
  track.push (...vlqBytes (0), 0xff, 0x2f, 0x00);

  const u32 = (v) => [(v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff];
  const u16 = (v) => [(v >>> 8) & 0xff, v & 0xff];
  return new Uint8Array ([
    ...asciiBytes ('MThd'), ...u32 (6), ...u16 (0), ...u16 (1), ...u16 (MIDI_PPQ),
    ...asciiBytes ('MTrk'), ...u32 (track.length), ...track,
  ]);
}

function exportMidi () {
  const bytes = buildMidi (state.params.BPM || 120, stepLengthBeats());
  const url = URL.createObjectURL (new Blob ([bytes], { type: 'audio/midi' }));
  const link = document.createElement ('a');
  link.href = url;
  link.download = 'spiral303-' + exportName () + '.mid';
  link.click();
  setTimeout (() => URL.revokeObjectURL (url), 2000);
}

window.__midi = { build: buildMidi };

function chooseMidiFile () {
  const picker = document.createElement('input');
  picker.type = 'file';
  picker.accept = '.mid,.midi';
  picker.onchange = () => {
    const chosen = picker.files && picker.files[0];
    if (! chosen) return;
    chosen.arrayBuffer().then ((buf) => {
      if (importMidi (buf)) {
        sendSequence();
        draw();
      }
    });
  };
  picker.click();
}

// Native export names the file after the loaded preset: a dirty user preset
// is just "user", a factory one is its name in lowercase with dashes.
function exportName () {
  if (bankDirty)
    return 'user';
  return (state.preset || 'Spiral303').toLowerCase().replace (/ /g, '-');
}

function downloadWav (message) {
  const samples = atob (message.samples);
  const rate = message.rate || 48000;
  const buffer = new ArrayBuffer (44 + samples.length);
  const out = new DataView (buffer);
  const write = (offset, text) => {
    for (let i = 0; i < text.length; ++i)
      out.setUint8 (offset + i, text.charCodeAt (i));
  };
  write (0, 'RIFF'); out.setUint32 (4, 36 + samples.length, true);
  write (8, 'WAVE'); write (12, 'fmt ');
  out.setUint32 (16, 16, true); out.setUint16 (20, 1, true);
  out.setUint16 (22, 1, true); out.setUint32 (24, rate, true);
  out.setUint32 (28, rate * 2, true); out.setUint16 (32, 2, true);
  out.setUint16 (34, 16, true); write (36, 'data');
  out.setUint32 (40, samples.length, true);
  for (let i = 0; i < samples.length; ++i)
    out.setUint8 (44 + i, samples.charCodeAt (i));

  const url = URL.createObjectURL (new Blob ([buffer], { type: 'audio/wav' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = 'spiral303-' + exportName () + '.wav';
  link.click();
  setTimeout (() => URL.revokeObjectURL (url), 2000);
}

// ---------------------------------------------------------------------------
// Drawing helpers

// The palette entries are '#rrggbb' strings; JUCE mixes colours in floating
// point, so the same arithmetic happens here.
const rgbCache = new Map();

function rgbOf (colour) {
  const cached = rgbCache.get (colour);
  if (cached !== undefined)
    return cached;
  const text = String (colour);
  const value = (parsed) => Number.isFinite (parsed) ? Math.min (255, Math.max (0, parsed)) : 0;
  const parts = text.startsWith ('rgb(')
    ? text.replace ('rgb(', '').replace (')', '').split (',').map ((v) => Math.round (Number (v))).concat ([1])
    : text.length === 9
    ? [ parseInt (text.slice (1, 3), 16), parseInt (text.slice (3, 5), 16),
        parseInt (text.slice (5, 7), 16), value (parseInt (text.slice (7, 9), 16) / 255) ]
    : [ parseInt (text.slice (1, 3), 16), parseInt (text.slice (3, 5), 16),
        parseInt (text.slice (5, 7), 16), 1 ];
  rgbCache.set (text, parts);
  return parts;
}

function withAlpha (colour, alpha) {
  const [r, g, b, a] = rgbOf (colour);
  return 'rgba(' + r + ',' + g + ',' + b + ',' + (a * alpha).toFixed (3) + ')';
}

/** JUCE's brighter() and darker(): a third of the way to white or black. */
function brighter (colour) { return mixWith (colour, '#ffffff', 0.3); }
function darker (colour) { return mixWith (colour, '#000000', 0.3); }

function mixWith (a, b, t) {
  const ca = rgbOf (a), cb = rgbOf (b);
  const lerp = (x, y) => Math.round (x + (y - x) * t);
  return 'rgb(' + lerp (ca[0], cb[0]) + ',' + lerp (ca[1], cb[1]) + ',' + lerp (ca[2], cb[2]) + ')';
}

function rr (x, y, w, h, r) {
  const radius = Math.min (r, Math.abs (w) / 2, Math.abs (h) / 2);
  ctx.beginPath();
  ctx.moveTo (x + radius, y);
  ctx.arcTo (x + w, y, x + w, y + h, radius);
  ctx.arcTo (x + w, y + h, x, y + h, radius);
  ctx.arcTo (x, y + h, x, y, radius);
  ctx.arcTo (x, y, x + w, y, radius);
  ctx.closePath();
}

function fillRR (x, y, w, h, r, colour) {
  rr (x, y, w, h, r);
  ctx.fillStyle = colour;
  ctx.fill();
}

function strokeRR (x, y, w, h, r, colour, width) {
  rr (x, y, w, h, r);
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.stroke();
}

function fillRect (x, y, w, h, colour) {
  ctx.fillStyle = colour;
  ctx.fillRect (x, y, w, h);
}

function line (x0, y0, x1, y1, width, colour) {
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo (x0, y0);
  ctx.lineTo (x1, y1);
  ctx.stroke();
}

/** One arc of a rotary track: the same half-circle sweep JUCE strokes, with
    the rounded ends the native PathStrokeType asks for. */
function arc (cx, cy, radius, from, to, width, colour) {
  if (to - from <= 0)
    return;
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc (cx, cy, radius, from - Math.PI / 2, to - Math.PI / 2);
  ctx.stroke();
  ctx.lineCap = 'butt';
}

function radialLine (cx, cy, r0, r1, angle, width, colour) {
  line (cx + Math.cos (angle) * r0, cy + Math.sin (angle) * r0,
        cx + Math.cos (angle) * r1, cy + Math.sin (angle) * r1, width, colour);
}

/** Text in one native rectangle. The native side paints with JUCE
    Justification; this is the same box model: the string is centred in the
    rectangle on both axes unless told otherwise, and clipped to it. */
function text (string, rect, size, colour, options) {
  const opts = options || {};
  ctx.font = (opts.bold ? 'bold ' : '') + size.toFixed (2) + 'px "Helvetica Neue",Helvetica,Arial,sans-serif';
  ctx.fillStyle = colour;
  ctx.textBaseline = 'middle';
  ctx.textAlign = opts.align === 'left' ? 'left' : opts.align === 'right' ? 'right' : 'center';
  const x = opts.align === 'left' ? rect.x
    : opts.align === 'right' ? rect.x + rect.w
    : rect.x + rect.w / 2;
  const y = opts.top !== undefined ? rect.y + opts.top
    : opts.align === 'top' ? rect.y + size / 2
    : rect.y + rect.h / 2;
  ctx.save();
  ctx.beginPath();
  ctx.rect (rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2);
  ctx.clip();
  ctx.fillText (String (string), x, y);
  ctx.restore();
}

function measure (string, size, bold) {
  ctx.font = (bold ? 'bold ' : '') + size.toFixed (2) + 'px "Helvetica Neue",Helvetica,Arial,sans-serif';
  return ctx.measureText (String (string)).width;
}

/** JUCE clips a too-long label; an ellipsis reads better and never overlaps
    the control next door. */
function fit (string, maxWidth, size, bold) {
  let out = String (string);
  if (measure (out, size, bold) <= maxWidth)
    return out;
  while (out.length > 1 && measure (out + '…', size, bold) > maxWidth)
    out = out.slice (0, -1);
  return out + '…';
}

function hit (x, y, rect) {
  return rect !== undefined && x >= rect.x && x <= rect.x + rect.w
         && y >= rect.y && y <= rect.y + rect.h;
}

function unionRect (a, b) {
  const x0 = Math.min (a.x, b.x), y0 = Math.min (a.y, b.y);
  const x1 = Math.max (a.x + a.w, b.x + b.w), y1 = Math.max (a.y + a.h, b.y + b.h);
  return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
}

// ---------------------------------------------------------------------------
// The parameter values as the plugin states them

function isLog (id) {
  const knob = KNOBS.find ((k) => k.id === id);
  return knob !== undefined && knob.log === true;
}

/** Raw value -> 0..1, the same mapping the native parameter range applies. */
function normalise (id, value) {
  const range = paramRanges[id] || { min: 0, max: 1 };
  const span = range.max - range.min;
  if (isLog (id)) {
    const lo = Math.log (Math.max (1e-6, range.min)), hi = Math.log (Math.max (1e-6, range.max));
    return (Math.log (Math.max (1e-6, value)) - lo) / (hi - lo);
  }
  return span <= 0 ? 0 : (value - range.min) / span;
}

function denormalise (id, norm) {
  const range = paramRanges[id] || { min: 0, max: 1 };
  const t = Math.min (1, Math.max (0, norm));
  if (isLog (id)) {
    const lo = Math.log (Math.max (1e-6, range.min)), hi = Math.log (Math.max (1e-6, range.max));
    return Math.exp (lo + (hi - lo) * t);
  }
  return range.min + (range.max - range.min) * t;
}

/** The knob readout: the digits and the unit the plugin reports for it. */
function paramText (id) {
  const range = paramRanges[id] || { min: 0, max: 1, decimals: 2, unit: '' };
  const value = state.params[id];
  if (value === undefined)
    return '';
  return value.toFixed (range.decimals === undefined ? 2 : range.decimals) + (range.unit || '');
}

// ---------------------------------------------------------------------------
// The layout: every native rectangle, in design units

const layout = { knobs: [], faders: [], combos: [], buttons: [], actions: [], seqCombos: [],
                 header: HEADER, gateCells: [], grid: null, flagRows: [] };

function computeLayout () {
  // The native resized() sizes a knob in device pixels: a 44 px body at 1:1,
  // never below 16 px and never wider than its cell. Divided by the page
  // scale, that floor becomes 16/scale in design units.
  const knobFloor = Math.max (44, 16 / Math.max (scale, 0.05));
  layout.knobs = KNOBS.map ((knob) => {
    const cell = knob.cell;
    const size = Math.min (knobFloor, cell.w);
    const slider = { x: cell.x + (cell.w - size) / 2, y: cell.y, w: size, h: size };
    const labelH = 13;
    return { knob, cell, rect: slider,
             nameRect: { x: cell.x, y: cell.y + size + 2, w: cell.w, h: labelH },
             valueRect: { x: cell.x, y: cell.y + size + 2 + labelH, w: cell.w, h: labelH } };
  });

  layout.combos = COMBOS.map ((combo) => ({ ...combo, rect: combo.rect }));
  // The fader owns the entire cell, as the native isFader branch does.
  layout.faders = FADERS.map ((fader) => ({ fader, rect: fader.cell }));
  layout.seqCombos = SEQ_COMBOS.map ((combo) => ({ ...combo, rect: combo.rect }));
  layout.buttons = BUTTONS.map ((button) => ({ ...button, rect: button.rect }));
  layout.actions = ACTIONS.map ((action) => ({ ...action, rect: action.rect }));

  // The gate: a 4x4 field of squares with a 3 px gap, inside the panel rect
  // reduced by its own frame (GateCircleComponent::cellBounds).
  const gateArea = reduced (PLOT.gateGrid, 1);
  const cellW = gateArea.w / 4, cellH = gateArea.h / 4;
  layout.gateCells = [];
  for (let i = 0; i < 16; ++i) {
    const col = i % 4, row = Math.floor (i / 4);
    layout.gateCells.push (reduced ({ x: gateArea.x + col * cellW, y: gateArea.y + row * cellH,
                                       w: cellW, h: cellH }, 3));
  }

  // The pattern grid: a 28 px gutter of note names, an 18 px caption strip on
  // top and eighteen equal rows — fourteen pitches, then TIE, SL, AC and VEL.
  const editor = reduced (PLOT.seqEditor, 1);
  const gutter = 28, top = 18, bottom = 2;
  const rowsTotal = PITCH_ROWS + FLAG_ROWS;
  const rowH = (editor.h - top - bottom) / rowsTotal;
  const plot = { x: editor.x + gutter, y: editor.y + top,
                 w: editor.w - gutter - 2, h: rowH * rowsTotal };
  const pitchBottom = plot.y + rowH * PITCH_ROWS;
  layout.grid = plot;
  layout.rowH = rowH;
  layout.gutter = gutter;
  layout.pitchBottom = pitchBottom;
  layout.flagRows = ['TIE', 'SL', 'AC', 'VEL'].map ((label, index) =>
    ({ label, kind: index, x: plot.x, y: pitchBottom + rowH * index, w: plot.w, h: rowH }));
  layout.velStrip = layout.flagRows[3];

  window.__layout = layout;
}

function reduced (rect, amount) {
  return { x: rect.x + amount, y: rect.y + amount,
           w: rect.w - amount * 2, h: rect.h - amount * 2 };
}

function fitCanvas () {
  const ratio = window.devicePixelRatio || 1;
  const width = window.innerWidth;
  const height = window.innerHeight;

  // The native editor scales the same way: fit the design inside the window
  // and centre it, so the whole surface grows with the host's resize.
  scale = Math.min (width / DESIGN_WIDTH, height / DESIGN_HEIGHT);
  canvas.width = Math.round (width * ratio);
  canvas.height = Math.round (height * ratio);
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';

  const o = origin();
  ctx.setTransform (ratio, 0, 0, ratio, 0, 0);
  ctx.fillStyle = skin.background;
  ctx.fillRect (0, 0, width, height);
  ctx.setTransform (ratio * scale, 0, 0, ratio * scale, ratio * o.x, ratio * o.y);

  computeLayout();
  draw();
}

// ---------------------------------------------------------------------------
// The paint

function clearAll () {
  const ratio = window.devicePixelRatio || 1;
  const o = origin();
  ctx.setTransform (ratio, 0, 0, ratio, 0, 0);
  ctx.fillStyle = skin.background;
  ctx.fillRect (0, 0, canvas.clientWidth, canvas.clientHeight);
  ctx.setTransform (ratio * scale, 0, 0, ratio * scale, ratio * o.x, ratio * o.y);
}

function draw () {
  clearAll();
  // Cabinet face plate: brushed aluminium, walnut or composite, generated from
  // the skin's seed. Drawn before the panels so the recessed pockets read as
  // machined out of it.
  const ftex = (typeof textures !== 'undefined') ? textures.face (skin, DESIGN_WIDTH, DESIGN_HEIGHT) : null;
  if (ftex !== null)
    ctx.drawImage (ftex, 0, 0, DESIGN_WIDTH, DESIGN_HEIGHT);
  drawFace();

  for (const panel of PANELS)
    drawPanel (panel.rect, panel.title);

  drawTitles();

  drawWaveScope (PLOT.waveScope);
  drawFilterScope (PLOT.filterScope);
  drawFaders();
  drawGateGrid();
  drawSequencer();
  drawCredit();

  for (const entry of layout.knobs)
    drawKnob (entry);

  for (const combo of layout.combos)
    drawParamCombo (combo);
  for (const combo of layout.seqCombos)
    drawSeqCombo (combo);
  for (const button of layout.buttons)
    drawButton (button);
  for (const action of layout.actions)
    drawButton (action);

  drawHeader();
  drawPopup();
  drawCard();
  drawAsk();
}

/** The machined face: the flat skins are a plain fill, the wood one gets the
    rebate between the cheeks and the insert (PluginEditor::paint). */
function drawFace () {
  if (skin.faceMaterial !== 'wood')
    return;
  const m = 12;
  const cheek = (rect) => {
    fillRect (rect.x, rect.y, rect.w, rect.h, withAlpha (skin.faceLo, 0.55));
    strokeRect (rect, brighter (skin.panelOutline), 1);
  };
  cheek ({ x: 0, y: 0, w: m, h: DESIGN_HEIGHT });
  cheek ({ x: DESIGN_WIDTH - m, y: 0, w: m, h: DESIGN_HEIGHT });
  cheek ({ x: 0, y: 0, w: DESIGN_WIDTH, h: m });
  cheek ({ x: 0, y: DESIGN_HEIGHT - m, w: DESIGN_WIDTH, h: m });

  const joint = { x: m, y: m, w: DESIGN_WIDTH - 2 * m, h: DESIGN_HEIGHT - 2 * m };
  strokeRect (joint, withAlpha (skin.bevelHi, 0.55), 1);
  strokeRect (reduced (joint, 1), withAlpha (skin.faceLo, 0.45), 1);
}

function strokeRect (rect, colour, width) {
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.strokeRect (rect.x + width / 2, rect.y + width / 2, rect.w - width, rect.h - width);
}

/** A recessed cabinet: the pocket is cut out of the face, so the shadow lands
    on the top lip and the light catches the bottom one (drawPanel). */
function drawPanel (rect, title) {
  const body = reduced (rect, 1);
  const material = skin.panelMaterial !== 'flat';
  fillRR (body.x, body.y, body.w, body.h, skin.corner, skin.panel);

  // Material plate: the native editor paints a generated texture over the
  // pocket. Tiled by the bucketed size, so a resize keeps the grain stable.
  const ptex = (typeof textures !== 'undefined') ? textures.panel (skin, body.w, body.h) : null;
  if (ptex !== null) {
    ctx.save();
    // clip to rounded body
    ctx.beginPath();
    const r = skin.corner;
    ctx.moveTo (body.x + r, body.y);
    ctx.lineTo (body.x + body.w - r, body.y);
    ctx.quadraticCurveTo (body.x + body.w, body.y, body.x + body.w, body.y + r);
    ctx.lineTo (body.x + body.w, body.y + body.h - r);
    ctx.quadraticCurveTo (body.x + body.w, body.y + body.h, body.x + body.w - r, body.y + body.h);
    ctx.lineTo (body.x + r, body.y + body.h);
    ctx.quadraticCurveTo (body.x, body.y + body.h, body.x, body.y + body.h - r);
    ctx.lineTo (body.x, body.y + r);
    ctx.quadraticCurveTo (body.x, body.y, body.x + r, body.y);
    ctx.closePath();
    ctx.clip();
    ctx.drawImage (ptex, body.x, body.y, body.w, body.h);
    ctx.restore();
  }

  if (material) {
    strokeRR (body.x, body.y, body.w, body.h, skin.corner, withAlpha (skin.bevelHi, 0.45), 1);
    ctx.save();
    ctx.beginPath();
    ctx.rect (body.x, body.y, body.w, body.h / 2);
    ctx.clip();
    strokeRR (body.x, body.y, body.w, body.h, skin.corner, withAlpha (skin.bevelLo, 0.70), 1);
    ctx.restore();
  }

  strokeRR (body.x, body.y, body.w, body.h, skin.corner, skin.panelOutline, 1);

  if (material && skin.screws > 0) {
    const size = Math.max (5, 7);
    const inset = 6;
    const corners = [[body.x + inset, body.y + inset],
                     [body.x + body.w - inset - size, body.y + inset],
                     [body.x + inset, body.y + body.h - inset - size],
                     [body.x + body.w - inset - size, body.y + body.h - inset - size]];
    for (const [cx, cy] of corners)
      drawScrew (cx, cy, size);
  }

  if (! title)
    return;

  const inset = material && skin.screws > 0 ? 21 : 10;
  const box = { x: rect.x + inset, y: rect.y + 6, w: rect.w - inset * 2, h: 12 };
  if (skin.engraved) {
    text (title, { ...box, y: box.y + 1 }, 10, withAlpha (skin.bevelHi, 0.5), { align: 'left', top: 6 });
    text (title, box, 10, withAlpha (skin.ink, 0.85), { align: 'left', top: 6 });
  } else if (skin.glowing) {
    for (const [dx, dy] of [[-1, 0], [1, 0], [0, -1], [0, 1]])
      text (title, { ...box, x: box.x + dx, y: box.y + dy }, 10,
            withAlpha (skin.ink, 0.22), { align: 'left', top: 6 });
    text (title, box, 10, skin.ink, { align: 'left', top: 6 });
  } else {
    text (title, box, 10, skin.textDim, { align: 'left', top: 6 });
  }
}

/** A corner screw seen from above: a slotted head with the light on its rim. */
function drawScrew (x, y, size) {
  const cx = x + size / 2, cy = y + size / 2, r = size / 2;
  ctx.beginPath();
  ctx.arc (cx, cy, r, 0, Math.PI * 2);
  ctx.fillStyle = skin.screw;
  ctx.fill();
  canvasArc (cx, cy, r * 0.85, Math.PI * 0.75, Math.PI * 1.6, 0.8, withAlpha (skin.screwHi, 0.9));
  canvasArc (cx, cy, r * 0.85, Math.PI * 1.75, Math.PI * 2.6, 0.8, withAlpha (skin.bevelLo, 0.8));
  line (cx - r * 0.55, cy + r * 0.55, cx + r * 0.55, cy - r * 0.55, 1, withAlpha (skin.bevelLo, 0.9));
}

function canvasArc (cx, cy, radius, from, to, width, colour) {
  if (to - from <= 0)
    return;
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.arc (cx, cy, radius, from, to);
  ctx.stroke();
}

/** The title line and the small captions the editor paints itself. */
function drawTitles () {
  text ("Spiralfractal's Spiral303  v" + (state.version || ''),
        HEADER.version, 14, skin.text, { align: 'left', bold: true });

  for (const caption of CAPTIONS)
    text (caption.text, caption.rect, 9, skin.textDim, { align: 'left', bold: true });
}

// ---------------------------------------------------------------------------
// The knobs

const ARC_START = Math.PI * 1.2;
const ARC_END = Math.PI * 2.8;

function drawKnob (entry) {
  const { knob, rect, nameRect, valueRect } = entry;
  ctx.save();
  if (! knobLive (knob.id))
    ctx.globalAlpha = 0.45;

  const fill = ink (knob.role);
  const norm = Math.min (1, Math.max (0, normalise (knob.id, state.params[knob.id])));
  const cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
  const outer = Math.min (rect.w, rect.h) / 2 - 1;
  const trackR = outer - 6;
  const bodyR = trackR - 3.5;
  const sweep = ARC_END - ARC_START;
  const angle = ARC_START + norm * sweep - Math.PI / 2;
  const material = skin.finish !== 'plastic';
  const detail = material && bodyR >= 9;

  if (hoverKnob === knob.id) {
    ctx.beginPath();
    ctx.arc (cx, cy, outer, 0, Math.PI * 2);
    ctx.fillStyle = withAlpha (fill, 0.22);
    ctx.fill();
    canvasArc (cx, cy, outer, 0, Math.PI * 2, 1.5, withAlpha (skin.text, 0.9));
  }

  for (let i = 0; i <= 10; ++i) {
    const t = ARC_START + sweep * i / 10 - Math.PI / 2;
    const major = i === 0 || i === 5 || i === 10;
    radialLine (cx, cy, trackR + 2, major ? outer : outer - 2, t,
                major ? 1.4 : 1.0,
                i / 10 <= norm + 0.001 ? withAlpha (fill, 0.9) : withAlpha (skin.textDim, 0.45));
  }

  arc (cx, cy, trackR, ARC_START, ARC_END, 3.0, skin.knobTrack);
  if (norm > 0.002)
    arc (cx, cy, trackR, ARC_START, ARC_START + norm * sweep, 3.0, fill);

  if (material) {
    if (skin.finish === 'phenolic') {
      const wr = bodyR * 1.13;
      const grad = ctx.createLinearGradient (cx - wr, cy - wr, cx + wr, cy + wr);
      grad.addColorStop (0, skin.washer);
      grad.addColorStop (1, darker (skin.washer));
      ctx.beginPath();
      ctx.arc (cx, cy, wr, 0, Math.PI * 2);
      ctx.fillStyle = grad;
      ctx.fill();
      canvasArc (cx, cy, wr, 0, Math.PI * 2, 0.8, withAlpha (skin.bevelLo, 0.75));
      canvasArc (cx, cy, wr - 0.6, Math.PI, Math.PI * 2, 0.8, withAlpha (skin.spec, 0.45));
    } else if (skin.finish === 'glowing') {
      const hr = bodyR * 1.10;
      ctx.beginPath();
      for (let i = 0; i < 6; ++i) {
        const a = i * (Math.PI * 2 / 6) + 0.26;
        const px = cx + Math.cos (a) * hr, py = cy + Math.sin (a) * hr;
        if (i === 0) ctx.moveTo (px, py); else ctx.lineTo (px, py);
      }
      ctx.closePath();
      ctx.fillStyle = withAlpha (skin.bevelLo, 0.85);
      ctx.fill();
      ctx.strokeStyle = withAlpha (skin.bevelHi, 0.55);
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  }

  const d = bodyR * 2;
  if (! material) {
    const grad = ctx.createLinearGradient (cx - bodyR, cy - bodyR, cx + bodyR, cy + bodyR);
    grad.addColorStop (0, skin.bodyTop);
    grad.addColorStop (1, skin.bodyBottom);
    ctx.beginPath();
    ctx.arc (cx, cy, bodyR, 0, Math.PI * 2);
    ctx.fillStyle = grad;
    ctx.fill();
    canvasArc (cx, cy, bodyR, 0, Math.PI * 2, 1.0, withAlpha (fill, 0.5));
  } else {
    const grad = ctx.createLinearGradient (cx, cy - bodyR, cx, cy + bodyR);
    grad.addColorStop (0, skin.bevelHi);
    grad.addColorStop (0.16, skin.bodyTop);
    grad.addColorStop (0.80, skin.bodyBottom);
    grad.addColorStop (1, skin.bevelLo);
    ctx.beginPath();
    ctx.arc (cx, cy, bodyR, 0, Math.PI * 2);
    ctx.fillStyle = grad;
    ctx.fill();
    if (skin.finish === 'phenolic') {
      // The cream print ring where the dome meets the skirt.
      canvasArc (cx, cy, bodyR - 0.9, 0.35, Math.PI - 0.35, 1.1, withAlpha (skin.skirt, 0.55));
    } else {
      canvasArc (cx, cy, bodyR, 0, Math.PI * 2, 1.0, withAlpha (skin.skirt, 0.85));
    }
  }

  // Material face plate (knurled rings, phenolic seams, glowing lines), rotated
  // with the value like the native SkinTextures::knobFace.
  if (material && detail) {
    const kf = (typeof textures !== 'undefined') ? textures.knobFace (skin, Math.round (bodyR * 2)) : null;
    if (kf !== null) {
      ctx.save();
      ctx.translate (cx, cy);
      ctx.rotate (angle);
      ctx.globalAlpha = 0.85;
      ctx.drawImage (kf, -bodyR, -bodyR, bodyR * 2, bodyR * 2);
      ctx.globalAlpha = 1;
      ctx.restore();
    }
  }

  // The rotated face and knurling are the SkinTextures cache on the native
  // side; here the vector relief is what survives: bevel, specular, pointer.
  if (detail) {
    // Console wears its dome plain: the half-circle bevel reads as two rings
    // on the glass there, so it stays on the machined finishes only.
    if (skin.finish !== 'glowing') {
      canvasArc (cx, cy, bodyR - 0.5, Math.PI, Math.PI * 2, 1.0, withAlpha (skin.bevelHi, 0.70));
      canvasArc (cx, cy, bodyR - 0.5, 0, Math.PI, 1.0, withAlpha (skin.bevelLo, 0.45));
    }
    canvasArc (cx, cy, bodyR * 0.82, -2.45, -0.75, 2.2, withAlpha (skin.spec, 0.45));
    canvasArc (cx, cy, bodyR * 0.82, 0.55, 1.65, 1.2, withAlpha (skin.spec, 0.16));
    if (skin.knurl > 0) {
      const ribs = Math.max (12, Math.round (skin.knurl * bodyR / 11));
      for (let i = 0; i < ribs; ++i) {
        const a = angle + i * (Math.PI * 2 / ribs);
        radialLine (cx, cy, bodyR * 0.86, bodyR * 0.99, a, 0.7, withAlpha (skin.grain, 0.5));
      }
    }
  }

  const cosA = Math.cos (angle), sinA = Math.sin (angle);
  const x0 = cx + cosA * bodyR * 0.55, y0 = cy + sinA * bodyR * 0.55;
  const x1 = cx + cosA * bodyR * 0.90, y1 = cy + sinA * bodyR * 0.90;
  if (skin.finish === 'plastic') {
    line (x0, y0, x1, y1, 2.5, skin.text);
  } else if (skin.finish === 'phenolic') {
    line (x0 + 0.7, y0 + 0.7, x1 + 0.7, y1 + 0.7, 3.2, withAlpha (skin.bevelLo, 0.85));
    line (x0, y0, x1, y1, 2.2, skin.pointer);
  } else if (skin.finish === 'glowing') {
    line (x0 - 1.4, y0 - 1.4, x1 + 1.4, y1 + 1.4, 4.6, withAlpha (skin.pointer, 0.35));
    line (x0, y0, x1, y1, 2.2, skin.pointer);
  } else {
    line (x0, y0, x1, y1, 2.2, skin.bevelLo);
    line (x0 - 0.7, y0 - 0.7, x1 - 0.7, y1 - 0.7, 1.1, withAlpha (skin.pointer, 0.95));
  }

  const px = cx + cosA * trackR, py = cy + sinA * trackR;
  if (skin.finish === 'glowing') {
    dot (px, py, 7, withAlpha (fill, 0.10));
    dot (px, py, 4, withAlpha (fill, 0.22));
  }
  dot (px, py, 2.4, fill);

  text (knob.label, nameRect, 10, skin.textDim, { bold: true });
  text (paramText (knob.id), valueRect, 10, skin.text);
  ctx.restore();
}

function dot (cx, cy, r, colour) {
  ctx.beginPath();
  ctx.arc (cx, cy, r, 0, Math.PI * 2);
  ctx.fillStyle = colour;
  ctx.fill();
}

/** The knobs the editor greys when a layer is bypassed. */
function knobLive (id) {
  if (id === SYNC_KNOB_ID)
    return Math.round (state.params.Sync || 0) === 1;
  if (id === 'GateAtk' || id === 'GateRel' || id === 'GateMix')
    return gateLive();
  return true;
}

function gateLive () {
  return (state.params.GateOn || 0) >= 0.5;
}

function analogLive () {
  return (state.params.AnalogOn || 0) >= 0.5;
}

// ---------------------------------------------------------------------------
// The combos, the toggles and the header

function comboText (combo) {
  if (combo.id === undefined) {
    if (combo.key === 'base') return noteName (sequence.baseMidi);
    if (combo.key === 'scale') return SCALE_NAMES[Math.min (SCALE_NAMES.length - 1, sequence.scaleIndex)];
    if (combo.key === 'steps') return String (sequence.steps.length);
    return '';
  }
  const list = state.choices[combo.id] || [];
  const index = Math.round (state.params[combo.id] || 0);
  return list[index] === undefined ? String (index) : list[index];
}

function drawComboFrame (rect, text_, enabled) {
  fillRR (rect.x, rect.y, rect.w, rect.h, skin.corner, skin.panel);
  strokeRR (rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1, skin.corner, skin.panelOutline, 1);

  const size = Math.min (13, rect.h * 0.62);
  ctx.save();
  if (! enabled) ctx.globalAlpha = 0.45;
  text (fit (text_, rect.w - 38, size, false),
        { x: rect.x + 5, y: rect.y, w: rect.w - 38, h: rect.h }, size, skin.text, { align: 'left' });

  // The chevron of drawComboBox: three points in a 20 px zone at the right.
  const arrow = { x: rect.x + rect.w - 30, y: rect.y, w: 20, h: rect.h };
  ctx.strokeStyle = withAlpha (skin.accent, enabled ? 0.9 : 0.2);
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  ctx.moveTo (arrow.x + 3, arrow.y + arrow.h / 2 - 2);
  ctx.lineTo (arrow.x + arrow.w / 2, arrow.y + arrow.h / 2 + 3);
  ctx.lineTo (arrow.x + arrow.w - 3, arrow.y + arrow.h / 2 - 2);
  ctx.stroke();
  ctx.restore();
}

function drawParamCombo (combo) {
  let enabled = true;
  if (combo.id === 'GateDiv') enabled = gateLive();
  if (combo.id === 'Sync') enabled = true;
  drawComboFrame (combo.rect, comboText (combo), enabled);
}

function drawSeqCombo (combo) {
  drawComboFrame (combo.rect, comboText (combo), true);
}

/** A TextButton as JUCE 8 paints it: a rounded plate, no outline, the label
    in the off colour and the accent when the parameter it mirrors is on. */
function drawButton (item) {
  const rect = item.rect;
  const on = item.on !== undefined ? item.on
    : item.id !== undefined ? buttonOn (item.id) : false;
  const live = !(item.id === 'Play' && !manualSync())
    && !(item.id === 'AnalogHiss' && !analogLive())
    && !(item.id === 'LimOn' && !on);

  ctx.save();
  if (! live) ctx.globalAlpha = 0.45;

  const chip = item.chip === true;
  const base = chip ? skin.chip : skin.panel;
  fillRR (rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1, 5,
          on ? withAlpha (ink (item.accent || 'Accent'), 0.30) : base);

  const label = on && item.onLabel ? item.onLabel
    : ! on && item.offLabel ? item.offLabel : item.label;
  const size = Math.min (13, rect.h * 0.62);
  text (fit (label, rect.w - 6, size, false), rect, size,
        on ? ink (item.accent || 'Accent') : (chip ? skin.chipText : skin.text));
  ctx.restore();
}

function buttonOn (id) {
  return (state.params[id] || 0) >= 0.5;
}

function manualSync () {
  return Math.round (state.params.Sync || 0) === 1;
}

/** The header row: logo, skin, help, the preset field with its arrows, the
    transport, the tempo readout and the master meter. */
function drawHeader () {
  drawLogo (HEADER.logo);
  drawButton ({ label: '?', rect: HEADER.help });

  fillRR (HEADER.presetField.x, HEADER.presetField.y, HEADER.presetField.w, HEADER.presetField.h,
          skin.corner, skin.panel);
  strokeRR (HEADER.presetField.x + 0.5, HEADER.presetField.y + 0.5,
            HEADER.presetField.w - 1, HEADER.presetField.h - 1, skin.corner, skin.panelOutline, 1);
  text (fit (presetCaption(), HEADER.presetField.w - 8, 11, true),
        HEADER.presetField, 11, skin.text, { bold: true });

  drawButton ({ label: '<', rect: HEADER.presetPrev });
  drawButton ({ label: '>', rect: HEADER.presetNext });

  // The tempo: the readout the timer writes, with the accent for manual play.
  // Under Sync = Host the clock follows the DAW, so the number is the host's,
  // not the knob's — the native label prints the same thing.
  const manual = manualSync();
  const shown = manual ? (state.params.BPM || 0) : (state.tempo || state.params.BPM || 0);
  text (shown.toFixed (1) + ' BPM  ' + (manual ? 'manual' : 'host'), HEADER.tempo, 11,
        manual ? skin.accentAlt : skin.textDim, { align: 'right', bold: true });

  drawMeter (HEADER.meter);
}

/** The logo: a decaying saw, five cycles, the filter knee closing over it. */
function drawLogo (rect) {
  const r = reduced (rect, 1);
  const over = hoverLogo;
  fillRR (r.x, r.y, r.w, r.h, skin.corner, over ? brighter (skin.panel) : skin.panel);
  strokeRR (r.x, r.y, r.w, r.h, skin.corner, over ? skin.accent : brighter (skin.panelOutline), 1.3);

  const inner = { x: r.x + r.w * 0.20, y: r.y + r.h * 0.26, w: r.w * 0.60, h: r.h * 0.48 };
  ctx.strokeStyle = over ? skin.accent : skin.text;
  ctx.lineWidth = 1.5;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  const steps = 64;
  for (let i = 0; i <= steps; ++i) {
    const t = i / steps;
    const x = inner.x + t * inner.w;
    const saw = 2 * (t * 5 - Math.floor (t * 5)) - 1;
    const amp = Math.exp (-2.6 * t) * saw;
    const y = inner.y + inner.h / 2 - amp * inner.h * 0.48;
    if (i === 0) ctx.moveTo (x, y); else ctx.lineTo (x, y);
  }
  ctx.stroke();
  ctx.lineCap = 'butt';
}

/** The header meter: RMS as a bar, the held peak as a line, GR as a strip. */
function drawMeter (rect) {
  const fraction = (db) => Math.min (1, Math.max (0, (db + 60) / 60));
  fillRR (rect.x, rect.y, rect.w, rect.h, skin.corner, darker (skin.background));

  const barX = rect.x + 4, barY = rect.y + rect.h * 0.35;
  const barW = rect.w - 8, barH = rect.h * 0.55;
  fillRR (barX, barY, barW, barH, 2, withAlpha (skin.knobTrack, 0.55));

  const xRed = barX + barW * fraction (-6);
  fillRect (barX, barY, xRed - barX, barH, withAlpha (skin.meterGreen, 0x30 / 255));
  fillRect (xRed, barY, barX + barW - xRed, barH, withAlpha (skin.meterAmber, 0x30 / 255));

  const grad = ctx.createLinearGradient (barX, 0, barX + barW, 0);
  grad.addColorStop (0, skin.meterGreen);
  grad.addColorStop (fraction (-6), skin.meterAmber);
  grad.addColorStop (1, skin.meterRed);
  ctx.fillStyle = grad;
  ctx.fillRect (barX + 1, barY + 1, Math.max (0, barW * fraction (ballistics.rmsDb) - 2), barH - 2);

  const clipped = ballistics.peakDb > -0.5;
  fillRect (barX + barW * fraction (ballistics.peakHoldDb) - 1, barY, 2, barH,
            clipped ? skin.gate : withAlpha (skin.playhead, 0.85));

  fillRect (barX, rect.y + rect.h - 4, barW, 2,
            ballistics.grDb > 0.5 ? withAlpha (skin.accent, 0.9) : skin.knobTrack);
}

// ---------------------------------------------------------------------------
// The scopes

function scopeFrame (rect) {
  const area = reduced (rect, 1);
  fillRR (area.x, area.y, area.w, area.h, skin.corner, skin.panel);
  strokeRR (area.x, area.y, area.w, area.h, skin.corner, skin.panelOutline, 1);
  return area;
}

/** One cycle of the oscillator: sine through saw to square, the same blend
    the voice uses (ScopeComponent::shapeAt). */
function drawWaveScope (rect) {
  const r = reduced (scopeFrame (rect), 2);
  const morph = Math.min (1, Math.max (0, state.params.Wave === undefined ? 0.5 : state.params.Wave));

  line (r.x, r.y + r.h / 2, r.x + r.w, r.y + r.h / 2, 0.7, skin.gridLine);
  for (let i = 1; i < 4; ++i) {
    const x = Math.round (r.x + r.w * i / 4);
    line (x, r.y, x, r.y + r.h, 1, i === 2 ? skin.gridBeat : skin.gridLine);
  }

  const samples = Math.max (96, Math.round (r.w / 2));
  const centreY = r.y + r.h / 2;
  ctx.save();
  clipRect (r);
  ctx.beginPath();
  ctx.moveTo (r.x, centreY);
  for (let i = 0; i <= samples; ++i)
    ctx.lineTo (r.x + (i / samples) * r.w, centreY - shapeAt (morph, i / samples) * r.h * 0.42);
  ctx.lineTo (r.x + r.w, centreY);
  ctx.closePath();
  ctx.fillStyle = withAlpha (skin.accent, 0.13);
  ctx.fill();

  ctx.beginPath();
  for (let i = 0; i <= samples; ++i) {
    const x = r.x + (i / samples) * r.w;
    const y = centreY - shapeAt (morph, i / samples) * r.h * 0.42;
    if (i === 0) ctx.moveTo (x, y); else ctx.lineTo (x, y);
  }
  ctx.strokeStyle = skin.accent;
  ctx.lineWidth = 1.6;
  ctx.stroke();
  ctx.restore();

  drawCaption (r, waveName (morph));
}

function shapeAt (morph, t) {
  const saw = 2 * t - 1;
  const square = t < 0.5 ? 1 : -1;
  const sine = Math.sin (Math.PI * 2 * t);
  if (morph <= 0.5)
    return (1 - morph * 2) * sine + morph * 2 * saw;
  const k = (morph - 0.5) * 2;
  return (1 - k) * saw + k * square;
}

function waveName (morph) {
  if (morph < 0.04) return 'SINE';
  if (morph > 0.96) return 'SQUARE';
  if (Math.abs (morph - 0.5) < 0.04) return 'SAW';
  return morph < 0.5 ? 'SINE-SAW' : 'SAW-SQUARE';
}

const K_FREQ_MIN = 20, K_FREQ_MAX = 20000, K_DB_TOP = 25.46, K_DB_BOTTOM = -48;
const K_MAX_Q = 18.5;

/** The lowpass response. The plugin sends the same 96 points the native
    formula evaluates, so the curve here is the native curve. */
function drawFilterScope (rect) {
  const r = reduced (scopeFrame (rect), 2);
  const gutter = scale >= 0.5 ? 15 : 0;
  const plot = { x: r.x + gutter, y: r.y, w: r.w - gutter, h: r.h };
  layout.filterPlot = plot;

  const dbToY = (db) => plot.y + plot.h - (db - K_DB_BOTTOM) / (K_DB_TOP - K_DB_BOTTOM) * plot.h;
  const freqToX = (f) => plot.x + Math.log (f / K_FREQ_MIN) / Math.log (K_FREQ_MAX / K_FREQ_MIN) * plot.w;

  const cutoff = state.params.Cutoff || 0;
  const res = state.params.Res || 0;
  const slope = state.params.Slope || 0;
  const points = Math.max (96, Math.round (plot.w / 2));

  ctx.save();
  clipRect (plot);

  ctx.beginPath();
  ctx.moveTo (plot.x, plot.y + plot.h);
  for (let i = 0; i <= points; ++i) {
    const t = i / points;
    ctx.lineTo (plot.x + t * plot.w, dbToY (curveDbAt (t)));
  }
  ctx.lineTo (plot.x + plot.w, plot.y + plot.h);
  ctx.closePath();
  ctx.fillStyle = withAlpha (skin.accentAlt, 0.13);
  ctx.fill();

  // The tint went down first: a line composited over it would not land on the
  // palette literal any more, and the literals are what the checks compare.
  let lastX = -100;
  for (const f of gridFrequencies()) {
    const x = Math.round (freqToX (f));
    if (x - lastX < 3)
      continue;
    lastX = x;
    line (Math.min (Math.max (x, plot.x), plot.x + plot.w - 1), plot.y,
          Math.min (Math.max (x, plot.x), plot.x + plot.w - 1), plot.y + plot.h, 1,
          isMajorFrequency (f) ? skin.gridBeat : skin.gridLine);
  }
  for (const db of [12, 0, -12, -24, -36])
    line (plot.x, Math.round (dbToY (db)), plot.x + plot.w, Math.round (dbToY (db)), 1,
          db === 0 ? skin.gridBeat : skin.gridLine);

  ctx.beginPath();
  for (let i = 0; i <= points; ++i) {
    const t = i / points;
    const x = plot.x + t * plot.w, y = dbToY (curveDbAt (t));
    if (i === 0) ctx.moveTo (x, y); else ctx.lineTo (x, y);
  }
  ctx.strokeStyle = skin.accentAlt;
  ctx.lineWidth = 1.5;
  ctx.stroke();
  ctx.restore();

  if (gutter > 0) {
    for (const db of [12, 0, -12, -24, -36])
      text (db.toFixed (0), { x: r.x, y: dbToY (db) - 6, w: gutter - 2, h: 12 }, 7,
            skin.textDim, { align: 'right' });
    for (const f of [100, 1000, 10000])
      text (f >= 1000 ? (f / 1000) + 'k' : String (f),
            { x: freqToX (f) - 16, y: plot.y + plot.h - 11, w: 32, h: 10 }, 7, skin.textDim);
  }

  const hover = layout.filterHover;
  const hovering = hover !== undefined && hover >= plot.x && hover <= plot.x + plot.w;
  if (hovering)
    line (Math.round (hover), plot.y, Math.round (hover), plot.y + plot.h, 1, withAlpha (skin.playhead, 0.4));

  const hoverFreq = hovering ? K_FREQ_MIN * Math.pow (K_FREQ_MAX / K_FREQ_MIN, (hover - plot.x) / plot.w) : 0;
  const summary = hovering
    ? Math.round (hoverFreq) + ' Hz  ' + (curveDbAtFreq (hoverFreq) >= 0 ? '+' : '')
      + curveDbAtFreq (hoverFreq).toFixed (1) + ' dB'
    : Math.round (cutoff) + ' Hz  Q' + (K_MAX_Q * res * res + 0.5).toFixed (1)
      + '  ' + Math.round (12 + 12 * slope) + ' dB/oct';
  drawCaption (r, summary);
}

/** The horizontal mini faders of the FX strip, the flat look the native
    drawLinearSlider paints: a track, the filled part in the accent ink and a
    small square thumb. */
function drawFaders () {
  for (const entry of layout.faders) {
    const { fader, rect } = entry;
    const fill = ink (fader.role);
    const norm = Math.min (1, Math.max (0, normalise (fader.id, state.params[fader.id] || 0)));
    const trackX = rect.x + 2, trackW = rect.w - 4;
    const cy = rect.y + rect.h / 2;

    fillRR (trackX, cy - 2, trackW, 4, 2, skin.knobTrack);
    if (norm > 0)
      fillRR (trackX, cy - 2, Math.max (2, trackW * norm), 4, 2, fill);

    const px = trackX + trackW * norm;
    fillRR (px - 3, cy - 5, 6, 10, 2, skin.text);
    strokeRR (px - 2.5, cy - 4.5, 5, 9, 2, withAlpha (skin.panelOutline, 0.6), 1);
  }
}

/** The plugin's 96 log-spaced points, read at a phase of the whole axis. */
function curveDbAt (t) {
  const curve = state.curve;
  if (! curve || curve.length === 0)
    return K_DB_BOTTOM;
  const position = t * (curve.length - 1);
  const low = Math.floor (position), high = Math.min (curve.length - 1, low + 1);
  return curve[low] + (curve[high] - curve[low]) * (position - low);
}

function curveDbAtFreq (freq) {
  const t = Math.log (freq / K_FREQ_MIN) / Math.log (K_FREQ_MAX / K_FREQ_MIN);
  return curveDbAt (Math.min (1, Math.max (0, t)));
}

let gridFrequencyCache = null;

function gridFrequencies () {
  if (gridFrequencyCache === null) {
    const out = [];
    for (let decade = 10; decade <= 10000; decade *= 10)
      for (let step = 1; step <= 9; ++step) {
        const f = decade * step;
        if (f >= K_FREQ_MIN && f <= K_FREQ_MAX)
          out.push (f);
      }
    out.sort ((a, b) => a - b);
    gridFrequencyCache = out;
  }
  return gridFrequencyCache;
}

function isMajorFrequency (f) {
  const lead = f / Math.pow (10, Math.floor (Math.log10 (f)));
  return Math.abs (lead - 1) < 0.001 || Math.abs (lead - 2) < 0.001 || Math.abs (lead - 5) < 0.001;
}

function clipRect (rect) {
  ctx.beginPath();
  ctx.rect (Math.round (rect.x), Math.round (rect.y),
            Math.round (rect.w), Math.round (rect.h));
  ctx.clip();
}

/** A label in the corner of a plot, on a chip of the palette's own label
    pair — the trace runs behind it. */
function drawCaption (rect, string) {
  const size = 8.5;
  const pad = 3;
  const w = measure (string, size, true);
  const box = { x: rect.x + rect.w - pad * 2 - w, y: rect.y + pad,
                w: w + pad * 2, h: size + pad };
  fillRR (box.x, box.y, box.w, box.h, 2, skin.chip);
  text (string, box, size, skin.chipText, { bold: true });
}

// ---------------------------------------------------------------------------
// The gate: sixteen squares, one per 1/16

function drawGateGrid () {
  const pocket = skin.panelMaterial !== 'flat';
  const active = meter.sector;
  const running = active >= 0;

  for (let i = 0; i < layout.gateCells.length; ++i) {
    const r = layout.gateCells[i];
    const lit = running && i === active;
    const closed = gateClosed (i);
    fillRR (r.x, r.y, r.w, r.h, skin.corner * 0.5, closed
      ? withAlpha (skin.gate, lit ? 1.0 : 0.72)
      : lit ? withAlpha (skin.accent, 0.45) : withAlpha (skin.background, 0.55));

    if (lit) {
      strokeRR (r.x, r.y, r.w, r.h, skin.corner * 0.5, skin.playhead, 2.0);
      continue;
    }
    // An open step is a pocket cut into the plate: the shadow hugs the top
    // edge, the lip below catches the light.
    strokeRR (r.x, r.y, r.w, r.h, skin.corner * 0.5,
              pocket ? withAlpha (skin.bevelLo, 0.85) : skin.panelOutline, 1.0);
    if (pocket)
      line (r.x + skin.corner * 0.5, r.y + r.h - 1, r.x + r.w - skin.corner * 0.5,
            r.y + r.h - 1, 1.0, withAlpha (skin.bevelHi, 0.30));
  }
}

// ---------------------------------------------------------------------------
// The pattern grid

function drawSequencer () {
  const area = scopeFrame (PLOT.seqEditor);
  const plot = layout.grid;
  const rowH = layout.rowH;
  const steps = Math.max (1, sequence.steps.length);
  const stepW = plot.w / steps;
  const playing = meter.running ? meter.step : -1;

  for (let s = 0; s < steps; ++s) {
    const x = plot.x + s * stepW;
    line (x, plot.y, x, plot.y + plot.h, s % 4 === 0 ? 1.2 : 0.7,
          s % 4 === 0 ? skin.gridBeat : skin.gridLine);
    if (s % 4 === 3)
      line (x + stepW, plot.y, x + stepW, plot.y + plot.h, 0.7, withAlpha (skin.gridBeat, 0.5));

    if (s === playing) {
      fillRect (x, plot.y, stepW, plot.h, withAlpha (skin.playhead, 0.10));
      fillRect (x, plot.y - 5, stepW, 3, skin.playhead);
    }
  }

  for (let r = 0; r < PITCH_ROWS; ++r) {
    const y = plot.y + (PITCH_ROWS - 1 - r) * rowH;
    const base = r === 0;
    if (base)
      fillRect (plot.x, y, plot.w, rowH, withAlpha (skin.accent, 0.14));

    text (noteName (midiForDegree (r)),
          { x: area.x + 2, y, w: layout.gutter - 4, h: rowH }, 9,
          base ? skin.accent : skin.textDim, { bold: base });
    line (plot.x, y, plot.x + plot.w, y, 0.6, withAlpha (skin.gridLine, 0.6));
  }

  for (let s = 0; s < steps; ++s) {
    const x = plot.x + s * stepW;
    const code = sequence.steps[s] || 0;

    if ((code & NOTE_FLAG) !== 0) {
      const degree = code & 0x7f;
      if (degree < PITCH_ROWS) {
        const y = plot.y + (PITCH_ROWS - 1 - degree) * rowH;
        const vel = velocityOf (s);
        fillRR (x + 1.5, y + 1.5, stepW - 3, rowH - 3, 3,
                withAlpha (skin.accent, 0.2 + 0.8 * vel));
        if ((code & ACCENT_FLAG) !== 0)
          fillRect (x + stepW / 2 - 2, y + rowH / 2 - 2, 4, 4, '#ffffff');
        if ((code & SLIDE_FLAG) !== 0)
          strokeRR (x + 3, y + 3, stepW - 6, rowH - 6, 2, skin.background, 1.2);
        if (vel < 0.55 || drag !== null && drag.vel === s)
          text (Math.round (vel * 100) + '%',
                { x, y: y + rowH / 2 - 6, w: stepW, h: 12 }, 8.5, skin.textDim);
      }
    } else if (code === TIE) {
      fillRR (x + stepW * 0.18, layout.pitchBottom + rowH * 0.5 - 1.5, stepW * 0.64, 3,
              1.5, skin.accentAlt);
    }

    if ((code & NOTE_FLAG) !== 0) {
      if ((code & SLIDE_FLAG) !== 0)
        text ('SL', { x, y: layout.pitchBottom + rowH, w: stepW, h: rowH }, 9.5, skin.pitch, { bold: true });
      if ((code & ACCENT_FLAG) !== 0)
        text ('AC', { x, y: layout.pitchBottom + rowH * 2, w: stepW, h: rowH }, 9.5,
             skin.accentAlt, { bold: true });

      const velRowY = layout.pitchBottom + rowH * 3;
      const barH = rowH * 0.7;
      const vel = velocityOf (s);
      fillRR (x + stepW * 0.22, velRowY + rowH * 0.15, stepW * 0.56, barH, 2, skin.gridLine);
      fillRR (x + stepW * 0.22, velRowY + rowH * 0.15 + barH * (1 - vel), stepW * 0.56,
              Math.max (0, barH * vel), 2, withAlpha (skin.accent, 0.25 + 0.75 * vel));
    }
  }

  for (const row of layout.flagRows)
    text (row.label, { x: area.x + 2, y: row.y, w: layout.gutter - 4, h: rowH }, 9,
          skin.textDim, { align: 'left', bold: true });

  text ('BASE ' + noteName (sequence.baseMidi) + '  |  '
        + (SCALE_NAMES[Math.min (SCALE_NAMES.length - 1, sequence.scaleIndex)] || ''),
        { x: plot.x, y: area.y, w: plot.w, h: 18 }, 9.5, skin.textDim, { align: 'left', bold: true });
}

function velocityOf (index) {
  const raw = sequence.velocity[index];
  return raw === undefined ? 1.0 : Math.min (1, Math.max (0, raw / 127));
}

function setVelocity (index, value) {
  while (sequence.velocity.length < sequence.steps.length)
    sequence.velocity.push (127);
  sequence.velocity[index] = Math.round (Math.min (1, Math.max (0.05, value)) * 127);
}

/** The credit column: the note where there is one, the factory attribution
    where there is not. Wrapped, because JUCE 8 took word wrap out of Label. */
function drawCredit () {
  const rect = PLOT.credit;
  const shown = state.note || creditOfLoadedPreset();
  const words = String (shown).split (/\s+/).filter ((w) => w !== '');
  const lines = [];
  let lineText = '';
  for (const word of words) {
    const trial = lineText === '' ? word : lineText + ' ' + word;
    if (measure (trial, 9, false) > rect.w - 2 && lineText !== '') {
      lines.push (lineText);
      lineText = word;
    } else {
      lineText = trial;
    }
  }
  if (lineText !== '')
    lines.push (lineText);

  lines.slice (0, 2).forEach ((row, index) =>
    text (row, { x: rect.x + 1, y: rect.y + index * 11, w: rect.w - 2, h: 11 }, 9, skin.textDim,
          { align: 'left' }));
}

function creditOfLoadedPreset () {
  const index = bank.names.indexOf (state.preset);
  return index >= 0 ? (bank.credits[index] || '') : '';
}

function presetCaption () {
  if (! bankDirty)
    return state.preset || 'Init';
  return loadedUser ? state.preset + ' *' : 'User*';
}

// ---------------------------------------------------------------------------
// The user bank: the browser's own storage, the same JSON the native files hold

const USER_STORE = 'spiral303.userPresets';

function readUserStore () {
  try {
    const raw = window.localStorage.getItem (USER_STORE);
    const parsed = raw ? JSON.parse (raw) : null;
    return parsed && typeof parsed === 'object' ? parsed : {};
  } catch (error) {
    return {};
  }
}

function writeUserStore (store) {
  try { window.localStorage.setItem (USER_STORE, JSON.stringify (store)); } catch (error) {}
  return store;
}

function userPresetNames () {
  return Object.keys (readUserStore()).sort ((a, b) => a.localeCompare (b));
}

function gateText () {
  const hex = gate.closedMask.toString (16).toUpperCase().padStart (4, '0');
  return 'g1:' + hex + ':' + (gate.rotation || 0) + ':' + gate.numSectors;
}

/** The preset as the native bank stores it: numbers only in params, so the
    plugin's own reader accepts the text without a special case. */
function currentPresetJson (name) {
  const params = {};
  for (const key of Object.keys (state.params))
    params[key] = state.params[key];
  return JSON.stringify ({ format: 1, name, params, sequence: sequenceText (),
                           gate: gateText (), credit: state.note || '',
                           comment: 'saved on the page' });
}

function saveUserPreset (name) {
  const store = readUserStore();
  store[name] = currentPresetJson (name);
  writeUserStore (store);
  loadedUser = name;
  bankDirty = false;
  send ({ t: 'presetText', json: store[name] });
}

function loadUserPreset (name) {
  const text = readUserStore()[name];
  if (text === undefined) {
    showCard ('Spiral303', 'Cannot load that preset.');
    return;
  }
  loadedUser = name;
  bankDirty = false;
  send ({ t: 'presetText', json: text });
}

function deleteUserPreset (name) {
  const store = readUserStore();
  delete store[name];
  writeUserStore (store);
  if (loadedUser === name)
    loadedUser = null;
}

function loadFactoryPreset (name) {
  loadedUser = null;
  bankDirty = false;
  send ({ t: 'preset', name });
}

function cyclePreset (delta) {
  const names = bank.names;
  if (names.length === 0)
    return;
  const index = names.indexOf (state.preset);
  const next = (index < 0 ? 0 : (index + delta + names.length) % names.length);
  loadFactoryPreset (names[next]);
}

// ---------------------------------------------------------------------------
// The popups: a menu anchored to the control that opened it

const MENU_ROW = 24;    // PresetMenuItem::getIdealSize
const MENU_PAD = 2;
const MENU_SEP = 9;

function visibleArea () {
  const o = origin();
  return { x: -o.x / scale, y: -o.y / scale,
           w: canvas.clientWidth / scale, h: canvas.clientHeight / scale };
}

function menuRows (items) {
  const rows = [];
  let y = MENU_PAD;
  for (let index = 0; index < items.length; ++index) {
    const item = items[index];
    if (item.separator)
      rows.push ({ separator: true, y, h: MENU_SEP });
    else
      rows.push ({ item, index, y, h: MENU_ROW });
    y += item.separator ? MENU_SEP : MENU_ROW;
  }
  return { rows, height: y + MENU_PAD };
}

function menuRect () {
  const area = visibleArea();
  const metrics = menuRows (popup.items);
  const width = Math.min (popup.width, area.w);
  const height = Math.min (metrics.height, Math.max (40, area.h - 8));
  const anchor = popup.anchor;

  let x = anchor.x;
  let y = anchor.y + anchor.h + 1;
  // A combo shows the row in play where the pointer is, the way the native
  // ComboBox places its list; a menu simply opens under its control.
  if (popup.alignSelected !== undefined && metrics.rows[popup.alignSelected])
    y = anchor.y - metrics.rows[popup.alignSelected].y - MENU_ROW / 2;
  // A header combo sits a row from the top, so aligning the current row on it
  // pushes the earlier ones off the canvas: clamp the list back into the view
  // the way the native ComboBox keeps it on the screen.
  if (y < area.y)
    y = area.y;
  if (y + height > area.y + area.h)
    y = Math.max (area.y, anchor.y - height - 1);
  if (x + width > area.x + area.w)
    x = area.x + area.w - width;
  if (x < area.x)
    x = area.x;

  popup.scroll = Math.min (Math.max (0, metrics.height - height), popup.scroll || 0);
  return { x, y, w: width, h: height, rows: metrics.rows, inner: metrics.height };
}

function menuRowAt (point) {
  if (popup === null)
    return -1;
  const r = menuRect();
  if (! hit (point.x, point.y, r))
    return -1;
  const local = point.y - r.y + popup.scroll;
  for (const row of r.rows) {
    if (row.separator || local < row.y || local >= row.y + row.h)
      continue;
    return row.index;
  }
  return -1;
}

function openMenu (options) {
  popup = { anchor: options.anchor, items: options.items, width: options.width || 260,
            index: -1, scroll: 0, alignSelected: options.alignSelected };
}

function closeMenu () { popup = null; }

function pickMenuItem (index) {
  const item = popup === null ? null : popup.items[index];
  if (item === null || item === undefined || item.separator)
    return;
  const action = item.pick;
  const armedDelete = item.deletePick !== undefined && popup.deleteArmed === true;
  closeMenu();
  if (armedDelete)
    item.deletePick();
  else if (typeof action === 'function')
    action();
}

/** A row as PresetMenuItem paints it: the loaded preset inverts its colours, the
    row under the pointer takes the menu highlight, and the delete hint only
    shows while hovering. */
function drawMenuRow (rect, item, hovered) {
  const box = { x: rect.x + 8, y: rect.y + 4, w: rect.w - 16, h: rect.h - 8 };
  const selected = item.selected === true;
  let colour = skin.text;
  if (hovered) {
    fillRR (box.x, box.y, box.w, box.h, skin.corner, skin.accent);
    colour = skin.background;
  } else if (selected) {
    fillRR (box.x, box.y, box.w, box.h, skin.corner, skin.text);
    colour = skin.background;
  }

  const label = item.label;
  const hintRoom = hovered && item.deletePick !== undefined ? 110 : 0;
  const textRect = { x: box.x + 4, y: box.y, w: box.w - 8 - hintRoom, h: box.h };
  text (fit (label, textRect.w, MENU_FONT_SIZE, false), textRect, MENU_FONT_SIZE,
        item.enabled === false ? withAlpha (colour, 0.4) : colour, { align: 'left' });

  if (hintRoom > 0)
    text ('right-click = delete', { x: box.x + box.w - 110, y: box.y, w: 106, h: box.h },
          10, hovered ? withAlpha (skin.background, 0.75) : skin.textDim, { align: 'right' });
}

const MENU_FONT_SIZE = 13;

function drawPopup () {
  if (popup === null)
    return;
  const r = menuRect();
  fillRR (r.x, r.y, r.w, r.h, skin.corner, skin.panel);
  strokeRR (r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, skin.corner, skin.accent, 1);

  ctx.save();
  clipRect ({ x: r.x, y: r.y, w: r.w, h: r.h });
  for (const row of r.rows) {
    const rect = { x: r.x, y: r.y + row.y - popup.scroll, w: r.w, h: row.h };
    if (rect.y + rect.h < r.y || rect.y > r.y + r.h)
      continue;
    if (row.separator) {
      line (rect.x + 8, rect.y + rect.h / 2, rect.x + rect.w - 8, rect.y + rect.h / 2,
            1.0, withAlpha (skin.panelOutline, 0.9));
      continue;
    }
    drawMenuRow (rect, row.item, row.index === popup.index);
  }
  ctx.restore();

  // A list taller than the window keeps its own ruler on the right edge.
  if (r.inner > r.h) {
    const share = r.h / r.inner;
    const thumb = Math.max (14, r.h * share);
    const track = r.h - 4;
    const at = (popup.scroll / Math.max (1, r.inner - r.h)) * (track - thumb);
    fillRR (r.x + r.w - 4, r.y + 2 + at, 2.5, thumb, 1.25, withAlpha (skin.textDim, 0.8));
  }
}

// ---------------------------------------------------------------------------
// The concrete menus, each anchored at its own control

function openInfoMenu () {
  openMenu ({ anchor: HEADER.logo, width: 260, items: [
    { label: 'About Spiral303...', pick: () => openCard ({ kind: 'about' }) },
    { label: 'User manual (RU / EN)...', pick: () => openCard ({ kind: 'manual' }) },
  ]});
}

function openPresetMenu () {
  const items = [];
  for (const name of bank.names)
    items.push ({ label: name, selected: name === state.preset && ! bankDirty,
                  pick: () => loadFactoryPreset (name) });
  items.push ({ separator: true });
  items.push ({ label: 'Save current settings as preset...', pick: promptSavePreset });

  const users = userPresetNames();
  if (users.length > 0) {
    items.push ({ separator: true });
    for (const name of users)
      items.push ({ label: name, selected: name === loadedUser && ! bankDirty,
                    pick: () => loadUserPreset (name),
                    deletePick: () => confirmDeletePreset (name) });
  }

  openMenu ({ anchor: HEADER.presetField, width: 260, items });
  popup.deleteArmed = false;
}

function promptSavePreset () {
  askBox = { kind: 'prompt', title: 'Save Preset', message: 'Enter a name for the new preset:',
             label: 'Preset name:', value: '', caret: 0, hover: 0,
             buttons: [ { label: 'OK', result: true, action: (value) => {
               const name = String (value).trim();
               if (name !== '')
                 saveUserPreset (name);
             } },
             { label: 'Cancel', result: false } ] };
}

function confirmDeletePreset (name) {
  askBox = { kind: 'confirm', title: 'Delete preset',
             message: 'Delete the user preset "' + name + '"?', value: '', hover: 0,
             buttons: [ { label: 'Delete', result: true, action: () => deleteUserPreset (name) },
                        { label: 'Cancel', result: false } ] };
}

function promptValue (knob, entry) {
  const prefill = paramRawText (knob.id);
  askBox = { kind: 'prompt', title: knob.label || knob.id, message: 'Enter an exact value:',
             label: 'Value', value: prefill, caret: prefill.length, hover: 0, selected: true,
             anchorRect: entry.valueRect,
             buttons: [ { label: 'OK', result: true,
                          action: (value) => {
                            const parsed = Number (value);
                            if (! Number.isFinite (parsed))
                              return;
                            const range = paramRanges[knob.id] || { min: 0, max: 1 };
                            sendParam (knob.id, Math.min (range.max, Math.max (range.min, parsed)));
                          } },
                        { label: 'Cancel', result: false } ] };
}

function openStepMenu (step) {
  const cell = { x: layout.grid.x + step * (layout.grid.w / sequence.steps.length),
                 y: layout.grid.y, w: layout.grid.w / sequence.steps.length,
                 h: layout.grid.h };
  openMenu ({ anchor: cell, width: 224, items: [
    { label: 'Tie (hold previous note)', pick: () => writeStep (step, TIE) },
    { label: 'Rest', pick: () => writeStep (step, REST) },
    { label: 'Slide', pick: () => toggleFlag (step, SLIDE_FLAG) },
    { label: 'Accent', pick: () => toggleFlag (step, ACCENT_FLAG) },
  ]});
}

function seqChoices (key) {
  if (key === 'base') {
    const list = [];
    for (let midi = 24; midi <= 72; ++midi)
      list.push (noteName (midi));
    return list;
  }
  if (key === 'scale')
    return SCALE_NAMES;
  return STEP_COUNTS.map (String);
}

function seqChoiceIndex (key) {
  if (key === 'base')
    return Math.min (48, Math.max (0, sequence.baseMidi - 24));
  if (key === 'scale')
    return Math.min (SCALE_NAMES.length - 1, sequence.scaleIndex);
  return Math.max (0, STEP_COUNTS.indexOf (sequence.steps.length));
}

function chooseSeq (key, index) {
  if (key === 'base')
    sequence.baseMidi = 24 + index;
  else if (key === 'scale')
    sequence.scaleIndex = index;
  else
    setStepCount (STEP_COUNTS[Math.min (STEP_COUNTS.length - 1, index)]);
  sendSequence();
}

function openComboMenu (combo) {
  const isParam = combo.id !== undefined;
  const choices = isParam ? (state.choices[combo.id] || []) : seqChoices (combo.key);
  const current = isParam ? Math.round (state.params[combo.id] || 0) : seqChoiceIndex (combo.key);
  const items = choices.map ((label, index) => ({
    label: String (label), selected: index === current,
    pick: () => (isParam ? sendParam (combo.id, index) : chooseSeq (combo.key, index)),
  }));
  openMenu ({ anchor: combo.rect, width: Math.max (combo.rect.w, 140), items,
              alignSelected: current });
}

function sendParam (id, value) {
  send ({ t: 'param', id, v: value });
}

/** Where a hand movement begins and ends. The host records automation, so it
    has to be told that the values between the two are one gesture: a knob turn
    is one point on the lane, not a hundred. */
function sendGesture (id, begin) {
  send ({ t: 'gesture', id, begin });
}

// ---------------------------------------------------------------------------
// The overlay cards: About, the manual, and a plain message

function wrap (string, width, size, bold) {
  const paragraphs = String (string).split ('\n');
  const lines = [];
  for (const paragraph of paragraphs) {
    let lineText = '';
    for (const word of paragraph.split (/\s+/).filter ((w) => w !== '')) {
      const trial = lineText === '' ? word : lineText + ' ' + word;
      if (measure (trial, size, bold) > width && lineText !== '') {
        lines.push (lineText);
        lineText = word;
      } else {
        lineText = trial;
      }
    }
    lines.push (lineText);
  }
  return lines;
}

const ABOUT_TEXT = () =>
  'Spiral303 ' + (state.version || '') + ' - acid synthesizer and sequencer\n\n'
  + 'Author: Spiralfractal Konstantin Chizhov\n'
  + 'Contact: konstantin.qi@gmail.com\n\n'
  + 'Morphing oscillator, resonant contour filter, distortion,\n'
  + 'a 303-style pattern editor with slide, accent and tie,\n'
  + 'analog drift and a 16-square gate grid.';

// The manual of ManualWindow.h, in the same order and the same colours; the
// gate section says what the panel actually is since the wheel became a grid.
const MANUAL_SECTIONS = [
  { ru: 'Секвенсор', en: 'Sequencer', role: 'Pitch',
    ruText: 'Паттерн до 32 шагов. ЛКМ по шагу ставит или снимает ноту, ПКМ открывает меню '
      + 'шага: TIE (держать предыдущую ноту), Rest, SL (слайд) и AC (акцент). Нижние ряды '
      + 'TIE / SL / AC / VEL тоже рисуются мышью. Длина паттерна: 8 / 12 / 16 / 24 / 32 '
      + 'шагов (список STEPS).\n'
      + 'NOTE и SCALE задают базовую ноту и одну из 12 гамм. Все правки применяются на лету, '
      + 'без остановки игры.\n'
      + 'Velocity каждой ноты: потяните ноту вертикально — вверх громче, вниз тише; яркость '
      + 'полоски показывает уровень. Наведение мыши читает число в процентах.',
    enText: 'Up to 32 steps. Left-click toggles a note on a step; right-click opens the step '
      + 'menu with Tie (hold the previous note), Rest, SL (slide) and AC (accent). The lower '
      + 'TIE / SL / AC / VEL rows can be painted too. Pattern length: 8 / 12 / 16 / 24 / 32 '
      + 'steps (STEPS).\n'
      + 'NOTE and SCALE set the root note and one of 12 scales. Every edit is applied on the '
      + 'fly while the sequencer keeps running.\n'
      + 'Per-note velocity: drag a note vertically — louder up, quieter down; the bar\'s '
      + 'brightness shows the level. Hovering reads the number in per cent.' },
  { ru: 'Гейт-сетка', en: 'Gate grid', role: 'Gate',
    ruText: '16 квадратов сетки 4×4, по одному на 1/16: тёмно-красные — ноты приглушены. '
      + 'Клики и протяжка включают и выключают квадраты, список GATE DIV делит такт на '
      + '1/2/4/8/16 фигур. В пресетах маска хранится как g1:<HEX>:<rotation>. Кнопка ON '
      + 'пропускает эффект целиком.',
    enText: '16 squares in a 4×4 grid, one per 1/16: the dark red ones mute the note under '
      + 'them. Clicks and drags toggle squares, the GATE DIV selector divides the bar into '
      + '1/2/4/8/16 figures. Presets store the mask as g1:<HEX>:<rotation>. The ON button '
      + 'bypasses the whole effect.' },
  { ru: 'Голос и фильтр', en: 'Voice and filter', role: 'Accent',
    ruText: 'WAVE — морф синус → пила → квадрат. CUTOFF — частота среза, которую огибающая '
      + 'открывает на каждой ноте (40–8000 Гц). Res — резонанс. Env — глубина открытия '
      + 'фильтра огибающей.\n'
      + 'FILTER (селектор под ручкой Cutoff) — модель фильтра: Classic (SVF), Diode Ladder, '
      + 'Moog Ladder, TPT SVF, Sallen-Key, One-Pole, Vowel, Diode 303. АЧХ под ручкой Slope — тоже '
      + 'ручка: тяните по графику, колесо меняет срез.',
    enText: 'WAVE morphs sine → saw → square. CUTOFF is the corner the contour envelope '
      + 'opens on every note (40–8000 Hz). Res is the resonance. Env is the squelch depth.\n'
      + 'FILTER (the selector under the Cutoff knob) picks the lowpass model: Classic (SVF), '
      + 'Diode Ladder, Moog Ladder, TPT SVF, Sallen-Key, One-Pole, Vowel, Diode 303. The response plot '
      + 'is a control too: drag across it for cutoff and resonance, the wheel for cutoff.' },
  { ru: 'Огибающая и аналог', en: 'Envelope and analog', role: 'AccentAlt',
    ruText: 'Contour — длина огибающей среза как доля шага. Accent — прибавка уровня и '
      + 'открытия фильтра на AC-шагах. Glide — длина слайда на SL-шагах. Warmth — сила '
      + 'аналогового дрейфа: высота тона, срез, уровень и Q фильтра плавно плавают.',
    enText: 'Contour is the decay length of the filter envelope as a fraction of a step. '
      + 'Accent raises the level and filter opening of AC steps. Glide is the slide length '
      + 'on SL steps. Warmth drives the analog drift: pitch, cutoff, level and the filter Q '
      + 'slowly wander.' },
  { ru: 'Дисторшн и выход', en: 'Distortion and output', role: 'Pitch',
    ruText: 'Drive — вход клиппера (уровень голоса внутри фиксирован на полном заводском), '
      + 'Dist Mix — сухая/мокрая смесь. Ряд SAT выбирает тип: Tanh, Drive, Triode, Fold, '
      + 'Master Clip, Tape, Tape Warm, Cassette, Reel, Tape Hot, Crunch и Clean (точный '
      + 'bypass). Gain — уровень мастера, Lim — лимитер на выходе.',
    enText: 'Drive pushes the input of the clipper (the voice level sits at the full factory '
      + 'setting inside), Dist Mix blends it in. The SAT row picks the type: Tanh, Drive, '
      + 'Triode, Fold, Master Clip, Tape, Tape Warm, Cassette, Reel, Tape Hot, Crunch and '
      + 'Clean (an exact bypass). Gain is the master level, Lim the output limiter.' },
  { ru: 'Транспорт', en: 'Transport', role: 'AccentAlt',
    ruText: 'SYNC = HOST — темп и позиция берутся у хоста, ручка BPM серая и зеркалит темп '
      + 'хоста. SYNC = MANUAL — работают ручка BPM и кнопка PLAY. Swing сдвигает каждый '
      + 'второй шаг, CLOCK — делитель частоты шагов.\n'
      + 'Кнопка SEQ: выключена — плагин живёт как обычный инструмент, секвенсор не '
      + 'запускается, ноты играются напрямую. Включена — сетка играет как обычно.',
    enText: 'SYNC = HOST follows the host tempo and play head; the BPM knob greys out and '
      + 'mirrors the host tempo. SYNC = MANUAL enables the BPM knob and the PLAY button. '
      + 'Swing delays every second step; CLOCK divides the step rate.\n'
      + 'The SEQ button: off, the plugin behaves as a plain instrument and the internal '
      + 'sequencer stands down; on, the grid plays as usual.' },
  { ru: 'Пресеты', en: 'Presets', role: 'Gate',
    ruText: '102 фабричные программы в меню по клику правым на поле имени. «Save current '
      + 'settings as preset…» пишет пресет в память страницы; пользовательские пресеты '
      + 'удаляются оттуда же правым кликом по строке. ←/→ листают банк.',
    enText: '102 factory programs in the menu opened on the preset field. "Save current '
      + 'settings as preset…" stores a preset in the page\'s own storage; user presets are '
      + 'deleted from the same menu with a right click on the row. Left and right arrows '
      + 'page through the bank.' },
];

let manualRussian = false;

function openCard (card_) {
  card = card_;
  card.scroll = card.scroll || 0;
  if (card.kind === 'manual')
    card.russian = manualRussian;
}

function cardContent (kind) {
  if (kind === 'about' || kind === 'message')
    return { title: kind === 'about' ? 'About Spiral303' : (card.title || 'Spiral303'),
             lines: wrap (kind === 'about' ? ABOUT_TEXT() : card.message, 372, 12, false)
                      .map ((row) => ({ text: row, size: 12, colour: skin.text })) };
  const lines = [];
  for (const section of MANUAL_SECTIONS) {
    lines.push ({ heading: true, text: card.russian ? section.ru : section.en,
                  role: section.role, size: 14 });
    const body = card.russian ? section.ruText : section.enText;
    for (const row of wrap (body, 560, 11.5, false))
      lines.push ({ text: row, size: 11.5, colour: skin.text, gap: 1 });
    lines.push ({ text: '', size: 6, colour: skin.text });
  }
  return { title: 'Spiral303 - user manual', lines };
}

/** The card rectangle: centred over the visible window, never larger than it. */
function cardRect () {
  const area = visibleArea();
  const wide = card !== null && card.kind === 'manual';
  const w = Math.min (wide ? 620 : 420, area.w - 20);
  const h = Math.min (wide ? 520 : 300, area.h - 20);
  return { x: area.x + (area.w - w) / 2, y: area.y + (area.h - h) / 2, w, h };
}

function cardCloseRect (rect) {
  return { x: rect.x + rect.w - 30, y: rect.y + 8, w: 22, h: 22 };
}

function cardLangRect (rect) {
  return { x: rect.x + rect.w - 84, y: rect.y + 8, w: 46, h: 22 };
}

function cardButtonRects (rect) {
  const buttons = askBox === null ? [] : askBox.buttons;
  const rects = [];
  let right = rect.x + rect.w - 12;
  for (let index = buttons.length - 1; index >= 0; --index) {
    const w = Math.max (56, measure (buttons[index].label, 12, false) + 22);
    right -= w;
    rects[index] = { x: right, y: rect.y + rect.h - 36, w, h: 24 };
    right -= 8;
  }
  return rects;
}

function scrim () {
  const area = visibleArea();
  fillRect (area.x, area.y, area.w, area.h, withAlpha (skin.shadow, 0.45));
}

function cardFrame (rect, title) {
  fillRR (rect.x, rect.y, rect.w, rect.h, skin.corner, skin.panel);
  strokeRR (rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1, skin.corner, skin.accent, 1.2);
  text (title, { x: rect.x + 14, y: rect.y + 8, w: rect.w - 100, h: 22 }, 14,
       skin.text, { align: 'left', bold: true });
  line (rect.x + 10, rect.y + 34, rect.x + rect.w - 10, rect.y + 34, 1,
        withAlpha (skin.panelOutline, 1));
  drawButton ({ label: '×', rect: cardCloseRect (rect) });
}

function drawCard () {
  if (card === null)
    return;
  scrim();
  const rect = cardRect();
  const content = cardContent (card.kind);
  fillRR (rect.x, rect.y, rect.w, rect.h, skin.corner, skin.panel);
  strokeRR (rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1, skin.corner, skin.accent, 1.2);
  text (content.title, { x: rect.x + 14, y: rect.y + 8, w: rect.w - 150, h: 22 }, 14,
        skin.text, { align: 'left', bold: true });
  if (card.kind === 'manual')
    drawButton ({ label: card.russian ? 'EN' : 'RU', rect: cardLangRect (rect) });
  drawButton ({ label: '×', rect: cardCloseRect (rect) });
  line (rect.x + 10, rect.y + 34, rect.x + rect.w - 10, rect.y + 34, 1,
        withAlpha (skin.panelOutline, 1));

  const body = { x: rect.x + 14, y: rect.y + 40, w: rect.w - 28, h: rect.h - 54 };
  ctx.save();
  clipRect (body);
  let y = body.y - card.scroll;
  for (const line_ of content.lines) {
    const h = line_.size + 3;
    if (y + h > body.y && y < body.y + body.h) {
      if (line_.heading)
        text (fit (line_.text, body.w, line_.size, true), { x: body.x, y, w: body.w, h },
              line_.size, ink (line_.role), { align: 'left', bold: true });
      else
        text (line_.text, { x: body.x, y, w: body.w, h }, line_.size, line_.colour,
              { align: 'left' });
    }
    y += h;
  }
  ctx.restore();

  const tall = y - body.y;
  if (tall > body.h)
    card.maxScroll = tall - body.h;
}

function closeCard () { card = null; }

// ---------------------------------------------------------------------------
// The dialogs: prompt and confirm, the way AlertWindow looks

function askRect () {
  const area = visibleArea();
  const w = Math.min (380, area.w - 20);
  const lines = askBox === null ? [] : wrap (askBox.message, w - 28, 12, false);
  const h = Math.min (78 + lines.length * 15 + (askBox !== null && askBox.kind === 'prompt' ? 34 : 0),
                      area.h - 20);
  return { x: area.x + (area.w - w) / 2, y: area.y + (area.h - h) / 2, w, h, lines };
}

function askEditRect (rect) {
  return { x: rect.x + 14, y: rect.y + rect.h - 74, w: rect.w - 28, h: 24 };
}

function askLabelRect (rect) {
  return { x: rect.x + 14, y: rect.y + rect.h - 96, w: rect.w - 28, h: 14 };
}

function drawAsk () {
  if (askBox === null)
    return;
  scrim();
  const frame = askRect();
  const rect = { x: frame.x, y: frame.y, w: frame.w, h: frame.h };
  cardFrame (rect, askBox.title);

  let y = rect.y + 42;
  for (const row of frame.lines) {
    text (row, { x: rect.x + 14, y, w: rect.w - 28, h: 15 }, 12, skin.text, { align: 'left' });
    y += 15;
  }

  if (askBox.kind === 'prompt') {
    text (askBox.label, askLabelRect (rect), 10, skin.textDim, { align: 'left' });
    const edit = askEditRect (rect);
    fillRR (edit.x, edit.y, edit.w, edit.h, skin.corner, skin.chip);
    strokeRR (edit.x + 0.5, edit.y + 0.5, edit.w - 1, edit.h - 1, skin.corner, skin.accent, 1);
    const caret = measure (askBox.value.slice (0, askBox.caret), 12, false);
    if (askBox.selected) {
      // The prefill of a double-clicked knob is held selected, as JUCE's
      // AlertWindow text box is, so the first keystroke replaces it.
      const width = Math.min (measure (askBox.value, 12, false), edit.w - 8);
      fillRR (edit.x + 4, edit.y + 4, width, edit.h - 8, 2, skin.accent);
      text (askBox.value, { x: edit.x + 4, y: edit.y, w: edit.w - 8, h: edit.h }, 12,
            skin.background, { align: 'left' });
    } else {
      text (askBox.value, { x: edit.x + 4, y: edit.y, w: edit.w - 8, h: edit.h }, 12,
            skin.text, { align: 'left' });
      line (edit.x + 4 + caret, edit.y + 4, edit.x + 4 + caret, edit.y + edit.h - 4, 1,
            skin.playhead);
    }
  }

  const rects = cardButtonRects (rect);
  askBox.buttons.forEach ((button, index) =>
    drawButton ({ label: button.label, rect: rects[index],
                  on: index === askBox.hover, accent: 'accent' }));
}

function askButtonAt (point) {
  if (askBox === null)
    return -1;
  const rects = cardButtonRects (cardRectForAsk());
  for (let index = 0; index < rects.length; ++index)
    if (hit (point.x, point.y, rects[index]))
      return index;
  return -1;
}

function cardRectForAsk () {
  const frame = askRect();
  return { x: frame.x, y: frame.y, w: frame.w, h: frame.h };
}

function pressAskButton (index) {
  if (askBox === null)
    return;
  const button = askBox.buttons[index];
  if (button === undefined)
    return;
  const box = askBox;
  askBox = null;
  if (button.result && typeof button.action === 'function')
    button.action (box.value);
}

// ---------------------------------------------------------------------------
// Writing the pattern

function isNoteStep (index) {
  return ((sequence.steps[index] || 0) & NOTE_FLAG) !== 0;
}

function degreeOf (index) {
  return (sequence.steps[index] || 0) & 0x7f;
}

function writeStep (index, code) {
  if (index < 0 || index >= sequence.steps.length)
    return;
  sequence.steps[index] = code;
  bankDirty = true;
  sendSequence ();
}

function toggleFlag (index, flag) {
  if (! isNoteStep (index))
    return;
  const code = sequence.steps[index];
  writeStep (index, (code & flag) !== 0 ? code & ~flag : code | flag);
}

function setStepCount (count) {
  const steps = sequence.steps.slice (0, count);
  while (steps.length < count)
    steps.push (REST);
  const velocity = sequence.velocity.slice (0, count);
  while (velocity.length < count)
    velocity.push (127);
  sequence.steps = steps;
  sequence.velocity = velocity;
  bankDirty = true;
  sendSequence ();
}

/** The raw text an AlertWindow would have pre-filled: the same digits as the
    label, without the unit the CLAP host appends. */
function paramRawText (id) {
  const range = paramRanges[id] || { min: 0, max: 1, decimals: 2 };
  const value = state.params[id];
  return (value === undefined ? range.min : value).toFixed (range.decimals === undefined
    ? 2 : range.decimals);
}

// ---------------------------------------------------------------------------
// The pointer

let hoverLogo = false;
let gateLast = -1;              // the square a drag has already toggled
let armVel = -1;                // the note a vertical drag is turning louder
let armStartY = 0;
let armStartVel = 1.0;
let gridWrote = false;          // the press wrote, so mouseUp must not erase

function canvasPoint (event) {
  const o = origin();
  return { x: (event.clientX - o.x) / scale, y: (event.clientY - o.y) / scale };
}

function knobAt (p) {
  return layout.knobs.find ((entry) => hit (p.x, p.y, entry.rect)) || null;
}

function faderAt (p) {
  return (layout.faders || []).find ((entry) => hit (p.x, p.y, entry.rect)) || null;
}

function comboAt (p) {
  return layout.combos.find ((c) => hit (p.x, p.y, c.rect))
      || layout.seqCombos.find ((c) => hit (p.x, p.y, c.rect)) || null;
}

function controlAt (p) {
  return layout.buttons.find ((b) => hit (p.x, p.y, b.rect))
      || layout.actions.find ((a) => hit (p.x, p.y, a.rect)) || null;
}

function gateAt (p) {
  return layout.gateCells.findIndex ((cell) => hit (p.x, p.y, cell));
}

function stepAt (x) {
  const plot = layout.grid;
  const steps = Math.max (1, sequence.steps.length);
  const stepW = plot.w / steps;
  if (x < plot.x || stepW <= 0)
    return -1;
  const index = Math.floor ((x - plot.x) / stepW);
  return index >= steps ? -1 : index;
}

/** The row under the pointer: a pitch row, or one of the four flag rows. */
function rowAt (y) {
  const plot = layout.grid;
  const rowH = layout.rowH;
  if (y >= layout.pitchBottom) {
    const index = Math.floor ((y - layout.pitchBottom) / rowH);
    return { degree: -1, flag: index >= 0 && index < FLAG_ROWS ? index : -1 };
  }
  if (y < plot.y)
    return { degree: -1, flag: -1 };
  const degree = Math.min (PITCH_ROWS - 1, Math.max (0, Math.floor ((layout.pitchBottom - y) / rowH)));
  return { degree, flag: -1 };
}

function velocityFromY (y) {
  const row = layout.flagRows[3];
  return Math.min (1, Math.max (0.05, 1.0 - (y - row.y) / layout.rowH));
}

function toggleGate (index) {
  if (index < 0 || index === gateLast)
    return;
  gate.closedMask ^= (1 << index);
  gateLast = index;
  bankDirty = true;
  sendGate ();
}

function toggleParam (id) {
  // Play belongs to the manual clock. Under Sync = Host the host's transport is
  // the run line, and a button that did nothing would be a lie: the native
  // editor dims it for the same reason.
  if (id === 'Play' && ! manualSync())
    return;

  sendParam (id, buttonOn (id) ? 0 : 1);
}

function doAction (id) {
  if (id === 'export')
    send ({ t: 'render' });
  else if (id === 'midi') {
    const button = ACTIONS.find ((a) => a.id === 'midi');
    openMenu ({ anchor: button.rect, width: 220, items: [
      { label: 'Import MIDI file...', pick: chooseMidiFile },
      { label: 'Export pattern as MIDI...', pick: exportMidi },
    ]});
  }
  else if (id === 'random')
    ask ('confirm', 'Randomise', 'Rewrite the pattern with random notes?',
         () => { randomise (Date.now ()); bankDirty = true; sendSequence (); }, 'Random', 'Cancel');
  else if (id === 'clear')
    ask ('confirm', 'Clear', 'Erase the pattern and the gate?',
         () => {
           sequence.steps.fill (REST);
           sequence.velocity.fill (127);
           gate.closedMask = 0;
           bankDirty = true;
           sendSequence (); sendGate ();
         }, 'Clear', 'Cancel');
}

/** The click that renames the loaded preset: a factory name becomes a copy, a
    user name renames the file in place — both live in the page's own storage. */
function editPresetName () {
  const initial = bankDirty && ! loadedUser ? '' : (state.preset || '');
  openValueInput (HEADER.presetField, initial, (value) => {
    const name = value.trim ();
    if (name === '' || name === initial)
      return;
    if (loadedUser !== null && loadedUser !== name && ! bankDirty) {
      const store = readUserStore ();
      const text = store[loadedUser];
      delete store[loadedUser];
      store[name] = text.replace (/"name":"[^"]*"/, '"name":' + JSON.stringify (name));
      writeUserStore (store);
      loadedUser = name;
      send ({ t: 'presetText', json: store[name] });
      return;
    }
    saveUserPreset (name);
  });
}

// ---------------------------------------------------------------------------
// The gestures

function pressAt (p, event) {
  if (askBox !== null) {
    const index = askButtonAt (p);
    if (index >= 0)
      pressAskButton (index);
    return;
  }

  if (card !== null) {
    const rect = cardRect();
    if (hit (p.x, p.y, cardCloseRect (rect)))
      closeCard();
    else if (card.kind === 'manual' && hit (p.x, p.y, cardLangRect (rect))) {
      manualRussian = ! manualRussian;
      card.russian = manualRussian;
      card.scroll = 0;
    } else if (! hit (p.x, p.y, rect))
      closeCard();
    return;
  }

  if (popup !== null) {
    const index = menuRowAt (p);
    if (index >= 0) {
      const item = popup.items[index];
      if (event.button === 2 && item !== undefined && item.deletePick !== undefined)
        popup.deleteArmed = true;
      pickMenuItem (index);
    } else
      closeMenu();
    return;
  }

  if (event.button === 2) {
    if (hit (p.x, p.y, HEADER.logo))
      openInfoMenu ();
    else if (hit (p.x, p.y, HEADER.presetField))
      openPresetMenu ();
    else if (hit (p.x, p.y, layout.grid)) {
      const step = stepAt (p.x);
      if (step >= 0)
        openStepMenu (step);
    }
    return;
  }

  if (hit (p.x, p.y, HEADER.logo)) { openInfoMenu (); return; }
  if (hit (p.x, p.y, HEADER.help)) { openCard ({ kind: 'manual' }); return; }
  if (hit (p.x, p.y, HEADER.presetPrev)) { cyclePreset (-1); return; }
  if (hit (p.x, p.y, HEADER.presetNext)) { cyclePreset (1); return; }
  if (hit (p.x, p.y, HEADER.presetField)) { editPresetName (); return; }

  const knob = knobAt (p);
  if (knob !== null) {
    if (! knobLive (knob.knob.id))
      return;
    drag = { kind: 'knob', entry: knob, last: { x: event.clientX, y: event.clientY },
             norm: normalise (knob.knob.id, state.params[knob.knob.id]) };
    hoverKnob = knob.knob.id;
    sendGesture (knob.knob.id, true);
    return;
  }

  const fader = faderAt (p);
  if (fader !== null) {
    // Slider snaps-to-mouse is off: the press remembers where the thumb was
    // and the drag carries that offset, exactly like the native fader.
    drag = { kind: 'fader', entry: fader,
             grabNorm: normalise (fader.fader.id, state.params[fader.fader.id] || 0),
             startX: p.x };
    sendGesture (fader.fader.id, true);
    return;
  }

  const combo = comboAt (p);
  if (combo !== null) {
    if (combo.id === 'GateDiv' && ! gateLive ())
      return;
    openComboMenu (combo);
    return;
  }

  const button = controlAt (p);
  if (button !== null) {
    // Toggles mirror a parameter; the bank buttons run an action. Both carry an
    // id, so the list the button sits in decides which one it is.
    if (layout.actions.includes (button))
      doAction (button.id);
    else
      toggleParam (button.id);
    return;
  }

  const cell = gateAt (p);
  if (cell >= 0) {
    gateLast = -1;
    toggleGate (cell);
    drag = { kind: 'gate' };
    return;
  }

  if (layout.filterPlot !== undefined && hit (p.x, p.y, layout.filterPlot)) {
    filterDrag = { x: event.clientX, y: event.clientY,
                   cutoff: state.params.Cutoff || 0, res: state.params.Res || 0 };
    sendGesture ('Cutoff', true);
    sendGesture ('Res', true);
    return;
  }

  if (hit (p.x, p.y, layout.grid)) {
    const step = stepAt (p.x);
    const row = rowAt (p.y);
    drag = { kind: 'seq', vel: -1 };
    armVel = -1;
    gridWrote = false;
    if (row.flag < 0 && step >= 0 && isNoteStep (step) && degreeOf (step) === row.degree) {
      // A press on a note bar arms a velocity drag; the writing waits for motion.
      armVel = step;
      armStartY = p.y;
      armStartVel = velocityOf (step);
      return;
    }
    gridGesture (p, true);
  }
}

/** SequenceEditorComponent::handleMouse: the armed velocity drag first, then the
    painting, so a horizontal sweep writes notes the way a pencil does. */
function gridGesture (p, isDown) {
  const step = stepAt (p.x);
  if (step < 0)
    return;

  if (! isDown && armVel >= 0) {
    const guard = rowAt (p.y);
    if (guard.flag >= 0 || step !== armVel || Math.abs (p.y - armStartY) * scale < 3
        || ! isNoteStep (armVel) || degreeOf (armVel) !== guard.degree)
      armVel = -1;                       // not a velocity drag; fall through to painting
  }

  if (! isDown && armVel === step && Math.abs (p.y - armStartY) * scale >= 3) {
    setVelocity (armVel, armStartVel + (armStartY - p.y) / (2.2 * layout.rowH));
    drag.vel = armVel;
    gridWrote = true;
    bankDirty = true;
    sendSequence ();
    return;
  }

  paintGrid (step, rowAt (p.y), isDown, p.y);
}

/** The grid gesture of SequenceEditorComponent::handleMouse: paint a note, clear
    it on the second click, toggle a flag row, set velocity from the VEL row. */
function paintGrid (step, row, isDown, y) {
  if (step < 0)
    return;

  if (row.flag >= 0) {
    if (row.flag === 3) {
      if (isNoteStep (step)) {
        setVelocity (step, velocityFromY (y));
        drag.vel = step;
        gridWrote = true;
        bankDirty = true;
        sendSequence ();
      }
      return;
    }
    if (! isDown)
      return;
    if (row.flag === 0) {
      writeStep (step, sequence.steps[step] === TIE ? REST : TIE);
    } else if (row.flag === 1) {
      toggleFlag (step, SLIDE_FLAG);
    } else {
      toggleFlag (step, ACCENT_FLAG);
    }
    gridWrote = true;
    return;
  }

  const code = sequence.steps[step] || 0;
  gridWrote = true;
  if (isNoteStep (step) && degreeOf (step) === row.degree && isDown)
    writeStep (step, REST);
  else
    writeStep (step, NOTE_FLAG | row.degree
                     | (code & SLIDE_FLAG) | (code & ACCENT_FLAG));
}

function moveAt (p, event) {
  hoverLogo = hit (p.x, p.y, HEADER.logo);

  if (drag !== null && drag.kind === 'knob') {
    const dx = event.clientX - drag.last.x;
    const dy = event.clientY - drag.last.y;
    drag.last = { x: event.clientX, y: event.clientY };
    drag.norm = Math.min (1, Math.max (0, drag.norm + knobDragStep (dx, dy, event.shiftKey)));
    sendParam (drag.entry.knob.id, denormalise (drag.entry.knob.id, drag.norm));
    return;
  }

  if (drag !== null && drag.kind === 'fader') {
    // The same offset carries in design units, so the scale cancels out.
    const norm = Math.min (1, Math.max (0,
      drag.grabNorm + (p.x - drag.startX) / drag.entry.rect.w));
    sendParam (drag.entry.fader.id, denormalise (drag.entry.fader.id, norm));
    return;
  }

  if (drag !== null && drag.kind === 'gate') {
    toggleGate (gateAt (p));
    return;
  }

  if (drag !== null && drag.kind === 'seq') {
    gridGesture (p, false);
    return;
  }

  if (filterDrag !== null) {
    const plot = layout.filterPlot;
    const dx = event.clientX - filterDrag.x;
    const dy = event.clientY - filterDrag.y;
    const cutoff = Math.min (8000, Math.max (40, filterDrag.cutoff * Math.pow (10, dx / plot.w * 3)));
    const res = Math.min (1, Math.max (0, filterDrag.res - dy / plot.h));
    sendParam ('Cutoff', cutoff);
    sendParam ('Res', res);
    return;
  }

  // The velocity drag lives in gridGesture, which the seq branch above runs.

  const knob = knobAt (p);
  const hoveredFader = knob === null ? faderAt (p) : null;
  const id = knob !== null ? knob.knob.id
    : hoveredFader !== null ? hoveredFader.fader.id : '';
  if (id !== hoverKnob) {
    hoverKnob = id;
    send ({ t: 'hover', id });
  }
  layout.filterHover = layout.filterPlot !== undefined
    && hit (p.x, p.y, layout.filterPlot) ? p.x : undefined;
}

function releaseAt () {
  if (armVel >= 0 && ! gridWrote) {
    // A click without a drag on a note erases it, exactly as the native grid.
    const step = armVel;
    armVel = -1;
    writeStep (step, REST);
  }
  if (drag !== null && drag.kind === 'knob')
    sendGesture (drag.entry.knob.id, false);
  if (drag !== null && drag.kind === 'fader')
    sendGesture (drag.entry.fader.id, false);
  if (filterDrag !== null) {
    sendGesture ('Cutoff', false);
    sendGesture ('Res', false);
  }
  armVel = -1;
  gateLast = -1;
  filterDrag = null;
  drag = null;
}

/** A knob drag, linear: 200 px for the whole range, a thousand with Shift held
    for the fine pass. JUCE's velocity curve (0.2 * (1 + sin(pi * (1.5 + t))))
    is not worth porting: three pixels of the mouse move a tenth of a per cent,
    and the knob reads as broken. */
function knobDragStep (dx, dy, shift) {
  return (dx - dy) / (shift ? 1000 : 200);
}

/** One notch is the parameter's own interval in its own units, one percent of
    the span where it has none — what KnobSlider::mouseWheelMove does, five
    times as much on Shift. */
function sendKnobStep (knob, notches) {
  const id = knob.id;
  const range = paramRanges[id] || { min: 0, max: 1 };
  const step = (knob.step || (range.max - range.min) * 0.01) * notches;
  const value = Math.min (range.max, Math.max (range.min, (state.params[id] || 0) + step));
  sendParam (id, value);
}

function wheelAt (p, event) {
  const delta = event.deltaY > 0 ? -1 : 1;
  const shift = event.shiftKey;

  if (popup !== null) {
    popup.scroll = Math.max (0, popup.scroll - delta * MENU_ROW * (shift ? 5 : 1));
    return true;
  }
  if (card !== null) {
    card.scroll = Math.max (0, card.scroll - delta * 20 * (shift ? 5 : 1));
    return true;
  }
  if (askBox !== null)
    return true;

  const knob = knobAt (p);
  if (knob !== null) {
    if (! knobLive (knob.knob.id))
      return false;
    sendKnobStep (knob.knob, delta * (shift ? 5 : 1));
    return true;
  }

  const fader = faderAt (p);
  if (fader !== null) {
    sendKnobStep (fader.fader, delta * (shift ? 5 : 1));
    return true;
  }

  if (layout.filterPlot !== undefined && hit (p.x, p.y, layout.filterPlot)) {
    const norm = Math.min (1, Math.max (0, normalise ('Cutoff', state.params.Cutoff)
                                        + delta * 0.01 * (shift ? 5 : 1)));
    sendParam ('Cutoff', denormalise ('Cutoff', norm));
    return true;
  }
  return false;
}

function showCard (title, message) {
  card = { kind: 'message', title, message, scroll: 0 };
}

// ---------------------------------------------------------------------------
// The dialogs, keyboard side

function ask (kind, title, message, action, okLabel, cancelLabel) {
  askBox = { kind, title, message, value: '', caret: 0, hover: 0,
             buttons: [ { label: okLabel || 'OK', result: true, action },
                        { label: cancelLabel || 'Cancel', result: false } ] };
}

function keyInAsk (event) {
  if (event.key === 'Enter') { pressAskButton (0); return true; }
  if (event.key === 'Escape') { askBox = null; return true; }
  if (event.key === 'Backspace') {
    askBox.selected = false;
    askBox.value = askBox.value.slice (0, -1);
    askBox.caret = askBox.value.length;
    return true;
  }
  if (event.key.length === 1 && askBox.kind === 'prompt') {
    // A focused TextEditor with select-all replaces what it holds on the first key.
    askBox.value = askBox.selected ? event.key : askBox.value + event.key;
    askBox.selected = false;
    askBox.caret = askBox.value.length;
    return true;
  }
  return false;
}

function keyInCard (event) {
  if (event.key !== 'Escape')
    return false;
  if (card.kind === 'manual') { closeCard(); return true; }
  closeCard();
  return true;
}

function onKeyDown (event) {
  if (document.activeElement === valueInput)
    return;

  if (askBox !== null) { event.preventDefault(); keyInAsk (event); draw(); return; }
  if (card !== null) { event.preventDefault(); keyInCard (event); draw(); return; }
  if (popup !== null) {
    event.preventDefault();
    const last = popup.items.length - 1;
    if (event.key === 'Escape')
      closeMenu();
    else if (event.key === 'ArrowDown')
      popup.index = Math.min (last, popup.index + 1);
    else if (event.key === 'ArrowUp')
      popup.index = Math.max (0, popup.index - 1);
    else if (event.key === 'Enter' && popup.index >= 0)
      pickMenuItem (popup.index);
    draw();
    return;
  }

  if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
    cyclePreset (event.key === 'ArrowLeft' ? -1 : 1);
    event.preventDefault();
    return;
  }

  // The knob under the pointer answers the arrows, one interval a press and
  // five with Shift — the same steps the wheel uses.
  if (hoverKnob !== '' && (event.key === 'ArrowUp' || event.key === 'ArrowDown')) {
    let entry = layout.knobs.find ((k) => k.knob.id === hoverKnob);
    let knob = entry !== undefined ? entry.knob : undefined;
    if (knob === undefined) {
      const fentry = (layout.faders || []).find ((f) => f.fader.id === hoverKnob);
      knob = fentry !== undefined ? fentry.fader : undefined;
    }
    if (knob !== undefined && knobLive (hoverKnob))
      sendKnobStep (knob, (event.key === 'ArrowUp' ? 1 : -1) * (event.shiftKey ? 5 : 1));
    event.preventDefault();
    return;
  }

  if (event.key === ' ') {
    toggleParam ('Play');
    event.preventDefault();
  }
}

// ---------------------------------------------------------------------------
// The wiring

canvas.addEventListener ('pointerdown', (event) => {
  event.preventDefault();
  // The capture keeps the moves and the release coming to the canvas even when
  // the pointer leaves the panel: a drag that ends because the mouse reached the
  // edge of the window is not a drag, it is a dead control.
  if (canvas.setPointerCapture !== undefined)
    canvas.setPointerCapture (event.pointerId);
  pressAt (canvasPoint (event), event);
  draw();
});

canvas.addEventListener ('contextmenu', (event) => event.preventDefault());

canvas.addEventListener ('pointermove', (event) => {
  if (askBox === null && card === null)
    moveAt (canvasPoint (event), event);
  draw();
});

function endDragEvent (event) {
  releaseAt ();
  if (event.pointerId !== undefined && canvas.releasePointerCapture !== undefined
      && canvas.hasPointerCapture !== undefined && canvas.hasPointerCapture (event.pointerId))
    canvas.releasePointerCapture (event.pointerId);
  draw();
}

canvas.addEventListener ('pointerup', endDragEvent);
canvas.addEventListener ('pointercancel', endDragEvent);

canvas.addEventListener ('dblclick', (event) => {
  const p = canvasPoint (event);
  const knob = knobAt (p);
  if (knob !== null && knobLive (knob.knob.id)) {
    promptValue (knob.knob, knob);
    draw();
    return;
  }
  // A double click on a fader opens the exact-value box, the same as on a knob.
  const fader = faderAt (p);
  if (fader !== null) {
    openValueInput (fader.fader.cell, paramText (fader.fader.id), (value) => {
      const parsed = parseFloat (value);
      if (! isNaN (parsed))
        sendParam (fader.fader.id, parsed);
    });
  }
  draw();
});

canvas.addEventListener ('wheel', (event) => {
  event.preventDefault();
  if (wheelAt (canvasPoint (event), event))
    draw();
}, { passive: false });

window.addEventListener ('keydown', onKeyDown);
window.addEventListener ('resize', fitCanvas);

fitCanvas();
send ({ t: 'hello' });
booted = true;


// Skin textures for the wasm page.
//
// This mirrors Source/gui/SkinTextures.h: every plate is generated from the
// palette's fixed seed and never from the clock, so a given skin renders
// identically every run. The seeded PRNG is JUCE's 48-bit LCG, ported line for
// line, and the hash that seeds it is the same boost-style mix the native
// SkinTextures::seeded() uses, so the grain pattern of a panel matches the
// native editor for the same skin and size.
//
// Loaded before app.js; app.js calls into the `textures` namespace.

  // --- JUCE Random (48-bit LCG) --------------------------------------------
  function juceRandom (seedValue) {
    let seed = BigInt (seedValue) & 0xffffffffffffn;
    return {
      nextInt () {
        seed = (((seed * 0x5deece66dn) + 11n) & 0xffffffffffffn);
        return Number (seed >> 16n);
      },
      nextIntMax (maxValue) {
        return Math.trunc ((((this.nextInt () >>> 0) * BigInt (maxValue)) >> 32n));
      },
      nextFloat () {
        const r = this.nextInt () >>> 0;
        return r / 0x100000000;
      },
      nextDouble () {
        return (this.nextInt () >>> 0) / (0xffffffff + 1.0);
      },
    };
  }

  // --- SkinTextures::seeded hash -------------------------------------------
  function seeded (skinIndex, kind, a, b, grainSeed, version) {
    let h = (grainSeed ^ 0x9e3779b9) >>> 0;
    const mix = (v) => {
      h = (h ^ ((v + 0x9e3779b9 + ((h << 6) >>> 0) + (h >>> 2)) >>> 0)) >>> 0;
      h = Math.imul (h, 0xc2b2ae35) >>> 0;
      h = (h ^ (h >>> 15)) >>> 0;
    };
    mix (skinIndex >>> 0);
    mix (kind >>> 0);
    mix (a >>> 0);
    mix (b >>> 0);
    mix (version >>> 0);
    return juceRandom (h & 0x7fffffff);
  }

  function bucket (v, step) {
    return Math.ceil (v / step) * step;
  }

  // --- tone helpers ---------------------------------------------------------
  function tone (th, kind) {
    // kind: 'face' | 'panel' | 'knob'
    if (kind === 'face') return { base: th.faceBase, hi: th.faceHi, lo: th.faceLo };
    if (kind === 'panel') return { base: th.panel, hi: th.panel, lo: th.panel };
    return { base: th.bodyTop, hi: th.bevelHi, lo: th.bevelLo };
  }

  // shadeVertical: gradient hi -> lo
  function shadeVertical (ctx, t, w, h, strength) {
    const mid = h * 0.45;
    const g1 = ctx.createLinearGradient (0, 0, 0, mid);
    g1.addColorStop (0, withAlphaTex (t.hi, 0.30 * strength));
    g1.addColorStop (1, withAlphaTex (t.hi, 0));
    ctx.fillStyle = g1;
    ctx.fillRect (0, 0, w, mid);
    const g2 = ctx.createLinearGradient (0, mid, 0, h);
    g2.addColorStop (0, withAlphaTex (t.lo, 0));
    g2.addColorStop (1, withAlphaTex (t.lo, 0.38 * strength));
    ctx.fillStyle = g2;
    ctx.fillRect (0, mid, w, h - mid);
  }

  // --- material generators --------------------------------------------------
  function brushed (ctx, th, t, w, h, rng, strength) {
    ctx.fillStyle = t.base;
    ctx.fillRect (0, 0, w, h);
    shadeVertical (ctx, t, w, h, strength);
    for (let y = 0; y < h; ++y) {
      const r = rng.nextFloat ();
      const heavy = (y % 7) === 0;
      ctx.strokeStyle = r < 0.56 ? withAlphaTex (t.hi, (heavy ? 0.085 : 0.030) * strength * (0.45 + r))
                                 : withAlphaTex (t.lo, (heavy ? 0.085 : 0.030) * strength * (0.45 + r));
      ctx.lineWidth = heavy ? 1 : 0.7;
      ctx.beginPath();
      ctx.moveTo (0, y + 0.5);
      ctx.lineTo (w, y + 0.5);
      ctx.stroke();
    }
    // sheen
    const g = ctx.createRadialGradient (w * 0.22, h * 0.10, 0, w * 0.78, h * 0.62, Math.max (w, h) * 0.7);
    g.addColorStop (0, withAlphaTex (th.spec, 0.085 * strength));
    g.addColorStop (1, withAlphaTex (th.spec, 0));
    ctx.fillStyle = g;
    ctx.fillRect (0, 0, w, h);
    // edge lips
    ctx.strokeStyle = withAlphaTex (t.lo, 0.55 * strength);
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo (0, h - 0.5); ctx.lineTo (w, h - 0.5); ctx.stroke();
    ctx.beginPath(); ctx.moveTo (w - 0.5, 0); ctx.lineTo (w - 0.5, h); ctx.stroke();
    ctx.strokeStyle = withAlphaTex (t.hi, 0.35 * strength);
    ctx.beginPath(); ctx.moveTo (0, 0.5); ctx.lineTo (w, 0.5); ctx.stroke();
  }

  function wood (ctx, th, t, w, h, rng) {
    ctx.fillStyle = t.base;
    ctx.fillRect (0, 0, w, h);
    shadeVertical (ctx, t, w, h, 0.75);
    const lines = Math.max (4, th.woodLines || 4);
    for (let k = 0; k < lines; ++k) {
      let base = (k + 0.5) * h / lines + rng.nextFloat () * 8 - 4;
      const wander = 3.5 + rng.nextFloat () * 11;
      const pts = [];
      for (let x = 0; x <= w; x += 6) {
        const fx = x;
        const y = base
          + Math.sin (fx * 0.021 + k * 1.73) * 2.4
          + Math.sin (fx * 0.0038 + k * 0.7) * wander;
        pts.push ({ x: fx, y });
      }
      const width = 1.1 + rng.nextFloat () * 1.2;
      const band = 6 + rng.nextFloat () * 10;
      const late = rng.nextFloat () < 0.5;
      ctx.lineWidth = band;
      ctx.strokeStyle = late ? withAlphaTex (t.lo, 0.16 + 0.12 * rng.nextFloat())
                            : withAlphaTex (t.hi, 0.13 + 0.09 * rng.nextFloat());
      ctx.beginPath();
      pts.forEach ((p, i) => i === 0 ? ctx.moveTo (p.x, p.y) : ctx.lineTo (p.x, p.y));
      ctx.stroke();
      ctx.lineWidth = width;
      ctx.strokeStyle = withAlphaTex (th.grain, 0.20 + 0.16 * rng.nextFloat());
      ctx.beginPath();
      pts.forEach ((p, i) => i === 0 ? ctx.moveTo (p.x, p.y) : ctx.lineTo (p.x, p.y));
      ctx.stroke();
      // light ring
      ctx.save();
      ctx.translate (0, width + band * 0.30);
      ctx.lineWidth = 1.1;
      ctx.strokeStyle = withAlphaTex (t.hi, late ? 0.20 : 0.11);
      ctx.beginPath();
      pts.forEach ((p, i) => i === 0 ? ctx.moveTo (p.x, p.y) : ctx.lineTo (p.x, p.y));
      ctx.stroke();
      ctx.restore();
    }
    // cathedral arcs
    for (let c = 0; c < 2; ++c) {
      const cy = h * (0.32 + 0.36 * c);
      ctx.lineWidth = 1.6;
      ctx.strokeStyle = withAlphaTex (th.grain, 0.05 + 0.03 * c);
      ctx.beginPath();
      ctx.moveTo (w * 0.04, cy + h * 0.16);
      ctx.quadraticCurveTo (w * 0.5, cy - h * (0.10 + 0.04 * c), w * 0.96, cy + h * 0.16);
      ctx.stroke();
    }
    // pores
    const pores = Math.max (60, Math.trunc (w / 5));
    for (let i = 0; i < pores; ++i) {
      const x = rng.nextFloat () * w;
      const y = rng.nextFloat () * h;
      ctx.fillStyle = withAlphaTex (t.lo, 0.05 + 0.10 * rng.nextFloat ());
      ctx.fillRect (x, y, 1 + rng.nextFloat (), 1);
    }
    // varnish band
    const g = ctx.createLinearGradient (0, h, w, 0);
    g.addColorStop (0, withAlphaTex (th.spec, 0.055));
    g.addColorStop (1, withAlphaTex (th.spec, 0));
    ctx.fillStyle = g;
    ctx.fillRect (0, 0, w, h);
  }

  function composite (ctx, th, t, w, h, edges) {
    ctx.fillStyle = t.base;
    ctx.fillRect (0, 0, w, h);
    shadeVertical (ctx, t, w, h, 0.6);
    for (let y = 0; y < h; y += 3) {
      ctx.strokeStyle = withAlphaTex (t.hi, 0.11);
      ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo (0, y + 0.5); ctx.lineTo (w, y + 0.5); ctx.stroke();
      ctx.strokeStyle = withAlphaTex (t.lo, 0.26);
      ctx.beginPath(); ctx.moveTo (0, y + 1.5); ctx.lineTo (w, y + 1.5); ctx.stroke();
    }
    const r = Math.max (w, h) * 0.75;
    const g = ctx.createRadialGradient (w * 0.5, h * 0.42, 0, w * 0.5 + r, h * 0.42, r);
    g.addColorStop (0, withAlphaTex (th.spec, 0.10));
    g.addColorStop (1, withAlphaTex (t.lo, 0.45));
    ctx.fillStyle = g;
    ctx.fillRect (0, 0, w, h);
    if (edges) {
      const tape = Math.max (10, w * 0.012);
      // hazard tape left and right
      for (const side of [0, 1]) {
        const x0 = side === 0 ? 0 : w - tape;
        ctx.save();
        ctx.beginPath();
        ctx.rect (x0, 0, tape, h);
        ctx.clip();
        ctx.fillStyle = withAlphaTex (th.hazard, 0.55);
        ctx.fillRect (x0, 0, tape, h);
        ctx.fillStyle = withAlphaTex (th.faceLo, 0.80);
        for (let y = -tape; y < h + tape; y += tape * 2) {
          ctx.beginPath();
          ctx.moveTo (x0, y);
          ctx.lineTo (x0 + tape, y + tape);
          ctx.lineTo (x0 + tape, y + tape + tape);
          ctx.lineTo (x0, y + tape);
          ctx.closePath();
          ctx.fill();
        }
        ctx.restore();
      }
    }
  }

  function withAlphaTex (hex, a) {
    // hex like '#rrggbb'
    const r = parseInt (hex.slice (1, 3), 16);
    const g = parseInt (hex.slice (3, 5), 16);
    const b = parseInt (hex.slice (5, 7), 16);
    return 'rgba(' + r + ',' + g + ',' + b + ',' + a + ')';
  }

  // --- cache ----------------------------------------------------------------
  const cache = new Map();
  function cacheKey (skinIndex, kind, a, b, version) {
    return skinIndex + '|' + kind + '|' + a + '|' + b + '|' + version;
  }
  let version = 1;
  function invalidate () { version++; cache.clear(); }

  // skinIndex: index into SKINS array
  function skinIndex (skinObj) {
    // find by reference equality in the global SKINS
    for (let i = 0; i < SKINS.length; ++i)
      if (SKINS[i] === skinObj) return i;
    return 0;
  }

  function getTexture (skinObj, kind, w, h, generator) {
    if (w < 2 || h < 2) return null;
    const bw = bucket (w, 8), bh = bucket (h, 8);
    const si = skinIndex (skinObj);
    const key = cacheKey (si, kind, bw, bh, version);
    if (cache.has (key)) return cache.get (key);
    if (cache.size > 96) cache.clear();
    const rng = seeded (si, kind === 'face' ? 0 : 1, bw, bh, skinObj.grainSeed, version);
    const canvas = document.createElement ('canvas');
    canvas.width = bw; canvas.height = bh;
    const ctx = canvas.getContext ('2d');
    const t = tone (skinObj, kind);
    generator (ctx, skinObj, t, bw, bh, rng);
    cache.set (key, canvas);
    return canvas;
  }

  function face (skinObj, w, h) {
    if (w < 2 || h < 2) return null;
    if (skinObj.faceMaterial === 'flat' || ! skinObj.faceTexture) return null;
    const gen = skinObj.faceMaterial === 'brushed' ? brushed
              : skinObj.faceMaterial === 'wood' ? wood
              : composite;
    const edges = skinObj.faceMaterial === 'composite';
    return getTexture (skinObj, 'face', w, h, (ctx, th, t, bw, bh, rng) => {
      if (th.faceMaterial === 'brushed') brushed (ctx, th, t, bw, bh, rng, 1.0);
      else if (th.faceMaterial === 'wood') wood (ctx, th, t, bw, bh, rng);
      else composite (ctx, th, t, bw, bh, edges);
    });
  }

  function panel (skinObj, w, h) {
    if (w < 2 || h < 2) return null;
    if (skinObj.panelMaterial === 'flat') return null;
    return getTexture (skinObj, 'panel', w, h, (ctx, th, t, bw, bh, rng) => {
      if (th.panelMaterial === 'brushed') brushed (ctx, th, t, bw, bh, rng, 0.55);
      else if (th.panelMaterial === 'wood') wood (ctx, th, t, bw, bh, rng);
      else composite (ctx, th, t, bw, bh, false);
    });
  }


// --- knob face / knurl / screw (mirrors SkinTextures::knobFace/knurl/screw) ---
function knobTone (th) { return { base: th.bodyTop, hi: th.bevelHi, lo: th.bevelLo }; }

function genKnobFace (ctx, th, d, rng) {
  const fd = d, cx = d / 2, cy = d / 2, r = d / 2 - 1;
  const t = knobTone (th);
  if (th.finish === 'metal') {
    // knurled metal: concentric rings
    for (let rr = r * 0.22; rr < r * 0.96; rr += 1.35) {
      ctx.strokeStyle = (rng.nextFloat () < 0.55 ? t.hi : t.lo);
      ctx.globalAlpha = 0.05 + 0.06 * rng.nextFloat ();
      ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.arc (cx, cy, rr, 0, Math.PI * 2); ctx.stroke();
      ctx.globalAlpha = 1;
    }
    ctx.strokeStyle = withAlphaTex (th.spec, 0.10);
    ctx.lineWidth = 0.7;
    ctx.beginPath(); ctx.arc (cx, cy, r * 0.5, 0, Math.PI * 2); ctx.stroke();
  } else if (th.finish === 'phenolic') {
    for (let i = 0; i < 2; ++i) {
      const x = cx + (i === 0 ? -r * 0.42 : r * 0.42);
      ctx.strokeStyle = withAlphaTex (th.spec, 0.07);
      ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo (x, cy - r * 0.85); ctx.lineTo (x, cy + r * 0.85); ctx.stroke();
      ctx.strokeStyle = withAlphaTex (th.bevelLo, 0.12);
      ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.moveTo (x + 1, cy - r * 0.85); ctx.lineTo (x + 1, cy + r * 0.85); ctx.stroke();
    }
    for (let i = 0; i < d; ++i) {
      const a = rng.nextFloat () * Math.PI * 2;
      const rr = r * Math.sqrt (rng.nextFloat ()) * 0.9;
      ctx.fillStyle = withAlphaTex (th.spec, 0.02 + 0.03 * rng.nextFloat ());
      ctx.fillRect (cx + Math.cos (a) * rr, cy + Math.sin (a) * rr, 1, 1);
    }
  } else if (th.finish === 'glowing') {
    for (let y = -r; y < r; y += 2) {
      ctx.strokeStyle = withAlphaTex (th.grain, 0.06);
      ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.moveTo (cx - r, cy + y); ctx.lineTo (cx + r, cy + y); ctx.stroke();
    }
  }
}

function genKnurl (ctx, th, d, rng) {
  const cx = d / 2;
  const rOuter = cx - 0.6;
  const rInner = rOuter - Math.max (3, d * 0.17);
  const spacing = 3;
  const fit = Math.trunc ((Math.PI * 2 * rInner) / spacing);
  const n = Math.max (8, Math.min (Math.max (8, th.knurl), fit));
  const pitch = (Math.PI * 2) / n;
  for (let i = 0; i < n; ++i) {
    const a = i * pitch;
    const cosA = Math.cos (a), sinA = Math.sin (a);
    ctx.strokeStyle = withAlphaTex (th.spec, 0.40 + 0.10 * rng.nextFloat ());
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo (cx + cosA * rInner, cx + sinA * rInner);
    ctx.lineTo (cx + cosA * rOuter, cx + sinA * rOuter);
    ctx.stroke();
    const b = a + pitch * 0.5;
    const cosB = Math.cos (b), sinB = Math.sin (b);
    ctx.strokeStyle = withAlphaTex (knobTone (th).lo, 0.50 + 0.12 * rng.nextFloat ());
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo (cx + cosB * rInner, cx + sinB * rInner);
    ctx.lineTo (cx + cosB * rOuter, cx + sinB * rOuter);
    ctx.stroke();
  }
}

function genScrew (ctx, th, px, rng) {
  const d = px, r = d / 2 - 0.5;
  const g = ctx.createLinearGradient (0, 0, d, d);
  g.addColorStop (0, th.screwHi);
  g.addColorStop (1, th.screw);
  ctx.fillStyle = g;
  ctx.beginPath(); ctx.arc (d / 2, d / 2, r, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = withAlphaTex (th.bevelLo, 0.75);
  ctx.lineWidth = 0.8;
  ctx.beginPath(); ctx.arc (d / 2, d / 2, r, 0, Math.PI * 2); ctx.stroke();
  const a = rng.nextFloat () * Math.PI * 2;
  const dx = Math.cos (a) * r * 0.62, dy = Math.sin (a) * r * 0.62;
  ctx.strokeStyle = withAlphaTex (th.bevelLo, 0.85);
  ctx.lineWidth = 1.3;
  ctx.beginPath();
  ctx.moveTo (d / 2 - dx, d / 2 - dy);
  ctx.lineTo (d / 2 + dx, d / 2 + dy);
  ctx.stroke();
}

function getKnobTexture (skinObj, kind, d, gen) {
  if (d < 8) return null;
  const bw = d;
  const si = 0; // placeholder, replaced below
  const key = 'knob|' + kind + '|' + d + '|' + skinIndex (skinObj) + '|' + version;
  if (cache.has (key)) return cache.get (key);
  if (cache.size > 96) cache.clear();
  const rng = seeded (skinIndex (skinObj), kind === 'knobFace' ? 2 : (kind === 'knurl' ? 3 : 4), d, 0, skinObj.grainSeed, version);
  const canvas = document.createElement ('canvas');
  canvas.width = d; canvas.height = d;
  const ctx = canvas.getContext ('2d');
  gen (ctx, skinObj, d, rng);
  cache.set (key, canvas);
  return canvas;
}

function knobFaceTex (skinObj, d) {
  if (d < 8) return null;
  if (skinObj.finish === 'plastic') return null;
  return getKnobTexture (skinObj, 'knobFace', d, (ctx, th, dd, rng) => genKnobFace (ctx, th, dd, rng));
}

function knurlTex (skinObj, d) {
  if (d < 12 || (skinObj.knurl || 0) <= 0) return null;
  return getKnobTexture (skinObj, 'knurl', d, (ctx, th, dd, rng) => genKnurl (ctx, th, dd, rng));
}

function screwTex (skinObj, px) {
  if (px < 4) return null;
  const b = bucket (px, 2);
  const key = 'screw|' + b + '|' + skinIndex (skinObj) + '|' + version;
  if (cache.has (key)) return cache.get (key);
  if (cache.size > 96) cache.clear();
  const rng = seeded (skinIndex (skinObj), 4, b, 0, skinObj.grainSeed, version);
  const canvas = document.createElement ('canvas');
  canvas.width = b; canvas.height = b;
  const ctx = canvas.getContext ('2d');
  genScrew (ctx, skinObj, b, rng);
  cache.set (key, canvas);
  return canvas;
}

var textures = { face, panel, knobFace: knobFaceTex, knurl: knurlTex, screw: screwTex, invalidate, withAlpha: withAlphaTex };
