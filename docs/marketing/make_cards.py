#!/usr/bin/env python3
"""Render social-media cards for Spiral303 from the offline GUI test paints.

    uv run --python 3.12 --with pillow docs/marketing/make_cards.py

Reads docs/screenshots/ui-console.png (written by Spiral303Tests) and emits
product-card{,-ru}.png (16:9, 2560×1440), stories{,-ru}.png (9:16, 1080×1920),
instagram-4x5{,-ru}.png (4:5, 1080×1350) and square{,-ru}.png (1:1, 1080×1080)
next to this file. Fonts resolve per-OS — Helvetica Neue faces on macOS, Arial
on Windows, DejaVu on Linux — so the same script runs on a plain checkout.
"""

import math
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SHOT = ROOT / "docs" / "screenshots" / "ui-console.png"

BG_TOP = (7, 11, 18)
BG_BOTTOM = (13, 21, 33)
GRID = (26, 38, 55)
INK = (232, 238, 245)
MUTED = (147, 163, 181)
CYAN = (78, 201, 240)
AMBER = (240, 180, 41)

# The face indices are Helvetica Neue's on macOS; everywhere else they collapse
# onto the local regular/bold pair so the layout still renders without them.
NEUE = "/System/Library/Fonts/HelveticaNeue.ttc"
REGULAR, BOLD, COND_BLACK, ULTRA, MEDIUM = 0, 1, 9, 5, 10


def _faces():
    if sys.platform == "darwin":
        return {i: (NEUE, i) for i in (REGULAR, MEDIUM, BOLD, ULTRA, COND_BLACK)}
    if os.name == "nt":
        fdir = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
        reg, bold = str(fdir / "arial.ttf"), str(fdir / "arialbd.ttf")
    else:
        d = Path("/usr/share/fonts")
        reg = next((str(p) for p in d.rglob("DejaVuSans.ttf")), "")
        bold = next((str(p) for p in d.rglob("DejaVuSans-Bold.ttf")), reg)
    return {REGULAR: (reg, 0), MEDIUM: (bold, 0), BOLD: (bold, 0),
            ULTRA: (bold, 0), COND_BLACK: (bold, 0)}


_FACES = _faces()
_CACHE = {}


def font(index=REGULAR, size=32):
    key = (index, size)
    if key not in _CACHE:
        path, face = _FACES[index]
        _CACHE[key] = (ImageFont.truetype(path, size, index=face)
                       if Path(path).exists() else ImageFont.load_default(size))
    return _CACHE[key]


def gradient(size, top=BG_TOP, bottom=BG_BOTTOM):
    img = Image.new("RGB", size, top)
    d = ImageDraw.Draw(img)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        d.line([(0, y), (size[0], y)],
               fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return img.convert("RGBA")


def grid(img, step=88):
    d = ImageDraw.Draw(img)
    for x in range(0, img.width, step):
        d.line([(x, 0), (x, img.height)], fill=GRID, width=1)
    for y in range(0, img.height, step):
        d.line([(0, y), (img.width, y)], fill=GRID, width=1)
    return img


def mark(img, cx, cy, r, colour=CYAN, second=AMBER):
    """The Spiral303 spiral: an expanding arc run, capped by a dot."""
    d = ImageDraw.Draw(img)
    pts = []
    for i in range(220):
        t = i / 219
        ang = t * math.pi * 3.4
        rad = r * (0.16 + 0.84 * t)
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    for a, b, col, w in ((0, 150, colour, max(2, r // 9)),
                         (150, 219, second, max(2, r // 9))):
        d.line(pts[a:b], fill=col, width=w, joint="curve")
    d.ellipse([pts[-1][0] - r * .18, pts[-1][1] - r * .18,
               pts[-1][0] + r * .18, pts[-1][1] + r * .18], fill=second)


def panel(img, box, radius=28, glow=CYAN, border=(52, 74, 102)):
    """Glowing rounded card: blurred halo behind a stroked dark plate."""
    halo = Image.new("RGBA", img.size, (0, 0, 0, 0))
    hd = ImageDraw.Draw(halo)
    pad = 6
    hd.rounded_rectangle([box[0] - pad, box[1] - pad, box[2] + pad, box[3] + pad],
                         radius=radius + pad, fill=glow + (90,))
    img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(26)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(box, radius=radius, fill=(10, 16, 25, 235),
                        outline=border, width=3)


def text(d, xy, s, f, fill=INK, tracking=0):
    """Draw with letter spacing; returns the advance width."""
    x, y = xy
    for ch in s:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + tracking
    return x - xy[0]


def width(s, f, tracking=0):
    m = Image.new("RGBA", (8, 8))
    d = ImageDraw.Draw(m)
    return sum(d.textlength(c, font=f) + tracking for c in s) - tracking


def bullets(img, items, x, y, w, f_head, f_sub, gap, dot_r=9):
    d = ImageDraw.Draw(img)
    for head, sub in items:
        d.ellipse([x, y + f_head.size * 0.30 - dot_r, x + dot_r * 2,
                   y + f_head.size * 0.30 + dot_r], fill=CYAN)
        text(d, (x + dot_r * 2 + 22, y), head, f_head, INK)
        yy = y + f_head.size + 10
        line = ""
        for word in sub.split():
            if width(line + " " + word, f_sub) > w - dot_r * 2 - 22:
                text(d, (x + dot_r * 2 + 22, yy), line, f_sub, MUTED)
                yy += int(f_sub.size * 1.32)
                line = word
            else:
                line = (line + " " + word).strip()
        if line:
            text(d, (x + dot_r * 2 + 22, yy), line, f_sub, MUTED)
            yy += f_sub.size
        y = yy + gap
    return y


def chips(img, labels, x, y, f, colour=CYAN, h=76, pad=30, gap=18):
    d = ImageDraw.Draw(img)
    for s in labels:
        w = width(s, f) + pad * 2
        d.rounded_rectangle([x, y, x + w, y + h], radius=14, outline=colour, width=3)
        text(d, (x + pad, y + (h - f.size) // 2 - 4), s, f, colour)
        x += w + gap
    return x


def chip_row(img, labels, left, y, right, size, colour=CYAN, h=56):
    """Outlined chips, wrapping to a new line before passing `right`."""
    f = font(index=BOLD, size=size)
    d = ImageDraw.Draw(img)
    x = left
    for s in labels:
        w = width(s, f) + 36
        if x + w > right:
            x, y = left, y + h + 18
        d.rounded_rectangle([x, y, x + w, y + h], radius=12, outline=colour, width=3)
        text(d, (x + 18, y + (h - f.size) // 2 - 2), s, f, colour)
        x += w + 14
    return y + h


def bullets_cols(img, items, x, y, col_w, gutter, f_head, f_sub, gap, cols=2, dot_r=7):
    """Bullets split into `cols` side-by-side columns; returns the lowest bottom."""
    per = -(-len(items) // cols)
    bottom = y
    for col in range(cols):
        bottom = max(bottom,
                     bullets(img, items[col * per:(col + 1) * per],
                             x + col * (col_w + gutter), y, col_w,
                             f_head, f_sub, gap, dot_r))
    return bottom


def right_text(d, xy, s, f, fill=MUTED, tracking=0):
    """Draw right-aligned to xy[0]; returns the left edge."""
    w = width(s, f, tracking)
    text(d, (xy[0] - w, xy[1]), s, f, fill, tracking)
    return xy[0] - w


def shot(img, box):
    """Paste the real UI paint, keeping its aspect inside box."""
    im = Image.open(SHOT).convert("RGBA")
    bw, bh = box[2] - box[0], box[3] - box[1]
    scale = min(bw / im.width, bh / im.height)
    size = (round(im.width * scale), round(im.height * scale))
    ox, oy = box[0] + (bw - size[0]) // 2, box[1] + (bh - size[1]) // 2
    img.alpha_composite(im.resize(size, Image.LANCZOS), (ox, oy))
    return (ox, oy, ox + size[0], oy + size[1])


COPY = {
    "en": {
        "tagline": "ACID SYNTHESIZER & 303 SEQUENCER",
        "bullets": [
            ("7 filter models", "classic 303 contour, diode and Moog ladders, "
                                "TPT SVF, Sallen-Key, one-pole, Vowel"),
            ("A filter that talks", "formant bank walks the voice through "
                                    "oo › oh › ah › ay › ee as you turn Cutoff"),
            ("Spiral sequencer", "8/12/16/24/32 steps, 12 scales, tie / slide / "
                                 "accent, 16-sector gate wheel"),
            ("MIDI import", "drop a .mid — lowest line wins, your scale stays, "
                            "tempo comes from the host clock"),
            ("4 UI skins", "flat dark, starship console, walnut, brushed metal — "
                           "the audio stays bit-identical"),
            ("27 factory presets", "acid classics plus 10 psytrance lines, user "
                                   "presets as plain JSON, drag & drop"),
        ],
        "chips": ["VST3", "AU", "STANDALONE", "macOS universal", "Windows", "Linux"],
        "foot": "~0.2% of realtime CPU  ·  896 offline checks",
        "author": "by Spiralfractal",
    },
    "ru": {
        "tagline": "КИСЛОТНЫЙ СИНТЕЗАТОР И 303-СЕКВЕНСОР",
        "bullets": [
            ("7 моделей фильтра", "классический контур 303, диодная и Moog-"
                                  "лестница, TPT SVF, Sallen-Key, однополюсник, Vowel"),
            ("Фильтр, который говорит", "банк формант ведёт голос по траектории "
                                        "у › о › а › э › и, ручка Cutoff = положение во рту"),
            ("Секвенсор-спираль", "8/12/16/24/32 шага, 12 гамм, tie / slide / accent "
                                  "на ступени, gate-колесо на 16 секторов"),
            ("Импорт MIDI", "бросьте .mid в окно: берётся самая низкая дорожка, строй "
                            "остаётся ваш, темп — из часов хоста"),
            ("4 скина интерфейса", "тёмный, пульт космического корабля, орех, "
                                   "шлифованный металл — звук бит-в-бит"),
            ("27 фабричных пресетов", "известные acid-линии и 10 psytrance-паттернов, "
                                      "свои пресеты — обычный JSON"),
        ],
        "chips": ["VST3", "AU", "STANDALONE", "macOS universal", "Windows", "Linux"],
        "foot": "~0,2% реального времени по CPU  ·  896 офлайн-проверок",
        "author": "автор Spiralfractal",
    },
}

VERSION = "v0.20.0"

# The 4:5 Instagram card carries four highlights instead of six — the caption
# does the rest of the talking.
SHORT = {
    "en": [
        ("A filter that talks", "seven models — the classic 303 contour, diode and "
                                "Moog ladders, TPT SVF, Sallen-Key, one-pole — plus a "
                                "formant bank that walks the voice oo › oh › ah › ay › ee "
                                "as you turn Cutoff"),
        ("Spiral sequencer", "8/12/16/24/32 steps, 12 scales, tie / slide / accent per "
                             "step, 16-sector gate wheel"),
        ("MIDI import", "drop a .mid into the window — the lowest line wins, your scale "
                        "stays, tempo comes from the host clock"),
        ("4 UI skins", "flat dark, starship console, walnut, brushed metal — the audio "
                       "stays bit-identical across all four"),
    ],
    "ru": [
        ("Фильтр, который говорит", "семь моделей: классический контур 303, диодная и "
                                    "Moog-лестница, TPT SVF, Sallen-Key, однополюсник — и "
                                    "банк формант, ведущий голос у › о › а › э › и от ручки "
                                    "Cutoff"),
        ("Секвенсор-спираль", "8/12/16/24/32 шага, 12 гамм, tie / slide / accent на "
                              "ступени, gate-колесо на 16 секторов"),
        ("Импорт MIDI", "бросьте .mid в окно: берётся самая низкая дорожка, строй "
                        "остаётся ваш, темп — из часов хоста"),
        ("4 скина интерфейса", "тёмный, пульт космического корабля, орех, шлифованный "
                               "металл — звук бит-в-бит при любом из них"),
    ],
}

# The square card is a glance: six heads, one short line each.
SQUARE = {
    "en": [
        ("7 filter models", "303 contour, ladders, SVF, Sallen-Key, Vowel"),
        ("A filter that talks", "formants walk oo › oh › ah › ay › ee"),
        ("Spiral sequencer", "up to 32 steps, 12 scales, gate wheel"),
        ("Tie · slide · accent", "the 303 articulation, per step"),
        ("MIDI import", "drop a .mid, keep your scale"),
        ("4 UI skins", "the audio stays bit-identical"),
    ],
    "ru": [
        ("7 моделей фильтра", "контур 303, лестницы, SVF, Sallen-Key, Vowel"),
        ("Фильтр, который говорит", "форманты идут у › о › а › э › и"),
        ("Секвенсор-спираль", "до 32 шагов, 12 гамм, gate-колесо"),
        ("Tie · slide · accent", "артикуляция 303 на каждом шаге"),
        ("Импорт MIDI", "бросьте .mid — строй останется ваш"),
        ("4 скина интерфейса", "звук бит-в-бит при любом скине"),
    ],
}


def post(lang):
    c = COPY[lang]
    img = grid(gradient((2560, 1440)))
    d = ImageDraw.Draw(img)

    mark(img, 84, 168, 34)
    text(d, (132, 148), "S P I R A L 3 0 3", font(index=COND_BLACK, size=34),
         MUTED, tracking=2)
    panel(img, (120, 236, 1500, 1236))
    shot(img, (152, 268, 1468, 1204))

    chips(img, c["chips"], 124, 1280, font(index=BOLD, size=27),
          h=66, pad=26, gap=16)
    text(d, (126, 1368), c["foot"], font(size=28), MUTED)

    x = 1620
    text(d, (x, 176), "Spiral303", font(index=BOLD, size=112), INK)
    text(d, (x + 6, 322), c["tagline"], font(index=BOLD, size=28), CYAN, tracking=2)
    d.line([(x + 4, 378), (2452, 378)], fill=(45, 66, 92), width=3)

    y = bullets(img, c["bullets"], x + 6, 418, 830,
                font(index=BOLD, size=33), font(size=26), gap=22)
    y = min(y + 34, 1216)
    text(d, (x + 6, y), c["author"], font(index=BOLD, size=42), CYAN, tracking=1)
    text(d, (x + 8, y + 76), VERSION, font(index=BOLD, size=28), MUTED)
    return img


def stories(lang):
    c = COPY[lang]
    img = grid(gradient((1080, 1920)), step=72)
    d = ImageDraw.Draw(img)

    mark(img, 116, 146, 44)
    text(d, (184, 98), "Spiral303", font(index=BOLD, size=84), INK)
    text(d, (188, 202), c["tagline"], font(index=BOLD, size=23), CYAN, tracking=2)
    d.line([(80, 254), (1000, 254)], fill=(45, 66, 92), width=3)

    panel(img, (56, 284, 1024, 852))
    shot(img, (76, 304, 1004, 832))

    y = bullets(img, c["bullets"], 84, 902, 878,
                font(index=BOLD, size=30), font(size=24), gap=18)

    y = chip_row(img, c["chips"], 84, y, 1004, 23) + 28

    d = ImageDraw.Draw(img)
    d.rounded_rectangle([56, y, 1024, y + 168], radius=24, fill=(9, 17, 25, 235),
                        outline=CYAN, width=4)
    text(d, (92, y + 26), c["author"], font(index=BOLD, size=38), CYAN, tracking=1)
    text(d, (94, y + 98), f"{VERSION}  ·  {c['foot']}", font(size=23), MUTED)
    return img


def portrait(lang):
    """4:5 feed card — four highlights, the caption carries the rest."""
    c = COPY[lang]
    img = grid(gradient((1080, 1350)), step=72)
    d = ImageDraw.Draw(img)

    mark(img, 112, 128, 40)
    text(d, (176, 84), "Spiral303", font(index=BOLD, size=70), INK)
    text(d, (180, 182), c["tagline"], font(index=BOLD, size=21), CYAN, tracking=2)
    d.line([(72, 230), (1008, 230)], fill=(45, 66, 92), width=3)

    panel(img, (56, 258, 1024, 642), radius=26)
    shot(img, (76, 278, 1004, 622))

    y = bullets(img, SHORT[lang], 84, 682, 878,
                font(index=BOLD, size=27), font(size=21), gap=13)

    y = chip_row(img, c["chips"], 84, y + 26, 1004, 19, colour=AMBER, h=48) + 22
    d.rounded_rectangle([56, y, 1024, y + 112], radius=22, fill=(9, 17, 25, 235),
                        outline=CYAN, width=4)
    text(d, (92, y + 16), c["author"], font(index=BOLD, size=32), CYAN, tracking=1)
    text(d, (94, y + 66), f"{VERSION}  ·  {c['foot']}", font(size=20), MUTED)
    return img


def square(lang):
    """1:1 feed card — six one-line highlights in two columns."""
    c = COPY[lang]
    img = grid(gradient((1080, 1080)), step=72)
    d = ImageDraw.Draw(img)

    mark(img, 92, 98, 30)
    text(d, (140, 60), "Spiral303", font(index=BOLD, size=58), INK)
    text(d, (144, 136), c["tagline"], font(index=BOLD, size=18), CYAN, tracking=2)
    d.line([(64, 172), (1016, 172)], fill=(45, 66, 92), width=3)

    panel(img, (48, 192, 1032, 632), radius=24)
    shot(img, (68, 212, 1012, 612))

    y = bullets_cols(img, SQUARE[lang], 76, 668, 452, 44,
                     font(index=BOLD, size=26), font(size=19), gap=16)
    y = chip_row(img, c["chips"], 76, y + 24, 1016, 19, colour=AMBER, h=50)

    text(d, (76, y + 24), c["author"], font(index=BOLD, size=28), CYAN, tracking=1)
    right_text(d, (1016, y + 32), f"{VERSION}  ·  {c['foot']}", font(size=19))
    return img


def main():
    if not SHOT.exists():
        raise SystemExit(f"missing {SHOT} — run Spiral303Tests first")
    jobs = [("product-card.png", post("en")), ("product-card-ru.png", post("ru")),
            ("stories.png", stories("en")), ("stories-ru.png", stories("ru")),
            ("instagram-4x5.png", portrait("en")),
            ("instagram-4x5-ru.png", portrait("ru")),
            ("square.png", square("en")), ("square-ru.png", square("ru"))]
    for name, img in jobs:
        out = HERE / name
        img.convert("RGB").save(out)
        print(f"wrote {out} {img.size} {round(os.path.getsize(out)/1024)} KB")


if __name__ == "__main__":
    main()
