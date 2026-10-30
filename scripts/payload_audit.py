#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: report exact lean-gate sections and approximate live-symbol source contributions.
import argparse
from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


#ai: keep parsed nm records explicit so exact and approximate accounting stay distinguishable.
@dataclass(frozen=True)
class Symbol:
    name: str
    kind: str
    address: int
    size: int


#ai: parse the GNU size sysv report used by lean_gate.sh.
def parse_size_sysv(text):
    sections = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 2 or not fields[0].startswith('.'):
            continue
        try:
            sections[fields[0]] = int(fields[1])
        except ValueError:
            continue
    return sections


#ai: compute the exact payload using the same .text + .rodata + .data contract as the gate.
def payload_metrics(sections, binary_bytes):
    text = sections.get('.text', 0)
    rodata = sections.get('.rodata', 0)
    data = sections.get('.data', 0)
    extra = {name: value for name, value in sections.items()
             if name.startswith(('.data.', '.got'))}
    return {
        'text_bytes': text,
        'rodata_bytes': rodata,
        'data_bytes': data,
        'payload_bytes': text + rodata + data,
        'bss_bytes': sections.get('.bss', 0),
        'binary_bytes': binary_bytes,
        'extra_initialized_sections': extra,
    }


#ai: parse the stable GNU nm POSIX format without demangling symbol names.
def parse_nm_posix(text):
    symbols = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        name, kind, address, size = fields[:4]
        try:
            symbols.append(Symbol(name, kind, int(address, 16), int(size, 16)))
        except ValueError:
            continue
    return symbols


#ai: run binutils commands with actionable errors instead of silently omitting attribution.
def run_tool(command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f'{command[0]} failed: {detail}')
    return result.stdout


#ai: map live binary symbols to a unique object definition where possible.
def symbol_origins(binary_symbols, object_root):
    objects = sorted(object_root.rglob('*.o'))
    if not objects:
        return {}, 0
    wanted = {(symbol.name, symbol.kind, symbol.size) for symbol in binary_symbols}
    candidates = defaultdict(set)
    for start in range(0, len(objects), 64):
        batch = objects[start:start + 64]
        output = run_tool(['nm', '-A', '--format=posix', '-S', '--defined-only',
                           *map(str, batch)])
        for line in output.splitlines():
            object_name, separator, record = line.partition(': ')
            if not separator:
                continue
            fields = record.split()
            if len(fields) < 4:
                continue
            name, kind, address, size = fields[:4]
            try:
                key = (name, kind, int(size, 16))
            except ValueError:
                continue
            if key in wanted:
                candidates[key].add(Path(object_name))
    origins = {}
    ambiguous = 0
    for symbol in binary_symbols:
        matches = candidates.get((symbol.name, symbol.kind, symbol.size), set())
        if len(matches) == 1:
            origins[symbol] = next(iter(matches))
        elif len(matches) > 1:
            ambiguous += 1
    return origins, ambiguous


#ai: summarize symbol-size estimates by the object that supplied each live symbol.
def object_estimates(binary_symbols, origins, root):
    totals = defaultdict(lambda: {'text_bytes': 0, 'rodata_bytes': 0,
                                  'data_bytes': 0, 'symbols': 0})
    seen = set()
    for symbol, path in origins.items():
        location = (symbol.address, symbol.size, symbol.kind)
        if location in seen:
            continue
        seen.add(location)
        try:
            label = str(path.relative_to(root))
        except ValueError:
            label = str(path)
        total = totals[label]
        total['symbols'] += 1
        if symbol.kind in 'Tt':
            total['text_bytes'] += symbol.size
        elif symbol.kind in 'Rr':
            total['rodata_bytes'] += symbol.size
        elif symbol.kind in 'DdGgSs':
            total['data_bytes'] += symbol.size
    rows = []
    for path, values in totals.items():
        total_bytes = sum(values[key] for key in
                          ('text_bytes', 'rodata_bytes', 'data_bytes'))
        rows.append({'object': path, 'named_symbol_bytes': total_bytes, **values})
    return sorted(rows, key=lambda row: row['named_symbol_bytes'], reverse=True)


#ai: build the full payload report and keep linker section accounting separate from nm estimates.
def audit(binary, object_root, top):
    size_output = run_tool(['size', '--format=sysv', str(binary)])
    sections = parse_size_sysv(size_output)
    if not all(section in sections for section in ('.text', '.rodata', '.data')):
        raise RuntimeError('size output is missing a lean-gate payload section')
    binary_symbols = [symbol for symbol in parse_nm_posix(
        run_tool(['nm', '--format=posix', '-S', '--defined-only', str(binary)]))
        if symbol.size > 0]
    origins, ambiguous = symbol_origins(binary_symbols, object_root)
    text_symbols = sorted((symbol for symbol in binary_symbols if symbol.kind in 'Tt'),
                          key=lambda symbol: symbol.size, reverse=True)
    rows = object_estimates(binary_symbols, origins, ROOT)
    metrics = payload_metrics(sections, binary.stat().st_size)
    metrics.update({
        'top_text_symbols': [
            {'name': symbol.name, 'bytes': symbol.size,
             'object': str(origins[symbol].relative_to(ROOT))
             if symbol in origins and origins[symbol].is_relative_to(ROOT) else None}
            for symbol in text_symbols[:top]],
        'top_objects_by_named_symbol_bytes': rows[:top],
        'live_named_symbols': len(binary_symbols),
        'attributed_symbols': len(origins),
        'ambiguous_symbols': ambiguous,
    })
    return metrics


#ai: print exact gate metrics and label source-level symbol sums as estimates.
def print_report(report, baseline_path, budget_path, top):
    print('PAYLOAD_AUDIT: exact linked sections (same definition as lean_gate.sh)')
    print(f"  text    {report['text_bytes']:>10,} bytes")
    print(f"  rodata  {report['rodata_bytes']:>10,} bytes")
    print(f"  data    {report['data_bytes']:>10,} bytes")
    print(f"  payload {report['payload_bytes']:>10,} bytes")
    print(f"  bss     {report['bss_bytes']:>10,} bytes (separate cap)")
    print(f"  file    {report['binary_bytes']:>10,} bytes")
    if baseline_path.exists() and budget_path.exists():
        baseline = json.loads(baseline_path.read_text())
        budget = json.loads(budget_path.read_text())
        image_cap = int(budget['image_max_bytes'])
        bss_cap = int(budget['bss_max_bytes'])
        image_delta = report['payload_bytes'] - int(baseline['image_bytes'])
        print(f"  baseline {baseline['image_bytes']:,} bytes; delta {image_delta:+,}; "
              f"cap {image_cap:,}; headroom {image_cap-report['payload_bytes']:+,}")
        print(f"  bss cap {bss_cap:,}; headroom {bss_cap-report['bss_bytes']:+,}")
    if report['extra_initialized_sections']:
        extra = ', '.join(f'{name}={size:,}' for name, size in
                          sorted(report['extra_initialized_sections'].items()))
        print(f'  initialized sections outside lean payload sum: {extra}')
    print(f'\nLargest {top} live text symbols:')
    for symbol in report['top_text_symbols']:
        origin = symbol['object'] or '[object not uniquely mapped]'
        print(f"  {symbol['bytes']:>8,}  {symbol['name']}  [{origin}]")
    print(f'\nLargest {top} source objects by mapped named-symbol estimate:')
    for row in report['top_objects_by_named_symbol_bytes']:
        print(f"  {row['named_symbol_bytes']:>8,}  {row['object']} "
              f"(text {row['text_bytes']:,}, rodata {row['rodata_bytes']:,}, "
              f"data {row['data_bytes']:,}; {row['symbols']} symbols)")
    print(f"\nMapped {report['attributed_symbols']:,}/{report['live_named_symbols']:,} "
          f"live named symbols; {report['ambiguous_symbols']:,} ambiguous.")
    print('Object totals are symbol-size estimates, not exact section attribution; '
          'unnamed sections, aliases, and linker GC prevent exact source sums.')


#ai: expose the audit as an optional script and JSON report without modifying build configuration.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'build/release/gX_build')
    parser.add_argument('--object-root', type=Path)
    parser.add_argument('--top', type=int, default=20)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    binary = args.binary.resolve()
    object_root = (args.object_root or binary.parent).resolve()
    if args.top < 1:
        parser.error('--top must be at least 1')
    if not binary.is_file():
        parser.error(f'binary does not exist: {binary}')
    if not shutil.which('size') or not shutil.which('nm'):
        parser.error('GNU binutils (size and nm) are required')
    try:
        report = audit(binary, object_root, args.top)
    except (OSError, RuntimeError) as error:
        print(f'PAYLOAD_AUDIT: {error}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_report(report, ROOT/'.lean_baseline', ROOT/'.lean_budget.json', args.top)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
