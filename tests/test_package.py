"""Source archive boundary tests. SPDX-License-Identifier: LGPL-2.1-or-later."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('package', Path(__file__).parents[1] / 'scripts/package.py')
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'VERSION').write_text('0.1.0\n')
        (self.root / 'PACKAGE_FILES').write_text('PACKAGE_FILES\nVERSION\n')

    def test_unlisted_private_material_and_builds_never_enter_archive(self):
        (self.root / 'private').mkdir()
        (self.root / 'private/key.bin').write_bytes(b'synthetic-private-data')
        (self.root / '.work').mkdir()
        (self.root / '.work/library.so').write_bytes(b'synthetic-binary')
        path, _ = p.package(self.root)
        with zipfile.ZipFile(path) as archive:
            self.assertEqual(archive.namelist(), ['goodix-538d-linux/PACKAGE_FILES', 'goodix-538d-linux/VERSION'])

    def test_forbidden_private_path_even_when_allowlisted(self):
        with (self.root / 'PACKAGE_FILES').open('a') as out: out.write('private/key.bin\n')
        with self.assertRaises(ValueError): p.public_files(self.root)

    def test_reject_symlink_source(self):
        (self.root / 'source.py').symlink_to(self.root / 'VERSION')
        with (self.root / 'PACKAGE_FILES').open('a') as out: out.write('source.py\n')
        with self.assertRaises(ValueError): p.public_files(self.root)

    def test_reproducible_archive(self):
        paths = p.package(self.root)
        first = [path.read_bytes() for path in paths]
        self.assertEqual(first, [path.read_bytes() for path in p.package(self.root)])
