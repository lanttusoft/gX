#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""Map peer-review scores (0-10) to S/A-F grades and enforce the gate."""

import argparse
import sys

#ai: bands are 0.3-0.5 wide; the minimum merge gate is B- (7.0) and the
#    target is A- (8.0). Keep this table in sync with docs/peer_review_gate.md.
PROFILES = {
    "ux": ("workflow/UX", "product/credibility", "accessibility"),
    "test": ("contract", "coverage", "determinism", "diagnostics"),
}

BANDS = (
    (9.5, "S+"),
    (9.0, "S"),
    (8.7, "A+"),
    (8.3, "A"),
    (8.0, "A-"),
    (7.7, "B+"),
    (7.3, "B"),
    (7.0, "B-"),
    (6.7, "C+"),
    (6.3, "C"),
    (6.0, "C-"),
    (5.7, "D+"),
    (5.3, "D"),
    (5.0, "D-"),
    (0.0, "F"),
)


def grade(score):
    """Return the letter grade for a 0-10 score; out-of-range values clamp."""
    value = max(0.0, min(10.0, float(score)))
    for threshold, letter in BANDS:
        if value >= threshold:
            return letter
    return "F"


def gate(scores, minimum=7.0, profile="ux"):
    """Return (passed, rows) for a review profile's lenses."""
    names = PROFILES.get(profile)
    if names is None:
        raise ValueError(f"unknown review profile: {profile}")
    if len(scores) != len(names):
        raise ValueError(
            f"profile {profile} needs {len(names)} scores, got {len(scores)}")
    rows = []
    passed = True
    for name, score in zip(names, scores):
        letter = grade(score)
        ok = float(score) >= minimum
        passed = passed and ok
        rows.append((name, float(score), letter, ok))
    return passed, rows


def parse_scores(values, profile="ux"):
    names = PROFILES.get(profile)
    if names is None:
        raise ValueError(f"unknown review profile: {profile}")
    if len(values) != len(names):
        raise ValueError(
            f"profile {profile} expects {len(names)} scores ({', '.join(names)})")
    scores = []
    for value in values:
        score = float(value)
        if score < 0.0 or score > 10.0:
            raise ValueError(f"score outside 0-10: {value}")
        scores.append(score)
    return scores


def main():
    parser = argparse.ArgumentParser(
        description="Grade peer-review scores and check the 7/10 merge gate.")
    parser.add_argument("scores", nargs="+", metavar="SCORE",
        help="0-10 scores for the selected profile")
    parser.add_argument("--profile", choices=tuple(PROFILES), default="ux",
        help="review profile (default ux)")
    parser.add_argument("--names", action="store_true",
        help="print the selected profile's lens names")
    parser.add_argument("--min", type=float, default=7.0,
        help="minimum passing score (default 7.0 = B-)")
    parser.add_argument("--target", type=float, default=8.0,
        help="release-line target (default 8.0 = A-)")
    args = parser.parse_args()
    if args.names:
        print(f"{args.profile}: {', '.join(PROFILES[args.profile])}")
        return 0
    try:
        scores = parse_scores(args.scores, args.profile)
    except ValueError as error:
        print(f"PEER_REVIEW: FAIL — {error}", file=sys.stderr)
        return 2
    passed, rows = gate(scores, args.min, args.profile)
    for name, score, letter, ok in rows:
        print(f"{name:<20} {score:4.1f}  {letter:<2}  {'pass' if ok else 'FAIL'}")
    target = all(score >= args.target for score in scores)
    print(f"merge gate ({args.min:.1f} = {grade(args.min)}): "
          f"{'PASS' if passed else 'FAIL'}")
    print(f"release target ({args.target:.1f} = {grade(args.target)}): "
          f"{'met' if target else 'not met'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
