#!/usr/bin/env python3
#ai: opt-in mutation test for the math gates; builds a disposable copy and
#   requires each single-line formula mutation to turn at least one gate red.
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GATES = (
    ("math-pass", "math_pass.py"),
    ("math-random", "math_random_check.py"),
    ("math-property", "math_property_check.py"),
)

MUTATIONS = (
    ("matrix_transpose",
     "src/gxmath/matrix.c",
     "    result.m1 = mat.m4;\n",
     "    result.m1 = mat.m1;\n"),
    ("quaternion_multiply",
     "src/gxmath/quaternion.c",
     "    r.x = q1x*q2w + q1w*q2x + q1y*q2z - q1z*q2y;\n",
     "    r.x = q1x*q2w + q1w*q2x + q1y*q2z + q1z*q2y;\n"),
    ("sdf_sphere_radius",
     "src/gxmath/sdf.c",
     "    return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) - r;\n",
     "    return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) + r;\n"),
    ("foot_to_meter",
     "src/gxmath/convert.c",
     "    return 0.3048f * foots;\n",
     "    return 0.3049f * foots;\n"),
    ("vector_cross_x",
     "src/gxmath/vector3.c",
     "    gX_F32v3 result = { v1.y*v2.z - v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x };\n",
     "    gX_F32v3 result = { v1.y*v2.z + v1.z*v2.y, v1.z*v2.x - v1.x*v2.z, v1.x*v2.y - v1.y*v2.x };\n"),
)


class MutationError(RuntimeError):
    """Raised when the disposable tree or a mutation cannot be prepared."""


def run(command, cwd, env=None, timeout=900):
    return subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                          text=True, timeout=timeout)


def copy_tree(work):
    ignore = shutil.ignore_patterns(
        ".git", "build", ".worktrees", "art", "resources", "docs",
        "certificates", "save", "screenshot*", "gX_build", "*.bundle",
    )
    shutil.copytree(ROOT, work, ignore=ignore, symlinks=True, dirs_exist_ok=True)


def build(work):
    config = run(["cmake", "-B", "build", ".", "-DCMAKE_BUILD_TYPE=Debug"], work)
    if config.returncode != 0:
        raise MutationError(f"cmake configure failed:\n{config.stdout}\n{config.stderr}")
    compile_run = run(["cmake", "--build", "build", "-j", str(os.cpu_count() or 2)], work)
    if compile_run.returncode != 0:
        raise MutationError(f"build failed:\n{compile_run.stdout}\n{compile_run.stderr}")


def gate_env():
    env = os.environ.copy()
    env["GX_NO_SETTINGS_SAVE"] = "1"
    env["PATH"] = env.get("PATH", "/usr/bin:/bin")
    return env


def run_gates(work):
    results = {}
    for name, script in GATES:
        completed = run([sys.executable, f"scripts/{script}", "--binary", "build/gX_build"],
                        work, env=gate_env(), timeout=300)
        results[name] = completed.returncode == 0
    return results


def apply_mutation(work, relative_path, old, new):
    path = work / relative_path
    text = path.read_text()
    if text.count(old) != 1:
        raise MutationError(f"{relative_path}: mutation anchor found {text.count(old)} times")
    path.write_text(text.replace(old, new, 1))
    return text


def main():
    parser = argparse.ArgumentParser(description="gX math gate mutation test")
    parser.add_argument("--keep", action="store_true",
                        help="keep the disposable tree for inspection")
    parser.add_argument("--scratch", default=os.environ.get("GX_SCRATCH", "/tmp/opencode"),
                        help="scratch directory (default: GX_SCRATCH or /tmp/opencode)")
    args = parser.parse_args()

    scratch = Path(args.scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="gx-math-mutation-", dir=scratch))
    keep = args.keep
    try:
        print(f"math-mutation: copying source to {work}")
        copy_tree(work)
        print("math-mutation: building baseline")
        build(work)
        baseline = run_gates(work)
        if not all(baseline.values()):
            raise MutationError(f"baseline gates are not green: {baseline}")

        detected = 0
        for name, relative_path, old, new in MUTATIONS:
            original = apply_mutation(work, relative_path, old, new)
            try:
                build(work)
                results = run_gates(work)
            finally:
                (work / relative_path).write_text(original)
            red = [gate for gate, passed in results.items() if not passed]
            if red:
                detected += 1
                print(f"  {name:<20} detected by {', '.join(red)}")
            else:
                keep = True
                print(f"  {name:<20} NOT DETECTED: all gates stayed green")
        if detected != len(MUTATIONS):
            raise MutationError(f"only {detected}/{len(MUTATIONS)} mutations detected")
        print(f"MATH_MUTATION: PASS — {detected}/{len(MUTATIONS)} mutations detected")
        return 0
    except Exception as exc:
        keep = True
        print(f"math-mutation: {exc}", file=sys.stderr)
        return 1
    finally:
        if keep:
            print(f"math-mutation: kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
