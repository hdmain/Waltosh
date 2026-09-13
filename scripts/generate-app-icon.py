#!/usr/bin/env python3
"""Build a multi-size Windows .ico from icon.svg — transparent, max-size mark."""

from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "branding"
OUT_SVG = OUT_DIR / "app-icon.svg"
OUT_ICO = OUT_DIR / "waltosh.ico"
OUT_PNG256 = OUT_DIR / "waltosh-256.png"

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)

# Geometry from icon.svg (viewBox 0..400).
POLY_OUTER = [(200, 40), (338.56, 120), (338.56, 280), (200, 360), (61.44, 280), (61.44, 120)]
TRI_UP = [(200, 40), (338.56, 280), (61.44, 280)]
TRI_DN = [(200, 360), (61.44, 120), (338.56, 120)]
LINES = [((200, 40), (200, 360)), ((61.44, 120), (338.56, 280)), ((61.44, 280), (338.56, 120))]
POLY_INNER = [(200, 120), (269.28, 160), (269.28, 240), (200, 280), (130.72, 240), (130.72, 160)]
TRI_IN_UP = [(200, 120), (269.28, 240), (130.72, 240)]
TRI_IN_DN = [(200, 280), (130.72, 160), (269.28, 160)]

# Tight crop of the mark in source coords (hexagon extents).
SRC_MIN_X, SRC_MAX_X = 61.44, 338.56
SRC_MIN_Y, SRC_MAX_Y = 40.0, 360.0


def write_app_svg() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # Transparent, full-bleed mark matching icon.svg geometry.
    OUT_SVG.write_text(
        """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400" width="400" height="400">
  <g fill="none" stroke="#12141a" stroke-linecap="round" stroke-linejoin="round" stroke-width="10">
    <polygon points="200,40 338.56,120 338.56,280 200,360 61.44,280 61.44,120"/>
    <polygon points="200,40 338.56,280 61.44,280"/>
    <polygon points="200,360 61.44,120 338.56,120"/>
    <line x1="200" y1="40" x2="200" y2="360"/>
    <line x1="61.44" y1="120" x2="338.56" y2="280"/>
    <line x1="61.44" y1="280" x2="338.56" y2="120"/>
    <polygon points="200,120 269.28,160 269.28,240 200,280 130.72,240 130.72,160"/>
    <polygon points="200,120 269.28,240 130.72,240"/>
    <polygon points="200,280 130.72,160 269.28,160"/>
  </g>
</svg>
""",
        encoding="utf-8",
    )


def map_pt(x: float, y: float, size: int) -> tuple[float, float]:
    # Map source bbox to nearly full canvas (minimal margin so strokes don't clip).
    margin = max(1.0, size * 0.02)
    box = size - 2 * margin
    sx = SRC_MAX_X - SRC_MIN_X
    sy = SRC_MAX_Y - SRC_MIN_Y
    scale = box / max(sx, sy)
    ox = margin + (box - sx * scale) * 0.5
    oy = margin + (box - sy * scale) * 0.5
    return ox + (x - SRC_MIN_X) * scale, oy + (y - SRC_MIN_Y) * scale


def map_poly(pts: list[tuple[float, float]], size: int) -> list[tuple[float, float]]:
    return [map_pt(x, y, size) for x, y in pts]


def stroke_width(size: int) -> float:
    if size <= 16:
        return 1.55
    if size <= 24:
        return 1.75
    if size <= 32:
        return 2.05
    if size <= 48:
        return 2.4
    return max(2.6, size * 0.03)


def render_icon(size: int) -> Image.Image:
    scale = 4 if size <= 64 else 2
    canvas = size * scale
    img = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Match icon.svg (black stroke on transparent).
    ink = (0, 0, 0, 255)
    w = max(1, int(round(stroke_width(size) * scale)))

    def poly(pts: list[tuple[float, float]]) -> None:
        draw.polygon(map_poly(pts, canvas), outline=ink, width=w)

    def line(a: tuple[float, float], b: tuple[float, float]) -> None:
        draw.line([map_pt(*a, canvas), map_pt(*b, canvas)], fill=ink, width=w, joint="curve")

    poly(POLY_OUTER)
    poly(TRI_UP)
    poly(TRI_DN)
    for a, b in LINES:
        line(a, b)
    poly(POLY_INNER)
    poly(TRI_IN_UP)
    poly(TRI_IN_DN)

    if scale != 1:
        img = img.resize((size, size), Image.Resampling.LANCZOS)
    if size <= 32:
        img = img.filter(ImageFilter.UnsharpMask(radius=0.55, percent=120, threshold=2))
    return img


def write_ico(path: Path, images: list[Image.Image]) -> None:
    png_blobs: list[bytes] = []
    for im in images:
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        png_blobs.append(buf.getvalue())

    count = len(images)
    offset = 6 + 16 * count
    entries = bytearray()
    data = bytearray()
    for im, blob in zip(images, png_blobs):
        w = 0 if im.width >= 256 else im.width
        h = 0 if im.height >= 256 else im.height
        entries += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob

    path.write_bytes(struct.pack("<HHH", 0, 1, count) + entries + data)


def main() -> None:
    write_app_svg()
    images = [render_icon(s) for s in SIZES]
    images[-1].save(OUT_PNG256)
    write_ico(OUT_ICO, images)
    print(f"Wrote {OUT_SVG.relative_to(ROOT)}")
    print(f"Wrote {OUT_ICO.relative_to(ROOT)} ({', '.join(str(s) for s in SIZES)} px)")
    print(f"Wrote {OUT_PNG256.relative_to(ROOT)} ({OUT_ICO.stat().st_size} bytes ico)")


if __name__ == "__main__":
    main()
