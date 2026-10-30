#!/usr/bin/env python3
"""
gX backend-guard strip (compression P5) — single-backend raylib.

Removes preprocessor branches for backends gX never builds (GLFW, LIBDRAGON,
PS1, PS2, SDL, VULKAN, NULL, N64) and unwraps always-true guards
(GAME_BACKEND_RAYLIB / GAME_BACKEND_GX), leaving clean unconditional code.

Only guards whose condition evaluates under gX's defines are touched; all
other preprocessor structure is preserved verbatim.

Usage:
    python3 scripts/strip_backends.py <file> [--dry-run]
    python3 scripts/strip_backends.py --all [--dry-run]
"""

import re
import subprocess
import sys

TRUE_BACKENDS = {"GAME_BACKEND_RAYLIB", "GAME_BACKEND_GX"}
KNOWN_BACKENDS = TRUE_BACKENDS | {
    "GAME_BACKEND_GLFW", "GAME_BACKEND_LIBDRAGON", "GAME_BACKEND_PS1",
    "GAME_BACKEND_PS2", "GAME_BACKEND_SDL", "GAME_BACKEND_VULKAN",
    "GAME_BACKEND_NULL", "GAME_BACKEND_N64",
}

TOK = re.compile(r"(defined\s*\(\s*\w+\s*\)|!\s*defined\s*\(\s*\w+\s*\)|&&|\|\||\w+)")


def eval_cond(cond):
    """Evaluate a preprocessor condition under gX's defines.
    Returns True/False for known conditions, None for unparsable/unknown."""
    cond = re.sub(r"\s*\\\s*\n\s*", " ", cond)
    cond = re.sub(r"/\*.*?\*/", "", cond, flags=re.S)
    cond = cond.split("//")[0].strip()
    if not cond:
        return None
    if cond in ("0", "1"):
        return cond == "1"
    if not re.search(r"GAME_BACKEND", cond):
        return None
    tokens = TOK.findall(cond)
    if not tokens:
        return None
    expr = []
    for t in tokens:
        t = t.strip()
        if t == "||":
            expr.append(" or ")
        elif t == "&&":
            expr.append(" and ")
        elif t.startswith("!"):
            name = re.search(r"!\s*defined\s*\(\s*(\w+)\s*\)", t)
            expr.append("not " + str(name.group(1) in TRUE_BACKENDS))
        elif t.startswith("defined"):
            name = re.search(r"defined\s*\(\s*(\w+)\s*\)", t)
            expr.append(str(name.group(1) in TRUE_BACKENDS))
        elif t in KNOWN_BACKENDS:
            expr.append(str(t in TRUE_BACKENDS))
        else:
            return None
    try:
        return eval(" ".join(expr))
    except Exception:
        return None


def strip_file(path, dry_run=False):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines(keepends=True)

    out = []
    stack = []  # entries: True | False | None | "done"
    changed = False

    for line in lines:
        m = re.match(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", line)
        if not m:
            if stack and (stack[-1] is False or stack[-1] == "done"):
                changed = True
            else:
                out.append(line)
            continue

        kind, rest = m.group(1), m.group(2)
        cond = rest.strip()

        if kind == "if":
            val = eval_cond(cond)
            stack.append(val)
            if val is False or val == "done":
                changed = True
            elif val is True:
                changed = True  # the #if line itself is dropped
            else:
                out.append(line)  # unknown — keep the guard line
            continue

        if kind == "ifdef":
            name = cond.split()[0].strip()
            val = (name in TRUE_BACKENDS) if name in KNOWN_BACKENDS else None
            stack.append(val)
            if val is False or val == "done":
                changed = True
            elif val is True:
                changed = True
            else:
                out.append(line)
            continue

        if kind == "ifndef":
            name = cond.split()[0].strip()
            val = (name not in TRUE_BACKENDS) if name in KNOWN_BACKENDS else None
            stack.append(val)
            if val is False or val == "done":
                changed = True
            elif val is True:
                changed = True
            else:
                out.append(line)
            continue

        if kind == "elif":
            if not stack:
                out.append(line)
                continue
            top = stack[-1]
            if top is True:
                stack[-1] = "done"
                changed = True
            elif top == "done":
                changed = True
            elif top is False:
                val = eval_cond(cond)
                if val is True:
                    stack[-1] = True
                    changed = True
                elif val is False:
                    changed = True  # stays false
                else:
                    stack[-1] = None  # unknown — pass through
                    out.append(line)
                    changed = True
            else:  # None (unknown top) — keep structure
                out.append(line)
            continue

        if kind == "else":
            if not stack:
                out.append(line)
                continue
            top = stack[-1]
            if top is True or top == "done":
                stack[-1] = "done"
                changed = True
            elif top is False:
                stack[-1] = True
                changed = True
            else:  # None — keep
                out.append(line)
            continue

        if kind == "endif":
            if not stack:
                out.append(line)
                continue
            top = stack.pop()
            if top is None:
                out.append(line)  # unknown guard — keep its #endif
            else:
                changed = True
            continue

    if stack:
        print(f"WARN: {path}: unbalanced #if stack ({len(stack)} open)", file=sys.stderr)

    if not changed:
        return False
    if dry_run:
        return True
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(out)
    return True


def main():
    args = [a for a in sys.argv[1:]]
    dry = "--dry-run" in args
    args = [a for a in args if a != "--dry-run"]

    if args == ["--all"]:
        r = subprocess.run(
            ["grep", "-rlE",
             "GAME_BACKEND_(GLFW|LIBDRAGON|PS1|PS2|SDL|VULKAN|NULL|N64)",
             "src", "--include=*.c", "--include=*.h"],
            capture_output=True, text=True)
        files = r.stdout.splitlines()
    else:
        files = args

    if not files:
        print("no files", file=sys.stderr)
        sys.exit(1)

    for path in files:
        if dry:
            if strip_file(path, dry_run=True):
                print(f"CHANGED: {path}")
        else:
            if strip_file(path):
                print(f"stripped: {path}")
    sys.exit(0)


if __name__ == "__main__":
    main()
