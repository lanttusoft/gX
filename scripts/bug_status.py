#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: local OPEN/FIXED status codes for private offline bug reports; only explicit set changes a status.
import argparse
import json
import os
from pathlib import Path
import re
import tempfile


#ai: these numeric values are the persisted report lifecycle contract.
STATUS_LABELS = {0: 'OPEN', 1: 'FIXED'}
STATUS_CODES = {'OPEN': 0, 'FIXED': 1, '0': 0, '1': 1}
STATUS_LINE = re.compile(r'(?m)^([ \t]*"status_code"[ \t]*:[ \t]*)-?\d+([ \t]*,?)[ \t]*$')


#ai: absent codes remain backward-compatible as OPEN until initialization writes them explicitly.
def read_report(path):
    text = path.read_text(encoding='utf-8')
    if len(STATUS_LINE.findall(text)) > 1:
        raise ValueError(str(path) + ': duplicate top-level status_code lines')
    document = json.loads(text)
    if not isinstance(document, dict):
        raise ValueError(str(path) + ': report root must be an object')
    code = document.get('status_code', 0)
    if type(code) is not int or code not in STATUS_LABELS:
        raise ValueError(str(path) + ': status_code must be 0 (OPEN) or 1 (FIXED)')
    return document, code


#ai: replace only the top-level status line, preserving private diagnostics and report formatting.
def _with_status(text, code):
    matches = list(STATUS_LINE.finditer(text))
    if len(matches) > 1:
        raise ValueError('report has duplicate top-level status_code lines')
    if matches:
        match = matches[0]
        line = match.group(1) + str(code) + match.group(2)
        return text[:match.start()] + line + text[match.end():]
    opening = re.match(r'\{\r?\n', text)
    if not opening:
        raise ValueError('report must start with a JSON object followed by a newline')
    return text[:opening.end()] + '  "status_code": ' + str(code) + ',\n' + text[opening.end():]


#ai: atomic replacement keeps a partial status update from corrupting a local report.
def _write_status(path, code):
    text = path.read_text(encoding='utf-8')
    updated = _with_status(text, code)
    if updated == text:
        return False
    mode = path.stat().st_mode & 0o777
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', suffix='.tmp',
                                               dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='') as stream:
            stream.write(updated)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return True


#ai: status initialization adds OPEN only to reports without an explicit author decision.
def initialize(directory):
    paths = sorted(directory.glob('report-*.json'))
    loaded = [(path, *read_report(path)) for path in paths]
    changed = 0
    for path, document, code in loaded:
        if 'status_code' not in document:
            changed += int(_write_status(path, code))
    return changed, len(paths)


#ai: explicit status updates accept only the documented binary lifecycle.
def set_status(path, value):
    name = str(value).upper()
    if name not in STATUS_CODES:
        raise ValueError('status must be 0 (OPEN) or 1 (FIXED)')
    document, previous = read_report(path)
    code = STATUS_CODES[name]
    if previous != code or 'status_code' not in document:
        _write_status(path, code)
    return previous, code


#ai: private report paths are restricted to one report filename below the chosen directory.
def _report_path(directory, name):
    if Path(name).name != name or not name.startswith('report-') or not name.endswith('.json'):
        raise ValueError('report must be a report-*.json filename inside bugs/')
    root = directory.resolve()
    path = (directory / name).resolve()
    if path.parent != root or not path.is_file():
        raise ValueError('report does not exist directly inside the report directory')
    return path


#ai: load the bounded report store once so list/summary share the same validation.
def load_reports(directory):
    return [(path, *read_report(path))
            for path in sorted(directory.glob('report-*.json'))]


#ai: feature requests are currently title-tagged; keep the classifier explicit.
def is_feature(document):
    return str(document.get('title', '')).strip().lower().startswith('[feature request]')


#ai: list filters never rewrite or drop reports; they only select for display.
def filter_reports(rows, status=None, severity=None, type_name=None, feature=None):
    selected = []
    for row in rows:
        path, document, code = row
        if status is not None and code != status:
            continue
        if severity and str(document.get('severity', '')).lower() != severity.lower():
            continue
        if type_name and str(document.get('type', '')).lower() != type_name.lower():
            continue
        if feature is not None and is_feature(document) != feature:
            continue
        selected.append(row)
    return selected


#ai: summary is read-only and bounded to the local report directory.
def summarize(directory):
    rows = load_reports(directory)
    open_count = 0
    fixed_count = 0
    features = 0
    open_features = 0
    by_severity = {}
    by_type = {}
    open_by_severity = {}
    open_by_type = {}
    for _, document, code in rows:
        severity = str(document.get('severity', '') or 'Unknown')
        report_type = str(document.get('type', '') or 'Unknown')
        feature = is_feature(document)
        by_severity[severity] = by_severity.get(severity, 0) + 1
        by_type[report_type] = by_type.get(report_type, 0) + 1
        features += int(feature)
        if code == 0:
            open_count += 1
            open_features += int(feature)
            open_by_severity[severity] = open_by_severity.get(severity, 0) + 1
            open_by_type[report_type] = open_by_type.get(report_type, 0) + 1
        else:
            fixed_count += 1
    return {
        'total': len(rows),
        'open': open_count,
        'fixed': fixed_count,
        'feature_requests': features,
        'open_feature_requests': open_features,
        'by_severity': dict(sorted(by_severity.items())),
        'open_by_severity': dict(sorted(open_by_severity.items())),
        'by_type': dict(sorted(by_type.items())),
        'open_by_type': dict(sorted(open_by_type.items())),
    }


#ai: status filters accept the same names and numbers as explicit set.
def _status_value(value):
    name = str(value).upper()
    if name not in STATUS_CODES:
        raise argparse.ArgumentTypeError('status must be 0 (OPEN) or 1 (FIXED)')
    return STATUS_CODES[name]


#ai: compact deterministic count line for human-readable summaries.
def _format_counts(counts):
    return ', '.join(name + '=' + str(value) for name, value in counts.items()) or 'none'


#ai: local CLI lists, initializes, summarizes, and explicitly transitions private reports.
def main():
    parser = argparse.ArgumentParser(description='List, summarize and update local bug report statuses.')
    parser.add_argument('--directory', type=Path, default=Path('bugs'))
    commands = parser.add_subparsers(dest='command', required=True)
    listing = commands.add_parser('list', help='list reports and their status codes')
    listing.add_argument('--status', type=_status_value, metavar='OPEN|FIXED',
                         help='only reports with this status')
    listing.add_argument('--severity', metavar='SEVERITY',
                         help='only reports with this severity (case-insensitive)')
    listing.add_argument('--type', dest='type_name', metavar='TYPE',
                         help='only reports with this type (case-insensitive)')
    feature = listing.add_mutually_exclusive_group()
    feature.add_argument('--feature', dest='feature', action='store_true',
                         help='only [feature request] reports')
    feature.add_argument('--no-feature', dest='feature', action='store_false',
                         help='exclude [feature request] reports')
    listing.set_defaults(feature=None)
    summary = commands.add_parser('summary', help='count reports by status, severity and type')
    summary.add_argument('--json', action='store_true', help='emit machine-readable JSON')
    commands.add_parser('initialize', help='add OPEN to reports missing a status code')
    update = commands.add_parser('set', help='explicitly set one report to OPEN or FIXED')
    update.add_argument('report')
    update.add_argument('status', choices=('0', '1', 'OPEN', 'FIXED', 'open', 'fixed'))
    args = parser.parse_args()
    if not args.directory.is_dir():
        raise SystemExit('report directory does not exist: ' + str(args.directory))
    if args.command == 'initialize':
        changed, total = initialize(args.directory)
        print('BUG_STATUS: initialized', changed, 'of', total, 'reports to OPEN where missing')
    elif args.command == 'list':
        rows = load_reports(args.directory)
        selected = filter_reports(rows, status=args.status, severity=args.severity,
                                  type_name=args.type_name, feature=args.feature)
        for path, document, code in selected:
            print(path.name + ': ' + str(code) + ' ' + STATUS_LABELS[code]
                  + ' — ' + str(document.get('title', '(untitled)')))
        if len(selected) == len(rows):
            print('BUG_STATUS:', len(rows), 'reports')
        else:
            print('BUG_STATUS:', len(selected), 'of', len(rows), 'reports')
    elif args.command == 'summary':
        result = summarize(args.directory)
        if args.json:
            print(json.dumps(result, indent=2, sort_keys=True))
        else:
            print('BUG_STATUS: total=' + str(result['total'])
                  + ' open=' + str(result['open'])
                  + ' fixed=' + str(result['fixed'])
                  + ' feature_requests=' + str(result['feature_requests'])
                  + ' open_feature_requests=' + str(result['open_feature_requests']))
            print('  severity(all): ' + _format_counts(result['by_severity']))
            print('  severity(open): ' + _format_counts(result['open_by_severity']))
            print('  type(all): ' + _format_counts(result['by_type']))
            print('  type(open): ' + _format_counts(result['open_by_type']))
    else:
        path = _report_path(args.directory, args.report)
        previous, code = set_status(path, args.status)
        print(path.name + ': ' + STATUS_LABELS[previous] + ' -> ' + STATUS_LABELS[code])


if __name__ == '__main__':
    main()
