#!/usr/bin/env python3
#ai: render every project font into one labeled PNG preview with explicit
#   missing-glyph markers and JSON metadata. No display, engine or network.
"""Render a deterministic font preview sheet for resources/font.

Each row labels a font and draws a sample line. Characters that match the
font's replacement glyph (the conservative missing-glyph heuristic available
through PIL) are drawn as a red [?] and listed in the JSON metadata. A font
that fails to load gets an explicit error row; the tool exits 2 only when no
font could be rendered.
"""

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "resources" / "font"
DEFAULT_OUT = "/tmp/opencode/frontend_fonts.png"
DEFAULT_SAMPLE = "The quick brown fox 0123 — ÆØÅ æøå → ← ↑ ↓"
BACKGROUND = (22, 22, 28)
TEXT = (235, 235, 242)
DIM = (150, 150, 165)
MISSING = (255, 90, 90)
ROW_HEIGHT = 76
HEADER = 58
WIDTH = 1200


def glyph_missing(font, char):
    if char.isspace():
        return False
    try:
        mask = bytes(font.getmask(char))
        replacement = bytes(font.getmask("\uFFFD"))
        return (mask == replacement
                and font.getbbox(char) == font.getbbox("\uFFFD"))
    except (OSError, ValueError):
        return True


def load_fonts():
    fonts = []
    errors = []
    for path in sorted(FONT_DIR.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".ttf", ".otf"}:
            continue
        try:
            font = ImageFont.truetype(str(path), 28)
        except OSError as error:
            errors.append({"path": str(path.relative_to(ROOT)),
                           "error": str(error)})
            continue
        fonts.append({
            "path": str(path.relative_to(ROOT)),
            "name": path.name,
            "font": font,
            "glyphs": getattr(font.font, "glyphs", None),
        })
    return fonts, errors


def draw_sample(draw, font, text, x, y, default_font):
    missing = []
    for char in text:
        if glyph_missing(font, char):
            missing.append(char)
            label = "[?]"
            draw.text((x, y), label, font=default_font, fill=MISSING)
            x += max(16, int(default_font.getlength(label)) + 2)
        else:
            draw.text((x, y), char, font=font, fill=TEXT)
            x += font.getlength(char)
    return missing


def build_sheet(sample):
    fonts, errors = load_fonts()
    rows = max(1, len(fonts) + len(errors))
    height = HEADER + rows * ROW_HEIGHT + 30
    image = Image.new("RGB", (WIDTH, height), BACKGROUND)
    draw = ImageDraw.Draw(image)
    default = ImageFont.load_default()
    draw.text((16, 16), "gX font preview", font=default, fill=TEXT)
    draw.text((150, 16), f"{len(fonts)} fonts · red [?] = missing glyph",
              font=default, fill=DIM)
    metadata_fonts = []
    row = 0
    for entry in fonts:
        y = HEADER + row * ROW_HEIGHT
        draw.text((16, y + 14), f"{entry['name']} "
                  f"({entry['glyphs']} glyphs)", font=default, fill=DIM)
        missing = draw_sample(draw, entry["font"], sample, 330, y + 12, default)
        metadata_fonts.append({
            "path": entry["path"],
            "name": entry["name"],
            "glyphs": entry["glyphs"],
            "missing_glyphs": "".join(dict.fromkeys(missing)),
        })
        row += 1
    for error in errors:
        y = HEADER + row * ROW_HEIGHT
        draw.text((16, y + 14), error["path"], font=default, fill=DIM)
        draw.text((330, y + 14), f"LOAD ERROR: {error['error']}",
                  font=default, fill=MISSING)
        row += 1
    draw.text((16, height - 22),
              "sample line: The quick brown fox 0123 "
              "(full sample in JSON metadata)", font=default, fill=DIM)
    return image, metadata_fonts, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path(DEFAULT_OUT),
                        help=f"output PNG (default: {DEFAULT_OUT})")
    parser.add_argument("--sample", default=DEFAULT_SAMPLE,
                        help="sample text")
    parser.add_argument("--json", nargs="?", const="", default="",
                        help="JSON metadata path (default: <out>.meta.json)")
    args = parser.parse_args()
    image, fonts, errors = build_sheet(args.sample)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    metadata = {
        "out": str(args.out),
        "width": image.width,
        "height": image.height,
        "sample": args.sample,
        "fonts": fonts,
        "errors": errors,
        "missing_glyph_heuristic":
            "glyph mask and bbox match the font's U+FFFD replacement glyph",
    }
    if args.json is not None:
        path = (Path(args.json) if args.json
                else args.out.with_name(args.out.stem + ".meta.json"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(metadata, indent=2, sort_keys=True),
                        encoding="utf-8")
        metadata["json"] = str(path)
    missing = sum(len(item["missing_glyphs"]) for item in fonts)
    print(f"FONT_PREVIEW: {len(fonts)} fonts, {len(errors)} load errors, "
          f"{missing} missing-glyph markers -> {args.out} "
          f"({image.width}x{image.height})")
    return 0 if fonts else 2


if __name__ == "__main__":
    sys.exit(main())
