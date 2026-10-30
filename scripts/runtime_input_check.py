#!/usr/bin/env python3
#ai: real-source native heap rollback and bounded JSON parser admission regressions.
"""No engine windows, network access, real exhaustion, or author-file writes."""
import argparse
import json
import os
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_STACK, (8 * 1024 * 1024, 8 * 1024 * 1024))


def run(command, **kwargs):
    return subprocess.run(command, check=True, timeout=30, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--heap-source", type=Path)
    parser.add_argument("--json-source", type=Path)
    args = parser.parse_args()
    scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gx-runtime-input-", dir=scratch) as directory:
        work = Path(directory)
        flags = ["-std=gnu11", "-O1" if args.sanitize else "-O2", "-g",
                 "-ffunction-sections", "-fdata-sections"]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        compiler = shlex.split(os.environ.get("CC", "cc"))
        for name, override in [("heap", args.heap_source), ("json", args.json_source)]:
            definitions = [] if override is None else [f'-DGX_{name.upper()}_SOURCE="{override.resolve()}"']
            run(compiler + flags + definitions + [f"-I{ROOT / 'src/common'}",
                f"-I{ROOT / 'src/common/marshal'}", str(ROOT / f"scripts/runtime_{name}_probe.c"),
                "-Wl,--gc-sections", "-ldl", "-lm", "-o", str(work / name)])
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run([str(work / "heap")], env=env, preexec_fn=limits)
        cases = [
            ("valid", '{"n":-1.25e+2,"a":[true,false,null,"text"],"o":{}}', 1),
            ("whitespace", ' \n {"n":0} \t\r\n', 1),
            ("max_finite", '{"n":1.7976931348623157e308}', 1),
            ("tiny_finite", '{"n":1e-999}', 1),
            ("overflow", '{"n":1e309}', 0),
            ("negative_overflow", '{"n":-1e309}', 0),
            ("trailing", '{"n":1} garbage', 0),
            ("second_document", '{}{}', 0),
            ("nul_suffix", '{}\0ignored', 0),
            ("leading_zero", '{"n":01}', 0),
            ("bare_minus", '{"n":-}', 0),
            ("missing_fraction", '{"n":1.}', 0),
            ("missing_exponent", '{"n":1e+}', 0),
            ("short_literal", '[tru]', 0),
            ("depth_boundary", '[' * 64 + '0' + ']' * 64, 1),
            ("depth_over", '[' * 65 + '0' + ']' * 65, 0),
            ("mixed_boundary", '{"a":[' * 32 + '0' + ']}' * 32, 1),
            ("mixed_over", '{"a":[' * 33 + '0' + ']}' * 33, 0),
            ("deep", '[' * 200000 + '0' + ']' * 200000, 0),
        ]
        for name, text, accepted in cases:
            path = work / f"{name}.json"
            path.write_text(text)
            run([str(work / "json"), str(accepted), str(path)], env=env, preexec_fn=limits)
        oom = work / "oom.json"
        oom.write_text('{"n":2,"a":[null,true,"",{"u":"\\u20ac\\n"}],"long":"' + 'x' * 300 + '"}')
        run([str(work / "json"), "oom", str(oom)], env=env, preexec_fn=limits)
        for extra, accepted in [(0, 1), (1, 0)]:
            path = work / f"size_{extra}.json"
            with path.open("wb") as stream:
                stream.write(b"{}")
                stream.write(b" " * (16 * 1024 * 1024 - 2 + extra))
            run([str(work / "json"), str(accepted), str(path)], env=env, preexec_fn=limits)
        controls = 0
        for path in sorted((ROOT / "resources").rglob("*.json")):
            try:
                value = json.loads(path.read_text())
            except (ValueError, UnicodeError):
                continue
            if value is not None:
                run([str(work / "json"), "1", str(path)], env=env, preexec_fn=limits)
                controls += 1
        print(f"RUNTIME_INPUT: PASS; {len(cases) + 2} JSON boundaries, {controls} preset controls")


if __name__ == "__main__":
    main()
