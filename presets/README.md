# Spiral303 preset format (`.sp303preset` / v1)

One JSON object per preset. The same format is used for the factory presets in this
folder and for the presets the plugin saves to
`%APPDATA%\Spiral303\Presets\` (macOS: `~/Library/Application Support/Spiral303/Presets/`),
so a file can be copied between the two. A file in either place can also be dropped
onto the plugin window.

Factory presets are compiled into the plugin from this folder — see `CMakeLists.txt`
(`juce_add_binary_data(PresetBank ...)`). Adding a file here and rebuilding is all it
takes; no C++ is involved.

## File names

`<order>-<slug>.json`, e.g. `02-adrenaline.json`. The numeric prefix fixes the program
order the host sees; the rest of the name is only for humans. The display name comes
from the `name` field, not the file name.

## Fields

| Field      | Type               | Required | Meaning |
|------------|--------------------|----------|---------|
| `format`   | integer            | yes      | Format version. Currently `1`. Anything else is rejected. |
| `name`     | string             | yes      | Display name, shown in the preset menu and as the host program name. |
| `params`   | object id → number | yes      | One entry per parameter, in the parameter's own units. |
| `sequence` | string             | no       | The pattern. Absent = keep whatever pattern is loaded. |
| `gate`     | string             | no       | The gate wheel. Absent = keep whatever gate is loaded. |
| `credit`   | string             | no       | Attribution line, shown under the gate wheel. |

Unknown keys are ignored, so a file stays readable by older builds.

### `params`

Keys are the parameter IDs from `Source/PluginParameters.h`, values are in the
parameter's natural units — the same numbers the knobs show:

| ID | Unit / range |
|----|--------------|
| `Cutoff` | Hz, 40–8000 (log) |
| `Wave` | 0–1 (sine → saw → square) |
| `Res` | 0–1 |
| `EnvAmt` | 0–1 |
| `Contour` | 0.05–1.5, as a fraction of the step |
| `Accent` | 0–1 |
| `Glide` | 0–1, as a fraction of the step |
| `Volume` | 0–1 |
| `Drive` | 0–1 |
| `DistMix` | 0–1 |
| `Slope` | 0–1 (12 → 24 dB/oct) |
| `Warmth` | 0–1 analog noise floor |
| `Swing` | 0–0.6 |
| `BPM` | 20–400 (log) |
| `ClockDiv` | choice index: 0 `1/4`, 1 `1/4T`, 2 `1/8`, 3 `1/8T`, 4 `1/16`, 5 `1/16T` |
| `Sync` | choice index: 0 Host, 1 Manual |
| `GateDiv` | choice index: 0 `16th`, 1 `8th`, 2 `32nd`, 3 `16T` |
| `GateOn` | `0` or `1` — the gate layer switch |
| `Play` | `0` or `1` — manual transport |
| `GateAtk` | ms, 0.2–50 (log) |
| `GateRel` | ms, 0.5–200 (log) |
| `GateMix` | 0–1, сухая/мокшая смесь гейта |
| `Gain` | dB, −24…+12 |

A parameter absent from the object keeps whatever value it had, so a preset that only
wants to change the filter can list `Cutoff` and `Res` alone. Values outside the range
are clamped. The factory files leave `Sync` and `Play` out on purpose: they describe a
sound and a line, not whether the plugin should ignore the host transport.

### `sequence`

`v1:<baseMidi>:<scaleIndex>:<step>,<step>,...`

- `baseMidi` — MIDI note of degree 0, the bottom row of the grid.
- `scaleIndex` — index into `Sequence::scaleNames()`: `0` Chromatic, `1` Major,
  `2` Minor, `3` Harmonic Minor, `4` Phrygian, `5` Phrygian Dominant, `6` Dorian,
  `7` Mixolydian, `8` Minor Pentatonic, `9` Major Pentatonic, `10` Ukrainian Dorian,
  `11` Whole Tone.
- Each step is one integer, a bit field (see `Source/dsp/Sequence.h`):

  | Value | Meaning |
  |-------|---------|
  | `0`   | rest |
  | `1`   | tie — hold the previous note through this step |
  | `128 + degree` | note, `degree` 0–13 walking the scale |
  | `+256` | slide (SL) — glide into this note from the previous one |
  | `+512` | accent (AC) — louder, with a harder filter kick |

  The flags add to the note code, so an accented slide on degree 7 is
  `128 + 7 + 256 + 512 = 903`.

Degrees walk the selected scale, so with Chromatic the degree is a semitone offset
above the base note. The editor offers 8, 12, 16, 24 or 32 steps; a file may hold any
count from 4 to 32, and anything outside that is repaired on load.

### `gate`

`g1:<HEX>:<rotation>`

16 sectors, clockwise from the top. `<HEX>` is the mask of the sectors that *mute*
(sector 0 is bit 0), so `g1:0000:0` is a fully open gate and `g1:ffff:0` is fully
closed. `<rotation>` shifts the wheel against the clock, letting the same mask land on
a different beat; it is wrapped into one turn and may be negative to turn the other
way. The third field may be left out — `g1:f0f0` is the same as `g1:f0f0:0`.

## Attribution

Patterns taken from [acid-tabs.com](https://acid-tabs.com) are free for commercial use
under CC BY-SA where the site says so; the `credit` field repeats what each file states.
Keep it when you redistribute a modified version of this folder.
