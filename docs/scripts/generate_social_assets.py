#!/usr/bin/env python3
"""Generate deterministic social preview and icon fallbacks for the Pages site."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
OUT_PREVIEW = PUBLIC / "social-preview.png"
OUT_FAVICON_PNG = PUBLIC / "favicon.png"
OUT_FAVICON_32 = PUBLIC / "favicon-32x32.png"
OUT_APPLE = PUBLIC / "apple-touch-icon.png"
OUT_ICO = PUBLIC / "favicon.ico"

FONT_MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_MONO_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

BG_DEEP = (31, 37, 35)
BG_METAL = (45, 53, 50)
BG_PANEL = (57, 67, 63)
TERM = (178, 184, 179)
TERM_SOFT = (230, 235, 231)
AMBER = (238, 176, 55)
RED = (231, 56, 86)
VIOLET = (170, 131, 228)
GREEN = (113, 222, 168)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_MONO_BOLD if bold else FONT_MONO, size)


def rounded_rectangle(draw: ImageDraw.ImageDraw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def add_glow(base: Image.Image, mask: Image.Image, color, blur: int, alpha: int):
    glow = Image.new("RGBA", base.size, color + (0,))
    glow.putalpha(mask.filter(ImageFilter.GaussianBlur(blur)).point(lambda p: min(255, int(p * alpha / 255))))
    base.alpha_composite(glow)


def make_mark(size: int) -> Image.Image:
    scale = size / 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=int(14 * scale), fill=(15, 16, 17, 255), outline=TERM + (90,), width=max(1, int(1.2 * scale)))
    def pts(seq):
        return [(int(x * scale), int(y * scale)) for x, y in seq]
    # Same double-forward-slash motif as title-mark.svg, drawn as filled polygons.
    d.polygon(pts([(12, 42), (25, 16), (32, 16), (19, 42)]), fill=(94, 98, 96, 255))
    d.polygon(pts([(32, 42), (45, 16), (52, 16), (39, 42)]), fill=(94, 98, 96, 255))
    d.ellipse((int(43 * scale), int(39 * scale), int(53 * scale), int(49 * scale)), fill=TERM_SOFT + (255,))
    return img


def make_icons() -> None:
    mark = make_mark(256)
    mark.save(OUT_FAVICON_PNG)
    mark.resize((32, 32), Image.Resampling.LANCZOS).save(OUT_FAVICON_32)
    make_mark(180).save(OUT_APPLE)
    sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    mark.save(OUT_ICO, sizes=sizes)


def line_noise(draw: ImageDraw.ImageDraw, width: int, height: int) -> None:
    for y in range(0, height, 4):
        draw.line((0, y, width, y), fill=(8, 12, 11, 44), width=1)
    for x in range(0, width, 32):
        alpha = 16 if x % 96 == 0 else 8
        draw.line((x, 0, x, height), fill=TERM + (alpha,), width=1)


def text(draw: ImageDraw.ImageDraw, xy, s, fnt, fill, shadow=None, anchor=None):
    x, y = xy
    if shadow:
        sx, sy, scol = shadow
        draw.text((x + sx, y + sy), s, font=fnt, fill=scol, anchor=anchor)
    draw.text((x, y), s, font=fnt, fill=fill, anchor=anchor)


def make_preview() -> None:
    w, h = 1200, 630
    img = Image.new("RGBA", (w, h), BG_DEEP + (255,))

    # Dark CRT gradient.
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = y / h
            v = int(10 * (1 - t))
            dx = (x - 960) / 560
            dy = (y - 85) / 320
            violet = max(0, 1 - math.sqrt(dx * dx + dy * dy))
            gx = (x - 100) / 760
            gy = (y - 650) / 560
            green = max(0, 1 - math.sqrt(gx * gx + gy * gy))
            r = BG_METAL[0] + v + int(VIOLET[0] * violet * 0.12) + int(GREEN[0] * green * 0.08)
            g = BG_METAL[1] + v + int(VIOLET[1] * violet * 0.08) + int(GREEN[1] * green * 0.10)
            b = BG_METAL[2] + v + int(VIOLET[2] * violet * 0.14) + int(GREEN[2] * green * 0.08)
            px[x, y] = (min(255, r), min(255, g), min(255, b), 255)

    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    line_noise(d, w, h)

    # Faint geometry/routing grid.
    for y in range(74, h, 56):
        d.line((0, y, w, y), fill=TERM + (20,), width=1)
    for x in range(70, w, 70):
        d.line((x, 0, x, h), fill=TERM + (14,), width=1)
    for i in range(9):
        y = 128 + i * 42
        d.line((730, y, 1118, y + 150), fill=GREEN + (26,), width=1)
        d.ellipse((710 + i * 38, y - 4, 718 + i * 38, y + 4), fill=TERM + (80,))

    # Main card.
    card = (58, 56, 1142, 574)
    rounded_rectangle(d, card, 32, fill=BG_PANEL + (220,), outline=TERM + (96,), width=2)
    rounded_rectangle(d, (82, 84, 1118, 146), 18, fill=(28, 34, 32, 210), outline=TERM + (70,), width=1)
    d.text((110, 104), "rinha4 benchmark console", font=font(24), fill=TERM + (230,))
    d.rectangle((1028, 102, 1090, 126), outline=RED + (190,), width=2)
    d.text((1040, 101), "LIVE", font=font(18, True), fill=RED + (230,))

    mark = make_mark(112)
    glow_mask = Image.new("L", img.size, 0)
    glow_mask.paste(mark.split()[-1], (94, 176))
    add_glow(overlay, glow_mask, TERM, 22, 110)
    overlay.alpha_composite(mark, (94, 176))

    # Big title with site glitch accents.
    title_f = font(64, True)
    text(d, (238, 180), "rinha4-back-end-c", title_f, TERM_SOFT + (255,), shadow=(3, 3, RED + (180,)))
    text(d, (240, 254), "FRAUD SIGNAL UNDER LOAD", font(43, True), TERM + (255,), shadow=(-2, -2, AMBER + (150,)))

    # Proof strip.
    badges = [
        ("PURE C", TERM),
        ("FD-PASS API", GREEN),
        ("0 FP / 0 FN", AMBER),
        ("CI-SOURCED METRICS", RED),
    ]
    bx = 238
    for label, col in badges:
        tw = d.textlength(label, font=font(22, True))
        rounded_rectangle(d, (bx, 322, bx + int(tw) + 34, 368), 12, fill=(26, 31, 30, 230), outline=col + (180,), width=2)
        d.text((bx + 17, 331), label, font=font(22, True), fill=col + (255,))
        bx += int(tw) + 52

    # Terminal readout.
    panel = (92, 410, 1092, 536)
    rounded_rectangle(d, panel, 16, fill=(23, 28, 26, 230), outline=TERM + (85,), width=1)
    readout = [
        ("$ jq .result docs/public/reports/latest-candidate.json", AMBER),
        ("score/p99/http/fp/fn come from the archived CI JSON", TERM_SOFT),
        ("gh-pages: CI reports, docs, and reproducible evidence", GREEN),
    ]
    yy = 430
    for line, col in readout:
        d.text((118, yy), line, font=font(22), fill=col + (238,))
        yy += 34
    d.rectangle((1052, 499, 1069, 525), fill=TERM + (220,))

    # Right-side C sigil.
    d.text((1035, 210), "C", font=font(150, True), fill=TERM + (50,), anchor="mm")
    d.arc((894, 116, 1168, 390), start=205, end=28, fill=RED + (160,), width=5)
    d.arc((918, 140, 1144, 366), start=210, end=24, fill=AMBER + (130,), width=2)

    img.alpha_composite(overlay)
    img.convert("RGB").save(OUT_PREVIEW, optimize=True)


if __name__ == "__main__":
    PUBLIC.mkdir(parents=True, exist_ok=True)
    make_icons()
    make_preview()
    for path in [OUT_PREVIEW, OUT_FAVICON_PNG, OUT_FAVICON_32, OUT_APPLE, OUT_ICO]:
        print(path.relative_to(ROOT), path.stat().st_size)
