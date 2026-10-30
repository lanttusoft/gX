#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: headless gfx analysis toolkit. Pure image ops over engine
#   captures (no engine coupling): side-by-side composites, frame stats,
#   A/B diffs, and block-match flow. Formalizes the ad-hoc analyses used
#   across the SSR/TAA/motion investigations so they are one command each.
#   Convention: all metrics run over the lower-frame ROI (below 30% height,
#   1px border) to skip HUD text; matches ssr_lab ROI handling.

import argparse
import os
import sys

import numpy as np
from PIL import Image


def load_rgb(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)


def roi_slice(shape, top_frac=0.30):
    h, w = shape[:2]
    return (slice(int(h * top_frac), h - 2), slice(1, w - 1))


def hf_map(gray):
    hor = np.abs(gray[:, 2:] - 2.0 * gray[:, 1:-1] + gray[:, :-2])
    ver = np.abs(gray[2:, :] - 2.0 * gray[1:-1, :] + gray[:-2, :])
    return 0.5 * (hor[1:-1, :] + ver[:, 1:-1])


def cmd_sbs(args):
    left = Image.open(args.a).convert('RGB')
    right = Image.open(args.b).convert('RGB')
    if args.crop:
        x, y, w, h = args.crop
        box = (x, y, x + w, y + h)
        left = left.crop(box)
        right = right.crop(box)
    if left.size != right.size:
        right = right.resize(left.size, Image.LANCZOS)
    canvas = Image.new('RGB', (left.width * 2 + 10, left.height),
                       (255, 0, 255))
    canvas.paste(left, (0, 0))
    canvas.paste(right, (left.width + 10, 0))
    if args.scale != 1.0:
        canvas = canvas.resize((int(canvas.width * args.scale),
                                int(canvas.height * args.scale)),
                               Image.LANCZOS)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    canvas.save(args.output)
    print(f"wrote {args.output} {canvas.size}")


def cmd_stats(args):
    m = load_rgb(args.image)
    gray = m.mean(axis=2)
    rs, cs = roi_slice(m.shape)
    region = gray[rs, :][:, cs]
    hlap = hf_map(gray)[rs, :][:, cs]
    ex = np.abs(gray[:, 1:] - gray[:, :-1]) > 12.0
    ey = np.abs(gray[1:, :] - gray[:-1, :]) > 12.0
    edge = 50.0 * (ex[rs, :][:, cs].mean() + ey[rs, :][:, cs].mean())
    print(f"{args.image}: mean={m.mean() / 255.0:.4f} "
          f"hf={hlap.mean():.4f} edge={edge:.3f}%")
    if args.regions:
        h, w = gray.shape
        named = {
            'sky': (slice(5, 60), slice(w // 2 - 50, w //2 + 50)),
            'floor-mid': (slice(int(h * 0.55), int(h * 0.65)),
                          slice(w // 2 - 100, w // 2 + 100)),
            'sphere': (slice(int(h * 0.2), int(h * 0.4)),
                       slice(0, int(w * 0.15))),
        }
        for name, (r2, c2) in named.items():
            sub = gray[r2, :][:, c2]
            print(f"  {name}: mean={sub.mean() / 255.0:.4f}")


def cmd_diff(args):
    a = load_rgb(args.a)
    b = load_rgb(args.b)
    if a.shape != b.shape:
        raise SystemExit("size mismatch "
                         f"{a.shape} vs {b.shape}")
    d = np.abs(a - b).mean(axis=2)
    rs, cs = roi_slice(a.shape)
    roi = d[rs, :][:, cs]
    print(f"mean|d|={roi.mean():.2f} cov>4={100.0 * (roi > 4).mean():.2f}% "
          f"p95={np.percentile(roi, 95):.1f}")
    if args.heatmap:
        heat = np.clip(roi / 64.0, 0.0, 1.0)
        colored = np.zeros((roi.shape[0], roi.shape[1], 3),
                           dtype=np.uint8)
        colored[..., 0] = (heat * 255).astype(np.uint8)
        os.makedirs(os.path.dirname(os.path.abspath(args.heatmap)),
                    exist_ok=True)
        Image.fromarray(colored).save(args.heatmap)
        print(f"wrote {args.heatmap}")


def block_flow(a, b, cx, cy, r=14, srng=60, step=2):
    T = a[cy - r:cy + r, cx - r:cx + r]
    best = (1e18, 0, 0)
    for dy in range(-srng, srng + 1, step):
        for dx in range(-srng, srng + 1, step):
            S = b[cy - r + dy:cy + r + dy, cx - r + dx:cx + r + dx]
            s = ((T - S) ** 2).mean()
            if s < best[0]:
                best = (s, dx, dy)
    _, bx, by = best
    best = (1e18, 0, 0)
    for dy in range(by - step, by + step + 1):
        for dx in range(bx - step, bx + step + 1):
            S = b[cy - r + dy:cy + r + dy, cx - r + dx:cx + r + dx]
            s = ((T - S) ** 2).mean()
            if s < best[0]:
                best = (s, dx, dy)
    return best[1], best[2]


def cmd_flow(args):
    a = load_rgb(args.a).mean(axis=2)
    b = load_rgb(args.b).mean(axis=2)
    h, w = a.shape
    if args.points:
        pts = []
        for spec in args.points:
            x, y = spec.split(',')
            pts.append((int(x), int(y)))
    else:
        pts = [(w // 2, h // 2)]
    for cx, cy in pts:
        dx, dy = block_flow(a, b, cx, cy, r=args.window,
                            srng=args.range)
        print(f"({cx},{cy}): flow=({dx:+d},{dy:+d})px")


def main():
    parser = argparse.ArgumentParser(description="gX headless gfx probe")
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('sbs', help='side-by-side composite')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--output', required=True)
    p.add_argument('--crop', nargs=4, type=int, default=None,
                   metavar=('X', 'Y', 'W', 'H'))
    p.add_argument('--scale', type=float, default=1.0)
    p.set_defaults(func=cmd_sbs)

    p = sub.add_parser('stats', help='mean/hf/edge + regions')
    p.add_argument('image')
    p.add_argument('--regions', action='store_true')
    p.set_defaults(func=cmd_stats)

    p = sub.add_parser('diff', help='A/B delta + coverage')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--heatmap', default=None)
    p.set_defaults(func=cmd_diff)

    p = sub.add_parser('flow', help='block-match flow at points')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--points', action='append', default=[],
                   metavar='X,Y')
    p.add_argument('--window', type=int, default=14)
    p.add_argument('--range', type=int, default=60)
    p.set_defaults(func=cmd_flow)

    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == '__main__':
    sys.exit(main())
