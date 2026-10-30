#!/usr/bin/env python3
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
STATUS_LINE = re.compile(r'(?m)^(  "status_code"[ \t]*:[ \t]*)-?\d+([ \t]*,?)[ \t]*$')


#ai: absent codes remain backward-compatible as OPEN until initialization writes them explicitly.
def read_report(path):
    document = json.loads(path.read_text(encoding='utf-8'))
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


#ai: local CLI lists, initializes, and explicitly transitions private reports.
def main():
    parser = argparse.ArgumentParser(description='List and update local bug report statuses.')
    parser.add_argument('--directory', type=Path, default=Path('bugs'))
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('list', help='list reports and their status codes')
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
        paths = sorted(args.directory.glob('report-*.json'))
        for path in paths:
            document, code = read_report(path)
            print(path.name + ': ' + str(code) + ' ' + STATUS_LABELS[code]
                  + ' — ' + str(document.get('title', '(untitled)')))
        print('BUG_STATUS:', len(paths), 'reports')
    else:
        path = _report_path(args.directory, args.report)
        previous, code = set_status(path, args.status)
        print(path.name + ': ' + STATUS_LABELS[previous] + ' -> ' + STATUS_LABELS[code])


if __name__ == '__main__':
    main()
