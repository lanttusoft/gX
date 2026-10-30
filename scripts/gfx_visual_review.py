#!/usr/bin/env python3
#opencode: advisory GFX visual-review lane. It turns the fixed captures from
#   gfx_feature_matrix.py and reflect_suite.py into reviewable contact sheets,
#   preserves adjacent human grades/notes, and records image hashes. It never
#   changes an automated gate result: UNREVIEWED is a valid initial state.

import argparse
import hashlib
import json
import os
from datetime import datetime, timezone

from PIL import Image, ImageDraw, ImageFont


DEFAULT_MATRIX = "/tmp/opencode/fmatrix"
DEFAULT_REFLECT = "/tmp/reflect_suite"
DEFAULT_OUT = "/tmp/opencode/visual-review"
UNREVIEWED = "UNREVIEWED"
VISUAL_GRADES = ("F", "D", "C", "B-", "B", "B+", "A-", "A", "A+", "S")
VALID_GRADES = set(VISUAL_GRADES) | {UNREVIEWED}
CARD_WIDTH = 440
CARD_HEIGHT = 300
CARD_GAP = 12
SHEET_COLUMNS = 3


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


def card(card_id, group, label, paths):
    return {"id": card_id, "group": group, "label": label,
            "grade": UNREVIEWED, "notes": "",
            "images": [image_info(path, image_label)
                       for image_label, path in paths if os.path.isfile(path)]}


def collect_matrix(root):
    cards = []
    if not os.path.isdir(root):
        return cards
    for name in sorted(os.listdir(root)):
        if not name.endswith("_on.png"):
            continue
        feature = name[:-7]
        on_path = os.path.join(root, name)
        off_path = os.path.join(root, feature + "_off.png")
        paths = [("on", on_path)]
        if os.path.isfile(off_path):
            paths.insert(0, ("off", off_path))
        cards.append(card("matrix:" + feature, "matrix", feature, paths))
    return cards


def collect_reflect(root):
    cards = []
    if not os.path.isdir(root):
        return cards

    fixed = [
        ("ssr_default_30_25", "SSR 30/25", "ssr_default_30_25.png"),
        ("planar_default_30_25", "Planar 30/25", "planar_default_30_25.png"),
        ("ssr_default_45_25", "SSR 45/25", "ssr_default_45_25.png"),
        ("planar_default_45_25", "Planar 45/25", "planar_default_45_25.png"),
        ("ssr_default_60_20", "SSR 60/20", "ssr_default_60_20.png"),
        ("planar_default_60_20", "Planar 60/20", "planar_default_60_20.png"),
        ("nonplanar", "Non-planar", "nonplanar.png"),
        ("water", "Water", "water.png"),
        ("cubemap", "Cubemap off/on", "cubemap_off.png", "cubemap_on.png"),
        ("cubemap_faces", "Cubemap faces", "cubemap_faces.png",
         "cubemap_faces_rotated.png"),
    ]
    for entry in fixed:
        card_id, label, first, *rest = entry
        paths = [("off", os.path.join(root, first))]
        if rest:
            paths.append(("on", os.path.join(root, rest[0])))
        else:
            paths[0] = ("capture", paths[0][1])
        if any(os.path.isfile(path) for _, path in paths):
            cards.append(card("reflect:" + card_id, "reflection", label, paths))
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


def fit(image, width, height):
    copy = image.copy()
    copy.thumbnail((width, height), Image.Resampling.LANCZOS)
    return copy


def render_sheet(cards, path):
    if not cards:
        return
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
            with Image.open(info["path"]).convert("RGB") as source:
                preview = fit(source, slot_width - 8, CARD_HEIGHT - 72)
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
        description="Build advisory GFX visual-review contact sheets.")
    parser.add_argument("--matrix-dir", default=DEFAULT_MATRIX)
    parser.add_argument("--reflect-dir", default=DEFAULT_REFLECT)
    parser.add_argument("--out", default=DEFAULT_OUT)
    parser.add_argument("--grade", action="append", default=[],
                        metavar="CARD_ID=GRADE")
    parser.add_argument("--note", action="append", default=[],
                        metavar="CARD_ID=TEXT")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    manifest_path = os.path.join(args.out, "visual_review.json")
    previous = load_review(manifest_path)
    cards = collect_matrix(args.matrix_dir) + collect_reflect(args.reflect_dir)
    apply_previous(cards, previous)
    apply_updates(cards, parse_updates(args.grade, "--grade"),
                  parse_updates(args.note, "--note"))

    matrix_cards = [item for item in cards if item["group"] == "matrix"]
    reflect_cards = [item for item in cards if item["group"] == "reflection"]
    render_sheet(matrix_cards, os.path.join(args.out, "matrix_contact_sheet.png"))
    render_sheet(reflect_cards,
                 os.path.join(args.out, "reflection_contact_sheet.png"))
    reviewed = sum(item["grade"] != UNREVIEWED for item in cards)
    manifest = {
        "schema": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "advisory": True,
        "visual_grades": list(VISUAL_GRADES),
        "sources": {"matrix": os.path.abspath(args.matrix_dir),
                    "reflection": os.path.abspath(args.reflect_dir)},
        "rubric": {
            "A+": "polished composition, materials, lighting, and no visible artifacts",
            "A": "strong authored result with only minor non-blocking roughness",
            "A-": "readable and correct, with noticeable but contained roughness",
            "B+": "good feature read with clear polish or composition gaps",
            "B": "functional image with several visible presentation compromises",
            "C": "feature is difficult to read or visually unstable",
        },
        "cards": cards,
        "reviewed_cards": reviewed,
        "card_count": len(cards),
    }
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print(f"VISUAL REVIEW: {reviewed}/{len(cards)} cards graded")
    print(f"VISUAL REVIEW: {manifest_path}")
    print(f"VISUAL REVIEW: {os.path.join(args.out, 'matrix_contact_sheet.png')}")
    print(f"VISUAL REVIEW: {os.path.join(args.out, 'reflection_contact_sheet.png')}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"VISUAL REVIEW: FAIL — {exc}")
        raise SystemExit(1)
