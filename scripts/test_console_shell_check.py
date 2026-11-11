#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: negative controls for the console gate itself; no engine or display needed.
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import console_shell_check as console


def element(index, text, x, *, width=180, height=40, y=100, disabled=0,
            visible=1, clickable=1, focusable=1, truncated=0, overflow=0,
            shrink=1.0, measure_height=20):
    return (f'GUITRACE: element canvas=0 element={index} name="control-{index}" '
            f'text="{text}" rect={x},{y},{width},{height} visible={visible} '
            f'disabled={disabled} clickable={clickable} focusable={focusable} '
            f'textfit=100,{measure_height} wraps=0 truncated={truncated} '
            f'overflow={overflow} shrink={shrink:.2f}\n')


def build(*rows):
    return ('GUITRACE: canvas-build index=0 template=1 name="Console" '
            f'elements={len(rows)}\n' + ''.join(rows))


class ConsoleGateTests(unittest.TestCase):
    def setUp(self):
        self.first = element(0, "ONE", 20)
        self.second = element(1, "TWO", 240)
        self.log = build(self.first, self.second)

    def check_layout(self, log):
        return console.assert_layout(log, 640, 480, ["ONE", "TWO"])

    def test_valid_layout_and_label_owned_click(self):
        rows = self.check_layout(self.log)
        self.assertEqual(console.interaction.center(console.target(rows, "TWO")),
                         (330, 120))

    def test_latest_build_replaces_stale_targets(self):
        rows = console.elements(self.log + build(element(0, "NEW", 20)))
        self.assertEqual([row["text"] for row in rows], ["NEW"])
        with self.assertRaisesRegex(RuntimeError, "ONE"):
            console.target(rows, "ONE")

    def test_missing_or_partial_trace_is_not_a_pass(self):
        for log in ("", 'GUITRACE: canvas-build index=0 template=1 '
                    'name="Console" elements=2\n' + self.first,
                    self.log.replace('overflow=0 shrink=1.00', '')):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                self.check_layout(log)

    def test_heap_high_water_mark_allows_unused_slots(self):
        log = build(self.first, element(3, "TWO", 240)).replace('elements=2', 'elements=4')
        self.check_layout(log)

    def test_missing_extra_or_inert_focus_targets_fail(self):
        for kwargs in ({"disabled": 1}, {"visible": 0}, {"focusable": 0},
                       {"clickable": 0}):
            with self.subTest(kwargs=kwargs), self.assertRaises(RuntimeError):
                self.check_layout(build(self.first, element(1, "TWO", 240, **kwargs)))
        with self.assertRaises(RuntimeError):
            self.check_layout(build(self.first, self.second, element(2, "DEAD", 440)))

    def test_clip_shrink_and_tiny_controls_fail(self):
        for kwargs in ({"truncated": 1}, {"overflow": 1}, {"shrink": 0.6},
                       {"measure_height": 0}, {"measure_height": 9}, {"height": 20}):
            with self.subTest(kwargs=kwargs), self.assertRaises(RuntimeError):
                self.check_layout(build(self.first, element(1, "TWO", 240, **kwargs)))

    def test_offscreen_and_overlap_fail(self):
        for x in (-3, 190, 600):
            with self.subTest(x=x), self.assertRaises(RuntimeError):
                self.check_layout(build(self.first, element(1, "TWO", x)))

    def test_disabled_and_ambiguous_click_targets_fail(self):
        for log in (build(element(0, "ONE", 20, disabled=1)),
                    build(self.first, element(1, "ONE", 240))):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                console.target(console.elements(log), "ONE")

    def test_focus_walk_requires_input_and_ordered_wrap(self):
        rows = self.check_layout(self.log)
        directions = ["down", "down", "down", "up", "left", "right"]

        def events(ids):
            return ''.join(f'GUITRACE: focus canvas=0 element={index} detail=""\n'
                           for index in ids)

        console.assert_focus_walk(events([0, 1, 0, 1, 0, 1]), rows, directions)
        for ids in ([], [0, 1], [0] * 6, [0, 1, 0, 1, 2, 1], [0, 0, 1, 1, 0, 1]):
            with self.subTest(ids=ids), self.assertRaises(RuntimeError):
                console.assert_focus_walk(events(ids), rows, directions)

    def test_gl_invalid_operation_and_high_debug_fail_console_gates(self):
        console.check_errors(
            "GL_DEBUG [MEDIUM] src=0x8246 id=131218 Program/shader state "
            "performance warning: Vertex shader is being recompiled")
        for log in (
                "GL_DEBUG [HIGH] src=0x824C id=7 GL_INVALID_OPERATION",
                "GL_DEBUG [HIGH] src=0x824C id=7 texture is immutable",
                "[ ERROR ] GL_INVALID_OPERATION: target mismatch"):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                console.check_errors(log)
        console.check_errors(
            "GL_DEBUG [HIGH] src=0x824C id=7 expected fixture error",
            allowed=(r"expected fixture error",))

    def test_final_subprocess_environment_drops_ambient_hooks(self):
        ambient = {"GX_GUI_TEMPLATE_FORCE": "GUITESTER", "GX_GUI_TITLE_VIEW": "4",
                   "GX_AI_INPUT": "1.0 key 256", "GX_CAM_CUT_FRAME": "1"}
        with mock.patch.dict(os.environ, ambient), \
                mock.patch.object(console.audit.subprocess, "run") as run:
            run.return_value = mock.Mock(returncode=0, stdout="")
            console.audit.run_scene("--console-shell", {"GX_GUI_TRACE": "1"}, clean=True)
            environment = run.call_args.kwargs["env"]
            self.assertEqual(environment["GX_GUI_TRACE"], "1")
            self.assertEqual(environment["GX_NO_SETTINGS_SAVE"], "1")
            for key in ambient:
                self.assertNotIn(key, environment)


#ai: M8 — the console gates must own their 320x240 Soft fixture and restore the
#   user file exactly, including after an exception.
class ConsoleSettingsFixtureTests(unittest.TestCase):
    def test_console_fixture_is_deterministic_and_removed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(console.audit, "ROOT", root):
                with console.audit.console_settings():
                    settings = root / "resources" / "settings.json"
                    data = json.loads(settings.read_text(encoding="utf-8"))
                    self.assertEqual(data["screenWidth"], 320)
                    self.assertEqual(data["screenHeight"], 240)
                    self.assertEqual(data["presentFilter"], 1)
                    self.assertEqual(data["consoleSSAA2x"], 0)
                self.assertFalse(settings.exists())

    def test_existing_settings_are_restored_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            settings = root / "resources" / "settings.json"
            settings.parent.mkdir(parents=True)
            original = b'{"user":true,"screenWidth":1024}\n'
            settings.write_bytes(original)
            with mock.patch.object(console.audit, "ROOT", root):
                with console.audit.console_settings():
                    self.assertNotEqual(settings.read_bytes(), original)
            self.assertEqual(settings.read_bytes(), original)

    def test_settings_are_restored_after_exception(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            settings = root / "resources" / "settings.json"
            settings.parent.mkdir(parents=True)
            original = b'{"user":true}\n'
            settings.write_bytes(original)
            with mock.patch.object(console.audit, "ROOT", root), \
                    self.assertRaisesRegex(RuntimeError, "probe failed"):
                with console.audit.console_settings():
                    raise RuntimeError("probe failed")
            self.assertEqual(settings.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
