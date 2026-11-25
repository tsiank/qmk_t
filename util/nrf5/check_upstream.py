#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only inventory/pin check. Never fetch, merge, reset or edit the tree."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def shared_changes(root, base):
    # Existing upstream files: every modification/deletion needs an inventory
    # entry. New files need one only inside shared firmware infrastructure.
    changes = set()
    for line in git(root, 'diff', '--no-renames', '--name-status', base, '--').splitlines():
        status, path = line.split('\t', 1)
        if status != 'A' or path.startswith(('quantum/', 'builddefs/', 'drivers/led/', 'drivers/oled/')):
            changes.add(path)
    for path in git(root, 'ls-files', '--others', '--exclude-standard').splitlines():
        if path.startswith(('quantum/', 'builddefs/', 'drivers/led/', 'drivers/oled/')):
            changes.add(path)
    return changes


def inspect(root, manifest, candidate=None):
    base = manifest['upstream']['commit']
    git(root, 'cat-file', '-e', base + '^{commit}')
    registered = set(manifest['core_patches']) | set(manifest['submodule_overrides'])
    changed = shared_changes(root, base)
    errors = ['Unregistered upstream change: ' + p for p in sorted(changed - registered)]
    for path, expected in manifest['submodule_overrides'].items():
        entry = git(root, 'ls-tree', 'HEAD', '--', path).split()
        if len(entry) < 3 or entry[0] != '160000' or entry[2] != expected:
            errors.append('Submodule gitlink differs from the recorded pin: ' + path)
    sdk = manifest['sdk']
    sdk_path = root / sdk['path']
    if not (sdk_path / '.git').exists():
        errors.append('Initialize the SDK submodule: ' + sdk['path'])
    elif git(sdk_path, 'rev-parse', 'HEAD') != sdk['commit'] or git(sdk_path, 'status', '--porcelain', '--untracked-files=no'):
        errors.append('SDK checkout differs from the recorded clean revision: ' + sdk['path'])
    for path in manifest['portable_sources']:
        if re.search(r'\b(?:PROTOCOL_NRF5|NRF5_\w*|nrf5_\w*|nrf_gpio\w*)\b', (root / path).read_text()):
            errors.append('Platform dependency leaked into portable source: ' + path)
    report = {'base': base, 'registered_changes': sorted(changed & registered),
              'obsolete_inventory_entries': sorted(set(manifest['core_patches']) - changed), 'errors': errors}
    if candidate:
        # Diagnostic only: overlap signals manual review, not a merge attempt.
        candidate_commit = git(root, 'rev-parse', '--verify', '--end-of-options', candidate + '^{commit}')
        upstream_changed = set(git(root, 'diff', '--name-only', base, candidate_commit, '--').splitlines())
        report['candidate'] = candidate_commit
        report['review_overlap'] = sorted(upstream_changed & registered)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', help='Already-fetched upstream ref to inspect without merging')
    args = parser.parse_args()
    try:
        manifest = json.loads((ROOT / 'platforms/nrf5/upstream.json').read_text())
        report = inspect(ROOT, manifest, args.candidate)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Upstream check failed: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return bool(report['errors'])


if __name__ == '__main__':
    sys.exit(main())
