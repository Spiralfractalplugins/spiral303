# Spiral303 — Announcement (EN)

## Short (stories / X / reel caption)

**🌀 Spiral303 — an acid 303 line that can talk.**

Seven filter models, including a formant bank: the Cutoff knob walks a voice
through `oo → oh → ah → ay → ee`, so each note actually pronounces a vowel.
Spiral sequencer with a gate wheel, `.mid` import from your DAW, four UI skins.
VST3 + AU + Standalone, macOS universal / Windows / Linux.

Author: **Spiralfractal**

---

## Detailed (post)

**🌀 Spiral303 — a Roland TB-303 emulation that grew vowels.**

Not another "acid" plugin with one cosmetic Filter knob. The sound comes out of a
live signal chain: sine → saw → square morphing oscillator with polyBLEP, a
resonant 12/24 dB/oct contour filter with envelope-on-cutoff, accent, glide and
analogue drift — plus seven filter models, each loudness-calibrated to the classic
one, so switching never deafens you or drops you to silence.

🎛 **What you get**
- **7 filter models**: classic 303 contour, diode and Moog ladders, TPT SVF,
  Sallen-Key, one-pole, and **Vowel**
- **Vowel — a filter that speaks.** Three parallel formant resonators (F1/F2/F3);
  Cutoff is mouth position, Resonance is formant width, and the note envelope moves
  the mouth itself — bright attack, dark tail. The note pronounces
  `oo → oh → ah → ay → ee`
- **303-style sequencer**: 8/12/16/24/32 steps, 12 scales, per-step
  tie / slide / accent; swing, glide, accent envelope
- **Gate wheel** — 16 sectors around a circle: closed sectors mute the note, rotating
  the wheel shifts the pattern by a subdivision. This is where stutter, rolls and
  chops come from
- **MIDI phrase import**: the `MIDI` button, or drop a `.mid` into the window. It
  takes the lowest line (the bass, not the kick), quantises to 1/16, follows the
  host clock — and keeps *your* scale: foreign notes snap to the nearest degree
- **Warmth knob** for analogue drift: pitch and cutoff wander, gate-threshold spread
- Live **scope + response curve**, and the curve is a control surface: drag it
  (X = cutoff, Y = resonance), wheel = cutoff; the knobs follow the graph
- Master limiter, meter, saturation (tanh / drive / triode / fold / five tape models)
- **27 factory presets** — well-known acid lines plus 10 psytrance patterns, your own
  presets as plain JSON, drag&drop
- **4 UI skins**: flat dark, starship console, walnut with bakelite knobs, brushed
  metal. Switch live — the audio stays bit-identical
- Export the current pattern to WAV from inside the plugin
- VST3 + AU + Standalone, macOS universal / Windows / Linux

📐 **Measured, not promised.** All DSP and the sequencer run offline in tests
(896 checks), the UI is verified pixel by pixel, an imported MIDI line renders
**bit-identically** to the same line typed by hand, and the engine sits at ~0.2%
of realtime CPU.

Try: **FilterType = Vowel**, Resonance up, sweep Cutoff from
bottom to top — and listen to a bass pronounce its vowels.

Author: **Spiralfractal**

---

## Artwork

| file | size | for |
|---|---|---|
| `docs/marketing/product-card.png` | 2560×1440 | post (Telegram / VK) |
| `docs/marketing/product-card-ru.png` | 2560×1440 | same, Russian |
| `docs/marketing/stories.png` | 1080×1920 | stories / reel cover |
| `docs/marketing/stories-ru.png` | 1080×1920 | same, Russian |

The screenshot in the card is not a mock-up — it is the real offline UI paint
produced by `Spiral303Tests` (Console skin). Rebuild after a release:

```sh
cmake --build build --config Release --target Spiral303Tests
./build/Spiral303Tests_artefacts/Release/Spiral303Tests
cp "$TMPDIR/Spiral303Tests/spiral303_gui/skin-1-console.png" docs/screenshots/ui-console.png
uv run --python 3.12 --with pillow docs/marketing/make_cards.py
```

Card copy and version live in `COPY` / `VERSION` inside `make_cards.py`.
