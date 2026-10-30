#!/usr/bin/env python3
#opencode: runtime uniform verifier companion to GX_UNITRACE (see the hook
#   in src/rhi/gl_draw.c). Joins per-program GetLocation results with
#   upload ranges to flag: uploads to locs never queried (hardcoded/stale
#   locs), queries resolving to -1 (missing uniform), overlapping mat4
#   ranges (location collision), uploads with no program bound, and
#   queried-but-never-uploaded names. Usage:
#     python3 scripts/gfx_unitrace.py run --frames 30 -- ./build/gX_build --showcase --ai-screenshot-final
#     python3 scripts/gfx_unitrace.py analyze /tmp/unitrace/log.txt
#   Sampler binds via the raylib path are not traced (documented gap).

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

import audit_common as audit

UP_RE = re.compile(r'GX_UNITRACE up prog=(-?\d+) loc=(-?\d+) end=(-?\d+) kind=(\w+)')
LOC_RE = re.compile(r'GX_UNITRACE loc prog=(\d+) name=(\S+) loc=(-?\d+)')


def parse_log(path):
    locs = {}  # (prog, name) -> loc (last wins)
    uploads = defaultdict(int)  # (prog, loc, end, kind) -> count
    with open(path, errors='replace') as handle:
        for line in handle:
            if 'GX_UNITRACE' not in line:
                continue
            m = LOC_RE.search(line)
            if m:
                prog, name, loc = int(m.group(1)), m.group(2), int(m.group(3))
                locs[(prog, name)] = loc
                continue
            m = UP_RE.search(line)
            if m:
                prog, loc, end, kind = (int(m.group(1)), int(m.group(2)),
                                        int(m.group(3)), m.group(4))
                uploads[(prog, loc, end, kind)] += 1
    return locs, uploads


def analyze(locs, uploads, only_prog=None, show_dead=True, show_miss=True):
    progs = set()
    for prog, _ in locs:
        progs.add(prog)
    for prog, _, _, _ in uploads:
        progs.add(prog)
    if only_prog is not None:
        progs = {p for p in progs if p == only_prog}

    names_by_prog = defaultdict(dict)
    for (prog, name), loc in locs.items():
        names_by_prog[prog][name] = loc
    ups_by_prog = defaultdict(list)
    for (prog, loc, end, kind), count in uploads.items():
        ups_by_prog[prog].append((loc, end, kind, count))

    total_flags = 0
    for prog in sorted(progs):
        if prog == 0:
            continue
        names = names_by_prog.get(prog, {})
        ups = ups_by_prog.get(prog, [])
        label_names = sorted(names)[:4]
        print(f"\n=== program {prog} "
              f"({'/'.join(label_names) if label_names else 'unnamed'}) ===")
        print(f"  queried names: {len(names)}, "
              f"upload sites: {len(ups)}, "
              f"uploads: {sum(c for _, _, _, c in ups)}")

        misses = sorted(n for n, loc in names.items() if loc < 0)
        if misses and show_miss:
            print(f"  MISS (-1, uniform absent): {', '.join(misses[:12])}"
                  f"{' ...' if len(misses) > 12 else ''}")
            total_flags += len(misses)

        # base locs covered by a query on this program
        covered = set()
        for loc in names.values():
            if loc >= 0:
                covered.add(loc)
        stale = [(loc, end, kind, c) for loc, end, kind, c in ups
                 if loc not in covered]
        if stale:
            print("  STALE/HARDCODED loc uploads (no query covers base):")
            for loc, end, kind, c in sorted(stale)[:12]:
                print(f"    loc {loc}..{end} {kind} x{c}")
            total_flags += len(stale)

        # overlapping ranges from different base locs (mat4 collision)
        ranges = sorted({(loc, end) for loc, end, _, _ in ups})
        collisions = set()
        for i, (a0, a1) in enumerate(ranges):
            for b0, b1 in ranges[i + 1:]:
                if a0 < b1 and b0 < a1:
                    collisions.add((a0, a1, b0, b1))
        if collisions:
            print("  COLLISION (overlapping ranges, different bases):")
            for a0, a1, b0, b1 in sorted(collisions)[:12]:
                print(f"    [{a0},{a1}) vs [{b0},{b1})")
            total_flags += len(collisions)

        uploaded_bases = {loc for loc, _, _, _ in ups}
        dead = sorted(n for n, loc in names.items()
                      if loc >= 0 and loc not in uploaded_bases)
        if dead and show_dead:
            print(f"  DEAD (queried, never uploaded): "
                  f"{', '.join(dead[:12])}"
                  f"{' ...' if len(dead) > 12 else ''}")

    zero_prog = [(loc, end, kind, c)
                 for (prog, loc, end, kind), c in uploads.items()
                 if prog == 0]
    if zero_prog:
        print(f"\nNO-PROGRAM uploads: {len(zero_prog)} sites "
              f"({sum(c for _, _, _, c in zero_prog)} calls) — silently lost")
        total_flags += len(zero_prog)

    print(f"\nflags: {total_flags}")
    return total_flags


def cmd_run(args):
    outdir = args.output
    os.makedirs(outdir, exist_ok=True)
    log_path = os.path.join(outdir, 'unitrace.log')
    #opencode: settings-safe, window-isolated trace run (the user command keeps
    #   its own args; only the audit env is injected).
    environment = audit.scene_env({
        'GX_UNITRACE': '1',
        'GX_SCREENSHOT_DIR': outdir,
        'GX_SHOT_FRAME': str(args.frames) if args.frames else None,
    }, clean=True)
    cmd = args.cmd
    print(f"running {' '.join(cmd)} (log {log_path})", flush=True)
    with open(log_path, 'w') as log:
        result = subprocess.run(cmd, env=environment, stdout=log,
                                stderr=subprocess.STDOUT,
                                timeout=args.timeout)
    print(f"exit={result.returncode}")
    locs, uploads = parse_log(log_path)
    return analyze(locs, uploads, only_prog=args.prog,
                   show_dead=not args.hide_dead,
                   show_miss=not args.hide_miss)


def cmd_analyze(args):
    locs, uploads = parse_log(args.log)
    hide_dead = getattr(args, 'hide_dead', False)
    hide_miss = getattr(args, 'hide_miss', False)
    only = getattr(args, 'prog', None)
    return analyze(locs, uploads, only_prog=only,
                   show_dead=not hide_dead, show_miss=not hide_miss)


def main():
    parser = argparse.ArgumentParser(description="gX runtime uniform audit")
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('run', help='execute binary under tracing, then audit')
    p.add_argument('--frames', type=int, default=30)
    p.add_argument('--timeout', type=float, default=120.0)
    p.add_argument('--output', default='/tmp/unitrace')
    p.add_argument('--prog', type=int, default=None)
    p.add_argument('--hide-dead', action='store_true')
    p.add_argument('--hide-miss', action='store_true')
    p.add_argument('cmd', nargs=argparse.REMAINDER,
                   help='binary + args after --')
    p.set_defaults(func=cmd_run)
    p = sub.add_parser('analyze', help='audit an existing log')
    p.add_argument('log')
    p.add_argument('--prog', type=int, default=None)
    p.add_argument('--hide-dead', action='store_true')
    p.add_argument('--hide-miss', action='store_true')
    p.set_defaults(func=cmd_analyze)
    args = parser.parse_args()
    if getattr(args, 'cmd', None) and args.cmd[0] == '--':
        args.cmd = args.cmd[1:]
    if getattr(args, 'cmd', None) == []:
        parser.error("run needs a binary command after --")
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
