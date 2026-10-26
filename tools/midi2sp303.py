#!/usr/bin/env python3
"""Turn a standard MIDI file into a Spiral303 `sequence` string.

Mirrors Source/dsp/MidiImport.h: the lowest-pitched track wins, time lives in
quarter-note beats (the file's tempo map is never read), each note snaps to the
nearest degree of the target scale, legato of the same pitch grows a run of ties,
legato of another pitch becomes a slide.

    tools/midi2sp303.py FILE.mid [--scale 4] [--base 36] [--steps 16]
    tools/midi2sp303.py --scan FILE [FILE...]      # score candidates, no output line
"""
import argparse
import pathlib
import sys

# Degree index -> semitones above base, letter-for-letter from Source/dsp/Sequence.cpp.
SCALES = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13],                    # Chromatic
    [0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23],                 # Major
    [0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22],                 # Minor
    [0, 2, 3, 5, 7, 8, 11, 12, 14, 15, 17, 19, 20, 23],                 # Harmonic Minor
    [0, 1, 3, 5, 7, 8, 10, 12, 13, 15, 17, 19, 20, 22],                 # Phrygian
    [0, 1, 4, 5, 7, 8, 11, 12, 13, 16, 17, 19, 20, 23],                 # Phrygian Dominant
    [0, 2, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19, 21, 22],                 # Dorian
    [0, 2, 4, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22],                 # Mixolydian
    [0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31],               # Minor Pentatonic
    [0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26, 28, 31],                # Major Pentatonic
    [0, 2, 3, 6, 7, 9, 10, 12, 14, 15, 18, 19, 21, 22],                 # Ukrainian Dorian
    [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26],                # Whole Tone
]
REST, TIE, NOTE_BASE, SLIDE, ACCENT = 0, 1, 0x80, 0x100, 0x200
DRUM_CHANNEL = 10          # JUCE numbers channels 1..16
ACCENT_VELOCITY = 100
BASE_FIRST, BASE_LAST = 24, 72
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def read_midi(path):
    """(format, division, tracks) where each track holds (midi, onset_beats, len_beats, vel)."""
    data = pathlib.Path(path).read_bytes()
    if data[0:4] != b"MThd":
        raise ValueError("not a standard MIDI file")
    fmt = int.from_bytes(data[8:10], "big")
    ntracks = int.from_bytes(data[10:12], "big")
    division = int.from_bytes(data[12:14], "big")
    if division <= 0 or division > 0x7fff:
        raise ValueError("no beat grid (SMPTE or bogus division %d)" % division)

    def vlq(buf, i):
        value = 0
        while True:
            b = buf[i]
            i += 1
            value = (value << 7) | (b & 0x7F)
            if not b & 0x80:
                return value, i

    pos, tracks = 14, []
    for _ in range(ntracks):
        if data[pos:pos + 4] != b"MTrk":
            break
        length = int.from_bytes(data[pos + 4:pos + 8], "big")
        body = data[pos + 8:pos + 8 + length]
        pos += 8 + length
        notes, running, tick, open_notes = [], 0, 0, {}
        i = 0
        while i < len(body):
            delta, i = vlq(body, i)
            tick += delta
            if i >= len(body):
                break
            byte = body[i]
            if byte < 0x80:                             # running status
                st = running
            else:
                i += 1
                st = running = byte
            if st == 0xFF:                              # meta: type, VLQ length, payload
                i += 1                                  # the type byte is not the length
                size, i = vlq(body, i)
                i += size
                continue
            if st in (0xF0, 0xF7):                      # sysex
                size, i = vlq(body, i)
                i += size
                continue
            if (st & 0xF0) in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1, d2 = body[i], body[i + 1]
                i += 2
            elif (st & 0xF0) in (0xC0, 0xD0):
                d1, d2 = body[i], 0
                i += 1
            else:
                break
            channel, note, velocity = (st & 0x0F) + 1, d1, d2
            if st & 0xF0 == 0x90 and velocity > 0:
                if channel != DRUM_CHANNEL:
                    open_notes[(channel, note)] = (tick, velocity)
            elif (st & 0xF0 == 0x80) or (st & 0xF0 == 0x90 and velocity == 0):
                started = open_notes.pop((channel, note), None)
                if started is None or channel == DRUM_CHANNEL:
                    continue
                onset, on_vel = started
                notes.append((note, onset / division, (tick - onset) / division, on_vel))
        for (channel, note), (onset, on_vel) in open_notes.items():   # never released: hold to end
            if channel == DRUM_CHANNEL:
                continue
            notes.append((note, onset / division, max(0.0, (tick - onset) / division), on_vel or 100))
        tracks.append(notes)
    return fmt, division, tracks


def collect_notes(path, track="lowest"):
    """Notes of one track: `lowest` (the MidiImport rule), `highest`, `loudest`
    (most events), or a 0-based index."""
    _, _, tracks = read_midi(path)
    scored = []
    for index, notes in enumerate(tracks):
        notes = sorted([n for n in notes if n[3] > 0], key=lambda n: (n[1], n[0]))
        if not notes:
            continue
        avg = sum(n[0] for n in notes) / len(notes)
        scored.append((index, avg, notes))
    if not scored:
        return []
    if isinstance(track, int):
        for index, _, notes in scored:
            if index == track:
                return notes
        return []
    if track == "highest":
        return max(scored, key=lambda s: s[1])[2]
    if track == "loudest":
        return max(scored, key=lambda s: (len(s[2]), s[1]))[2]
    best, best_avg = [], 1e9
    for _, avg, notes in scored:
        if avg < best_avg or (avg == best_avg and len(notes) > len(best)):
            best_avg, best = avg, notes
    return best


def fold_octaves(notes, root, window):
    """Bring every note inside `root .. root + window` by moving whole octaves.
    Wide arpeggios become one continuous run instead of collapsing on the top row."""
    folded = []
    for midi, onset, length, vel in notes:
        p = midi
        while p > root + window:
            p -= 12
        while p < root:
            p += 12
        folded.append((p, onset, length, vel))
    return folded


def nearest_degree(offsets, semitones):
    if semitones <= offsets[0]:
        return 0
    if semitones >= offsets[-1]:
        return len(offsets) - 1
    gaps = [abs(o - semitones) for o in offsets]
    best = gaps.index(min(gaps))
    return best


def fit(notes, scale_index=4, base=None, steps=16, step_beats=0.25, from_beat=None,
        fold=False, keep_velocity=False):
    """Lay notes on the grid, returning (base, codes, velocities) as MidiImport::fit does.
    `base=None` anchors the pattern at the lowest note of the phrase, which is what a
    factory preset wants: the whole 14-row window is used instead of the top row."""
    offsets = SCALES[scale_index]
    if not notes:
        return base or 36, [REST] * steps, [127] * steps
    origin = notes[0][1] if from_beat is None else from_beat
    window = [n for n in notes if n[1] >= origin and n[1] < origin + steps * step_beats]
    if not window:
        return base or 36, [REST] * steps, [127] * steps
    lowest = min(n[0] for n in window)
    if fold:
        window = fold_octaves(window, lowest, offsets[-1])
    if base is None:
        base = lowest
    elif lowest < base or lowest > base + offsets[-1]:
        base = lowest
    base = max(BASE_FIRST, min(BASE_LAST, base))

    codes, vels = [REST] * steps, [127] * steps
    held_to, held_deg, attacked_at = -1, -1, -1
    for midi, onset, length, vel in window:
        s0 = round((onset - origin) / step_beats)
        if s0 >= steps:
            break
        if s0 <= attacked_at:
            continue
        s1 = min(steps - 1, s0 + max(1, round(length / step_beats)) - 1)
        degree = nearest_degree(offsets, midi - base)
        legato = held_to >= 0 and s0 <= held_to + 1
        if legato and degree == held_deg:
            for k in range(held_to + 1, s1 + 1):
                codes[k] = TIE
            held_to = max(held_to, s1)
            continue
        codes[s0] = NOTE_BASE + degree
        vels[s0] = max(1, min(127, vel))
        if vel >= ACCENT_VELOCITY:
            codes[s0] |= ACCENT
        if legato:
            codes[s0] |= SLIDE
        for k in range(s0 + 1, s1 + 1):
            codes[k] = TIE
        held_to, held_deg, attacked_at = s1, degree, s0
    return base, codes, (vels if keep_velocity else None)


def encode(base, codes, vels=None, scale_index=4):
    head = "v1" if vels is None or all(v == 127 for v in vels) else "v2"
    s = "%s:%d:%d:%s" % (head, base, scale_index, ",".join(str(c) for c in codes))
    if head == "v2":
        s += ":" + ",".join(str(v) for v in vels)
    return s


def roll(codes, base, scale_index):
    """The fitted grid as text: X note, A accent, S slide, - tie, . rest."""
    offsets = SCALES[scale_index]
    lines = []
    for d in range(len(offsets) - 1, -1, -1):
        row = ""
        for c in codes:
            if not (c & NOTE_BASE) or (c & 0x7F) != d:
                row += "-" if c == TIE else "."
            else:
                row += "A" if c & ACCENT else ("S" if c & SLIDE else "X")
        pitch = base + offsets[d]
        lines.append("%3s%-2d |%s" % (NOTE_NAMES[pitch % 12], pitch // 12 - 1, row))
    return "\n".join(lines)


def all_tracks(path):
    """Every pitched track of the file, each sorted by onset."""
    _, _, tracks = read_midi(path)
    out = []
    for notes in tracks:
        notes = sorted([n for n in notes if n[3] > 0], key=lambda n: (n[1], n[0]))
        if notes:
            out.append(notes)
    return out


def best_phrase(notes, steps, step_beats, granularity=1.0):
    """(start, attacks, window) for the window of `steps * step_beats` beats holding the
    most attacks, snapped to `granularity` beats. A long file is usually one busy riff
    and a lot of padding; this is what finds the riff."""
    if not notes:
        return 0.0, 0, []
    length = steps * step_beats
    starts = sorted({float(int(n[1] / granularity)) * granularity for n in notes})
    best, best_window, best_key = starts[0], [], None
    for s in starts:
        win = [n for n in notes if s <= n[1] < s + length]
        attacks = len({round((n[1] - s) / step_beats) for n in win})
        key = (attacks, len({n[0] for n in win}))
        if best_key is None or key > best_key:
            best, best_window, best_key = s, win, key
    return best, best_key[0], best_window


def source_phrase(path, steps, step_beats, track="lowest", best=True):
    """(notes, from_beat): the track to read and where its densest phrase starts.
    With best=False the MidiImport track rule applies (lowest tessitura)."""
    if not best:
        notes = collect_notes(path, track)
        return notes, best_phrase(notes, steps, step_beats)[0]
    winner, winner_key, winner_bar = [], (0, 0), 0.0
    for notes in all_tracks(path):
        bar, attacks, window = best_phrase(notes, steps, step_beats)
        key = (attacks, len({n[0] for n in window}))
        if key > winner_key:
            winner, winner_key, winner_bar = notes, key, bar
    return winner, winner_bar


def scan(path, track="lowest", steps=16, step_beats=0.25, fold=True, scale=4, best=True):
    """One line describing the file and its densest phrase, for picking sources."""
    notes, bar = source_phrase(path, steps, step_beats, track, best)
    if not notes:
        return "%-58s no pitched notes" % path
    base, codes, _ = fit(notes, scale, None, steps, step_beats, bar, fold)
    degrees = [str(c & 0x7F) for c in codes if c & NOTE_BASE]
    pitches = [n[0] for n in notes]
    return ("%-58s lo=%-3d hi=%-3d bar=%-6.2f attacks=%-3d base=%-3d d[%s]"
            % (path[-58:], min(pitches), max(pitches), bar, len(degrees), base,
               " ".join(degrees[:20])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--base", type=int, default=None,
                    help="MIDI note of degree 0 (default: the lowest note of the phrase)")
    ap.add_argument("--steps", type=int, default=16)
    ap.add_argument("--step-beats", type=float, default=0.25)
    ap.add_argument("--from-beat", type=float, default=None,
                    help="phrase start inside the file, beats (default: first note)")
    ap.add_argument("--track", default="lowest",
                    help="lowest | highest | loudest | <track index>")
    ap.add_argument("--fold", action="store_true", help="fold wide runs into one window")
    ap.add_argument("--midi-rule", action="store_true",
                    help="pick the lowest-pitched track like MidiImport does, "
                         "instead of the densest phrase in the file")
    ap.add_argument("--vel", action="store_true", help="emit v2 with MIDI velocities")
    ap.add_argument("--scan", action="store_true", help="just describe the files")
    ap.add_argument("--roll", action="store_true", help="print the fitted grid")
    a = ap.parse_args()
    track = int(a.track) if a.track.lstrip("-").isdigit() else a.track
    for f in a.files:
        if a.scan:
            try:
                print(scan(f, track, a.steps, a.step_beats, a.fold, a.scale, not a.midi_rule))
            except Exception as e:
                print("%-64s ERROR %s" % (f[-64:], e))
            continue
        if a.midi_rule:
            notes = collect_notes(f, track)
            from_beat = a.from_beat if a.from_beat is not None else \
                best_phrase(notes, a.steps, a.step_beats)[0]
        else:
            notes, from_beat = source_phrase(f, a.steps, a.step_beats, track)
            if a.from_beat is not None:
                from_beat = a.from_beat
        base, codes, vels = fit(notes, a.scale, a.base, a.steps, a.step_beats,
                                from_beat, a.fold, a.vel)
        print(encode(base, codes, vels, a.scale))
        if a.roll:
            print(roll(codes, base, a.scale), file=sys.stderr)
            print("source: " + f, file=sys.stderr)


if __name__ == "__main__":
    main()
