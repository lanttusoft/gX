#!/usr/bin/env python3
#opencode: GUI visual-review lane — paired template/baseline contact sheet,
#   persistent human grades, and image hashes for the GUI scoreboard.

import argparse
import hashlib
import json
import os
import re
from datetime import datetime, timezone

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENUM = os.path.join(ROOT, "src", "gui", "enum.h")
DEFAULT_MATRIX = "/tmp/opencode/gui_audit"
DEFAULT_OUT = "/tmp/opencode/gui-visual-review"
UNREVIEWED = "UNREVIEWED"
VISUAL_GRADES = ("F", "D", "C", "B-", "B", "B+", "A-", "A", "A+", "S")
VALID_GRADES = set(VISUAL_GRADES) | {UNREVIEWED}
CARD_WIDTH = 440
CARD_HEIGHT = 300
CARD_GAP = 12
SHEET_COLUMNS = 3


def template_names():
    with open(ENUM, encoding="utf-8") as handle:
        source = handle.read()
    match = re.search(r"#define gX_GUI_TEMPLATE \\\n(.*?)\n\n", source, re.S)
    if not match:
        raise RuntimeError("could not find gX_GUI_TEMPLATE registry")
    return re.findall(r"X\(([A-Z0-9_]+)\)", match.group(1))


def digest(path):
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def image_info(path, label):
    with Image.open(path) as image:
        width, height = image.size
    return {"label": label, "path": os.path.abspath(path),
            "sha256": digest(path), "width": width, "height": height}


def collect_cards(matrix_dir):
    baseline = os.path.join(matrix_dir, "baseline.png")
    if not os.path.isfile(baseline):
        raise RuntimeError(f"GUI baseline capture missing: {baseline}")
    cards = []
    for name in template_names():
        image = os.path.join(matrix_dir, name.lower() + ".png")
        if not os.path.isfile(image):
            continue
        cards.append({
            "id": "template:" + name,
            "group": "template",
            "label": name,
            "grade": UNREVIEWED,
            "notes": "",
            "images": [image_info(baseline, "title baseline"),
                       image_info(image, "template")],
        })
    if not cards:
        raise RuntimeError(f"no template captures found under {matrix_dir}")
    return cards


def load_review(path):
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return {item["id"]: item for item in data.get("cards", [])
            if isinstance(item, dict) and item.get("id")}


def apply_previous(cards, previous):
    for item in cards:
        old = previous.get(item["id"], {})
        grade = old.get("grade", UNREVIEWED)
        item["grade"] = grade if grade in VALID_GRADES else UNREVIEWED
        item["notes"] = str(old.get("notes", ""))


def parse_updates(values, field):
    updates = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"{field} must use CARD_ID=VALUE: {value}")
        key, update = value.split("=", 1)
        if not key or not update:
            raise ValueError(f"{field} must use CARD_ID=VALUE: {value}")
        updates[key] = update
    return updates


def apply_updates(cards, grades, notes):
    known = {item["id"] for item in cards}
    unknown = (set(grades) | set(notes)) - known
    if unknown:
        raise ValueError("unknown card id(s): " + ", ".join(sorted(unknown)))
    for item in cards:
        if item["id"] in grades:
            grade = grades[item["id"]]
            if grade not in VALID_GRADES or grade == UNREVIEWED:
                raise ValueError("invalid visual grade: " + grade)
            item["grade"] = grade
        if item["id"] in notes:
            item["notes"] = notes[item["id"]]


def font():
    try:
        return ImageFont.truetype("DejaVuSans.ttf", 16)
    except OSError:
        return ImageFont.load_default()


def render_sheet(cards, path):
    columns = min(SHEET_COLUMNS, len(cards))
    rows = (len(cards) + columns - 1) // columns
    sheet = Image.new(
        "RGB",
        (columns * (CARD_WIDTH + CARD_GAP) + CARD_GAP,
         rows * (CARD_HEIGHT + CARD_GAP) + CARD_GAP),
        (18, 20, 24),
    )
    draw = ImageDraw.Draw(sheet)
    text_font = font()
    for index, item in enumerate(cards):
        col = index % columns
        row = index // columns
        x = CARD_GAP + col * (CARD_WIDTH + CARD_GAP)
        y = CARD_GAP + row * (CARD_HEIGHT + CARD_GAP)
        draw.rectangle((x, y, x + CARD_WIDTH, y + CARD_HEIGHT),
                       fill=(34, 37, 44), outline=(82, 88, 102))
        draw.text((x + 10, y + 8), item["label"], fill=(240, 242, 248),
                  font=text_font)
        grade = item["grade"]
        grade_color = (255, 207, 74) if grade == UNREVIEWED else (111, 224, 145)
        draw.text((x + CARD_WIDTH - 112, y + 8), grade, fill=grade_color,
                  font=text_font)
        images = item["images"]
        slot_width = (CARD_WIDTH - 30) // max(1, len(images))
        for image_index, info in enumerate(images):
            with Image.open(info["path"]) as source:
                preview = source.convert("RGB")
                preview.thumbnail((slot_width - 8, CARD_HEIGHT - 72),
                                  Image.Resampling.LANCZOS)
            slot_x = x + 10 + image_index * slot_width
            slot_y = y + 38
            draw.rectangle((slot_x, slot_y, slot_x + slot_width - 8,
                            y + CARD_HEIGHT - 20), fill=(8, 9, 12))
            image_x = slot_x + (slot_width - 8 - preview.width) // 2
            image_y = slot_y + ((CARD_HEIGHT - 58 - preview.height) // 2)
            sheet.paste(preview, (image_x, image_y))
            draw.text((slot_x + 4, y + CARD_HEIGHT - 18), info["label"],
                      fill=(190, 194, 204), font=text_font)
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(
        description="Build advisory GUI template review contact sheets.")
    parser.add_argument("--matrix-dir", default=DEFAULT_MATRIX,
                        help="output directory from gui_audit.py")
    parser.add_argument("--out", default=DEFAULT_OUT)
    parser.add_argument("--grade", action="append", default=[],
                        metavar="CARD_ID=GRADE")
    parser.add_argument("--note", action="append", default=[],
                        metavar="CARD_ID=TEXT")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    manifest_path = os.path.join(args.out, "visual_review.json")
    cards = collect_cards(args.matrix_dir)
    apply_previous(cards, load_review(manifest_path))
    apply_updates(cards, parse_updates(args.grade, "--grade"),
                  parse_updates(args.note, "--note"))

    sheet_path = os.path.join(args.out, "gui_contact_sheet.png")
    render_sheet(cards, sheet_path)
    reviewed = sum(item["grade"] != UNREVIEWED for item in cards)
    manifest = {
        "schema": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "advisory": True,
        "visual_grades": list(VISUAL_GRADES),
        "sources": {"matrix": os.path.abspath(args.matrix_dir)},
        "rubric": {
            "A+": "polished layout, typography, color, and no visible artifacts",
            "A": "clear and cohesive with only minor non-blocking roughness",
            "A-": "readable and correct with noticeable contained roughness",
            "B": "functional UI with visible presentation compromises",
            "C": "difficult to read, inconsistent, or visually unstable",
        },
        "cards": cards,
        "reviewed_cards": reviewed,
        "card_count": len(cards),
    }
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print(f"GUI VISUAL REVIEW: {reviewed}/{len(cards)} cards graded")
    print(f"GUI VISUAL REVIEW: {manifest_path}")
    print(f"GUI VISUAL REVIEW: {sheet_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"GUI VISUAL REVIEW: FAIL — {exc}")
        raise SystemExit(1)
