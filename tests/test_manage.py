"""No root, USB, service or private-key access. SPDX-License-Identifier: LGPL-2.1-or-later."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('manage', Path(__file__).parents[1] / 'scripts/manage.py')
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.target = self.root / 'install'
        self.dropin = self.root / 'systemd/driver.conf'
        self.target.mkdir()
        for name, value in [('TARGET', self.target), ('DROPIN', self.dropin)]:
            p = patch.object(m, name, value)
            p.start()
            self.addCleanup(p.stop)

    def key(self):
        path = self.root / 'synthetic.key'
        path.write_bytes(bytes(range(1, 33)))
        path.chmod(0o600)
        return path

    def test_valid_key_and_invalid_permissions_size_zero_links(self):
        key = self.key()
        self.assertEqual(m.read_key(key), bytes(range(1, 33)))
        key.chmod(0o644)
        with self.assertRaises(RuntimeError): m.read_key(key)
        key.chmod(0o600)
        link = self.root / 'symlink'
        link.symlink_to(key)
        with self.assertRaises(OSError): m.read_key(link)
        hard = self.root / 'hardlink'
        os.link(key, hard)
        with self.assertRaises(RuntimeError): m.read_key(key)
        hard.unlink()
        for content in [b'\0' * 32, b'x' * 31, b'x' * 33]:
            key.write_bytes(content)
            with self.assertRaises(RuntimeError): m.read_key(key)
        with self.assertRaises(RuntimeError): m.read_key(Path('relative.key'))

    def test_manifest_detects_changed_or_extra_artifact(self):
        work = self.root / '.work'
        work.mkdir()
        source = self.root / 'synthetic-library'
        source.write_text('first')
        with patch.object(m, 'BASE', self.root), patch.object(m, 'artifact_paths', return_value={'lib/a': source}):
            m.manifest(create=True)
            m.manifest()
            source.write_text('changed')
            with self.assertRaises(RuntimeError): m.manifest()

    def test_stage_copies_key_privately_without_activating_service(self):
        self.target.rmdir()
        source = self.root / 'library'
        source.write_bytes(b'synthetic-library')
        (self.root / 'VERSION').write_text('0.1.0\n')
        key = self.key()
        files = {f'lib/{m.LIBRARY}': source}
        hashes = {name: m.digest(path) for name, path in files.items()}
        with patch.object(m, 'BASE', self.root), patch.object(m, 'manifest', return_value=hashes), \
             patch.object(m, 'artifact_paths', return_value=files), \
             patch.object(m, 'check_service'), patch.object(m, 'check_device'), patch.object(m, 'run') as run:
            m.stage(key)
        run.assert_not_called()
        installed = self.target / 'private/existing.psk'
        self.assertEqual(installed.read_bytes(), key.read_bytes())
        self.assertEqual(installed.stat().st_mode & 0o777, 0o600)
        self.assertEqual(installed.parent.stat().st_mode & 0o777, 0o700)
        self.assertFalse(self.dropin.exists())
        self.assertFalse(json.loads((self.target / 'installation.json').read_text())['transport_passed'])

    def fake_run(self, *args, **kwargs):
        self.commands.append(args)
        return subprocess.CompletedProcess(args, 0, '', '')

    def test_failure_unmasks_and_restores_active_service(self):
        self.commands = []
        with patch.object(m, 'check_service'), patch.object(m, 'run', side_effect=self.fake_run):
            with self.assertRaisesRegex(RuntimeError, 'probe failed'):
                with m.exclusive_device():
                    raise RuntimeError('probe failed')
        self.assertIn(('systemctl', 'mask', '--runtime', m.SERVICE), self.commands)
        self.assertIn(('systemctl', 'unmask', '--runtime', m.SERVICE), self.commands)
        self.assertEqual(self.commands[-1], ('systemctl', 'start', m.SERVICE))

    def test_existing_mask_is_never_removed(self):
        with patch.object(m, 'run', return_value=subprocess.CompletedProcess([], 1, 'masked\n', '')) as run:
            with self.assertRaisesRegex(RuntimeError, 'already masked'):
                with m.exclusive_device(): self.fail('Must not enter')
        self.assertEqual(run.call_count, 1)

    def test_activation_does_not_write_override_when_probe_fails(self):
        with patch.object(m, 'owned', return_value={'transport_passed': True}), \
             patch.object(m, 'run', return_value=subprocess.CompletedProcess([], 0, '', '')), \
             patch.object(m, 'check_device'), patch.object(m, 'exclusive_device'), \
             patch.object(m, 'probe', side_effect=RuntimeError('wrong key')):
            with self.assertRaisesRegex(RuntimeError, 'wrong key'): m.activate()
        self.assertFalse(self.dropin.exists())

    def test_activation_rolls_back_failed_daemon_validation(self):
        self.commands = []
        with patch.object(m, 'owned', return_value={'transport_passed': True}), \
             patch.object(m, 'run', side_effect=self.fake_run), patch.object(m, 'check_device'), \
             patch.object(m, 'exclusive_device'), patch.object(m, 'probe'), \
             patch.object(m, 'check_linkage'), patch.object(m, 'start_verified', side_effect=RuntimeError('mapping')):
            with self.assertRaisesRegex(RuntimeError, 'mapping'): m.activate()
        self.assertFalse(self.dropin.exists())
        self.assertIn(('systemctl', 'daemon-reload'), self.commands)

    def test_unknown_override_is_preserved(self):
        self.dropin.parent.mkdir()
        self.dropin.write_text('another integration')
        with patch.object(m, 'check_service'), patch.object(m, 'run') as run:
            with self.assertRaises(RuntimeError): m.deactivate()
        run.assert_not_called()
        self.assertEqual(self.dropin.read_text(), 'another integration')

    def test_update_restores_old_bytes_after_hardware_failure(self):
        old = self.target / 'lib/example'
        old.parent.mkdir()
        old.write_bytes(b'old-library')
        new = self.root / 'new-library'
        new.write_bytes(b'new-library')
        data = {'product': m.PRODUCT, 'version': 'old'}
        m.write_json(self.target / 'installation.json', data)
        with patch.object(m, 'owned', return_value=data), patch.object(m, 'require_our_dropin'), \
             patch.object(m, 'manifest', return_value={'lib/example': 'new'}), \
             patch.object(m, 'check_device'), patch.object(m, 'exclusive_device'), \
             patch.object(m, 'artifact_paths', return_value={'lib/example': new}), \
             patch.object(m, 'run'), patch.object(m, 'probe', side_effect=RuntimeError('timeout')):
            with self.assertRaisesRegex(RuntimeError, 'timeout'): m.update()
        self.assertEqual(old.read_bytes(), b'old-library')
        self.assertEqual(json.loads((self.target / 'installation.json').read_text()), data)

    def test_linkage_checks_stderr_for_missing_symbols(self):
        results = [subprocess.CompletedProcess([], 0, '{ path=/usr/libexec/fprintd ; }', ''),
                   subprocess.CompletedProcess([], 0, '', 'undefined symbol: test')]
        with patch.object(m, 'run', side_effect=results):
            with self.assertRaisesRegex(RuntimeError, 'not compatible'): m.check_linkage()


if __name__ == '__main__':
    unittest.main()
