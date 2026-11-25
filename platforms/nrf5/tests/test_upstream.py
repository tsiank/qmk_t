# SPDX-License-Identifier: GPL-2.0-or-later
"""Upgrade guardrails: inventory coverage, portable boundaries and pin schema."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('nrf5_upstream', ROOT / 'util/nrf5/check_upstream.py')
upstream = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upstream)


class UpstreamTests(unittest.TestCase):
    def test_current_checkout_has_reviewed_core_changes_and_clean_sdk_pin(self):
        manifest = json.loads((ROOT / 'platforms/nrf5/upstream.json').read_text())
        report = upstream.inspect(ROOT, manifest)
        self.assertEqual(report['errors'], [])
        self.assertEqual(report['obsolete_inventory_entries'], [])

    def test_inventory_detects_new_shared_files_and_preserves_custom_platform_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return upstream.git(root, *args)
            git('init', '-q')
            (root / 'quantum').mkdir()
            (root / 'quantum/core.c').write_text('int core;\n')
            git('add', '.')
            git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'baseline')
            base = git('rev-parse', 'HEAD')
            (root / 'quantum/core.c').write_text('int changed;\n')
            (root / 'quantum/new.c').write_text('int new_core;\n')
            (root / 'platforms/nrf5').mkdir(parents=True)
            (root / 'platforms/nrf5/extension.c').write_text('int extension;\n')
            self.assertEqual(upstream.shared_changes(root, base), {'quantum/core.c', 'quantum/new.c'})
            # Staged additions, like a PR commit, must not hide behind the
            # rule that allows new files in the custom platform directory.
            git('add', '.')
            self.assertEqual(upstream.shared_changes(root, base), {'quantum/core.c', 'quantum/new.c'})

    def test_pin_schema_accepts_avr_and_nordic_pins_without_ambiguous_oneof(self):
        import jsonschema
        schema = json.loads((ROOT / 'data/schemas/definitions.jsonschema').read_text())['mcu_pin']
        validator = jsonschema.Draft202012Validator(schema)
        for pin in ('F6', 'B0', 'A10', 'GP5', 'LINE_PIN12', 'NO_PIN', '0', '31', '47', 31, None):
            with self.subTest(pin=pin):
                validator.validate(pin)
        for pin in ('F6junk', 'garbage7', 'P0.31', '', '123'):
            with self.subTest(pin=pin):
                self.assertFalse(validator.is_valid(pin))
