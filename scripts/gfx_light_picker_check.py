#!/usr/bin/env python3
#opencode: pick and drag visible point-light markers through the ImGui input bridge.

from pathlib import Path

import audit_common as audit
import re
import subprocess
import sys
import tempfile

from PIL import Image


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "art2.json"


#opencode: launch one editor capture with private output and the user's art2 fixture.
def run_editor(output, actions="", frame=240):
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_SCREENSHOT_DIR": str(output),
        "GX_SHOT_FRAME": str(frame),
    })
    for key in ("GX_AI_INPUT", "GX_GFX_PRESET_SAVE", "GX_GFX_PRESET_BATCH"):
        environment.pop(key, None)
    if actions:
        environment["GX_AI_INPUT"] = actions
    result = subprocess.run(
        [str(BIN), "--gfx", "--gfx-editor", "--screenWidth", "1536",
         "--screenHeight", "1016", "--ai-screenshot-after", str(frame)],
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"editor exited {result.returncode}\n{result.stdout[-3000:]}")
    return result.stdout


#opencode: settings safety and window isolation live in audit_common.
with_settings_preserved = audit.with_settings_preserved


#opencode: use the screenshot's client dimensions so input maps to the shown marker.
def main():
    if not BIN.is_file() or not PRESET.is_file():
        print(f"GFX_LIGHT_PICKER_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_light_picker_",
                                         dir="/tmp/opencode") as temp:
            output = Path(temp)

            def exercise():
                layout = output / "layout"
                layout.mkdir()
                run_editor(layout, frame=60)
                screenshots = list(layout.glob("gXss_*.png"))
                if not screenshots:
                    raise RuntimeError("layout screenshot missing")
                image = Image.open(max(screenshots, key=lambda path: path.stat().st_mtime))
                width, height = image.size
                #opencode: marker positions are game-space constants (captured
                #   from the layout probe on the pre-inset dock); map them
                #   through the dock math including the inspector inset so a
                #   click lands on the marker at any window size.
                scale = min((width - 320.0 - 380.0) / 1536.0,
                            height / 1016.0, 1.0)
                viewport_y = (height - 1016.0 * scale) * 0.5
                redX = round(320.0 + 815.6 * scale)
                redY = round(viewport_y + 357.9 * scale)
                greenX = round(320.0 + 922.0 * scale)
                greenY = round(viewport_y + 690.9 * scale)
                dragX = redX + max(16, round(32.0 * scale))
                actions = ";".join((
                    f"1.0 move {redX} {redY} 150",
                    f"1.25 drag {redX} {redY} {dragX} {redY} 450",
                    f"2.1 move {greenX} {greenY} 150",
                    f"2.35 click {greenX} {greenY}",
                ))
                capture = output / "interaction"
                capture.mkdir()
                log = run_editor(capture, actions, frame=240)
                clean = re.sub(r"\x1b\[[0-9;]*m", "", log)
                if "GfxEd: picked light 0" not in clean:
                    raise RuntimeError(f"red marker was not picked\n{clean[-3000:]}")
                if "GfxEd: picked light 1" not in clean:
                    raise RuntimeError(f"green marker was not picked\n{clean[-3000:]}")
                match = re.search(
                    r"GfxEd: light drag end light=0 "
                    r"startPos=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\) "
                    r"pos=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)",
                    clean)
                if not match:
                    raise RuntimeError(f"drag did not move light 0\n{clean[-3000:]}")
                start = tuple(float(value) for value in match.groups()[:3])
                position = tuple(float(value) for value in match.groups()[3:])
                if abs(start[0] - 0.976) + abs(start[1] - 2.5) \
                        + abs(start[2] - 2.193) > 0.05:
                    raise RuntimeError(f"light 0 started somewhere unexpected: {start}")
                if sum(abs(a - b) for a, b in zip(start, position)) < 0.05:
                    raise RuntimeError(f"light drag left its position unchanged: {position}")

                #opencode: Ctrl+Z reverts one whole light drag; Ctrl+Y restores it.
                undo_dir = output / "undo"
                undo_dir.mkdir()
                undo_actions = ";".join((
                    f"1.0 move {redX} {redY} 150",
                    f"1.25 drag {redX} {redY} {dragX} {redY} 450",
                    "2.1 key 90 0 1",
                    "2.7 key 89 0 1",
                ))
                undo_log = re.sub(r"\x1b\[[0-9;]*m", "",
                    run_editor(undo_dir, undo_actions, frame=240))
                undo_match = re.search(
                    r"GfxEd: light history undo light=0 "
                    r"pos=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)", undo_log)
                redo_match = re.search(
                    r"GfxEd: light history redo light=0 "
                    r"pos=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)", undo_log)
                if not undo_match or not redo_match:
                    raise RuntimeError(f"light undo/redo history missing\n{undo_log[-3000:]}")
                undo_position = tuple(float(value) for value in undo_match.groups())
                redo_position = tuple(float(value) for value in redo_match.groups())
                if any(abs(a - b) > 0.01 for a, b in zip(undo_position, start)):
                    raise RuntimeError(
                        f"undo restored {undo_position}, expected {start}")
                if any(abs(a - b) > 0.01 for a, b in zip(redo_position, position)):
                    raise RuntimeError(
                        f"redo restored {redo_position}, expected {position}")

            with_settings_preserved(exercise)
            print("GFX_LIGHT_PICKER_CHECK: GX_AI_INPUT picked both visible lights, "
                  "dragged light 0, and Ctrl+Z/Y restored its position")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"GFX_LIGHT_PICKER_CHECK: FAIL: {error}", file=sys.stderr)
        return 1
    print("GFX_LIGHT_PICKER_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
