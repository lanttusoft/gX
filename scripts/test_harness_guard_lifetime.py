# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: compile actual native test guard blocks against exact inherited values and allocation failures; no renderer or guard-policy replacement.
import ast
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def guard_block(path, start, end):
    source = (ROOT / path).read_text()
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


PRELUDE = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef char gX_I8;
typedef int gX_I32;
typedef void gX_I0;
#define gX_LOG_ERROR 1
static int savedCalls, failClone;
static inline char *test_clone(const char *value)
{
    if (failClone) return NULL;
    return strdup(value);
}
static int test_save(const char *path)
{
    const char *value = getenv("GX_NO_SETTINGS_SAVE");
    assert(value && !strcmp(value, "1"));
    assert(strstr(path, "resources/"));
    savedCalls++;
    return 1;
}
#define strdup test_clone
#define SYN_TEST_ASSERT(cond, msg) do { if (!(cond)) errors++; } while (0)
#define TW_TEST_ASSERT(cond, msg) SYN_TEST_ASSERT(cond, msg)
#define gX_TraceLog(...) ((void)0)
#define gX_Synth_WT_SaveFile(slot, path) test_save((const char *)(path))
#define gX_TextWriter_SaveFileTXT(writer, path) test_save((const char *)(path))
#define gX_ScenePhysics_Tuning_Save(path, tuning) test_save((const char *)(path))
'''

CASES = (
    ("synth", "src/audio/synth_tests.c", "    gX_I8 guardPath[128];", "    FILE *check ="),
    ("textwriter", "src/renderer/textwriter_tests.c", "            gX_I8 guardPath[128];", "            FILE *guardCheck ="),
    ("physics", "src/scene/physics_tests.c", "    const gX_I8 *guardPrev =", "    //ai: Apply drives"),
)


class HarnessGuardLifetime(unittest.TestCase):
    def check_case(self, case, fail_clone=False):
        name, path, start, end = case
        snippet = guard_block(path, start, end)
        body = PRELUDE + "\nstatic int probe(void)\n{\n    int errors = 0;\n" + snippet + "\n    return errors;\n}\n"
        body += r'''
int main(void)
{
    char longValue[4097];
    memset(longValue, 'q', sizeof(longValue)-1);
    longValue[sizeof(longValue)-1] = '\0';
    const char *values[] = {NULL, "", "1", "0", "inherited-harness-value", longValue};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++)
    {
        if (values[i]) assert(setenv("GX_NO_SETTINGS_SAVE", values[i], 1) == 0);
        else assert(unsetenv("GX_NO_SETTINGS_SAVE") == 0);
        savedCalls = 0;
        failClone = FAIL_CLONE;
        int errors = probe();
        const char *actual = getenv("GX_NO_SETTINGS_SAVE");
        if (values[i]) assert(actual && !strcmp(actual, values[i]));
        else assert(!actual);
        assert(errors == (failClone && values[i] ? 1 : 0));
        assert(savedCalls == (failClone && values[i] ? 0 : 1));
    }
    return 0;
}
'''
        body = body.replace("FAIL_CLONE", "1" if fail_clone else "0")
        with tempfile.TemporaryDirectory(prefix="guard_lifetime_", dir="/tmp/opencode") as directory:
            directory = Path(directory)
            source = directory / (name + ".c")
            executable = directory / name
            source.write_text(body)
            compiled = subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_synth_exact_inherited_value(self):
        self.check_case(CASES[0])

    def test_textwriter_exact_inherited_value(self):
        self.check_case(CASES[1])

    def test_physics_exact_inherited_value(self):
        self.check_case(CASES[2])

    def test_synth_snapshot_failure_does_not_change_guard(self):
        self.check_case(CASES[0], True)

    def test_textwriter_snapshot_failure_does_not_change_guard(self):
        self.check_case(CASES[1], True)

    def test_physics_snapshot_failure_does_not_change_guard(self):
        self.check_case(CASES[2], True)

    def test_preset_failure_handler_types_resolve(self):
        source = (ROOT / "scripts/gfx_preset_check.py").read_text()
        tree = ast.parse(source)
        namespace = {}
        for item in tree.body:
            if isinstance(item, ast.Import):
                exec(compile(ast.Module(body=[item], type_ignores=[]), "preset imports", "exec"), namespace)
        handlers = [item for item in ast.walk(tree) if isinstance(item, ast.ExceptHandler) and item.type is not None]
        for handler in handlers:
            classes = eval(compile(ast.Expression(body=handler.type), "preset handler", "eval"), namespace)
            self.assertTrue(all(issubclass(value, BaseException) for value in (classes if isinstance(classes, tuple) else (classes,))))


if __name__ == "__main__":
    unittest.main()
