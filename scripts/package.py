#!/usr/bin/env python3
"""Allowlisted source archives only. SPDX-License-Identifier: LGPL-2.1-or-later."""
import argparse
import gzip
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile
import zipfile

BASE = Path(__file__).resolve().parent.parent


def public_files(base):
    names = (base / 'PACKAGE_FILES').read_text().splitlines()
    if not names or len(names) != len(set(names)) or 'PACKAGE_FILES' not in names:
        raise ValueError('Invalid or duplicate package allowlist.')
    files = {}
    for name in sorted(names):
        part = PurePosixPath(name)
        if (not name or part.is_absolute() or '..' in part.parts or '\\' in name
                or any(x in part.parts for x in ('.work', '.git', 'private', 'windows-private', 'dist'))
                or part.suffix.lower() in ('.bin', '.psk', '.key', '.pgm', '.raw', '.dll', '.exe', '.log', '.zip')):
            raise ValueError(f'Forbidden release path: {name}')
        path = base / name
        if any(p.is_symlink() for p in [path, *path.parents] if p != base.parent):
            raise ValueError(f'Symlinks are not release sources: {name}')
        info = path.stat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError(f'Not a single-link regular source: {name}')
        content = path.read_bytes()
        text = content.decode('utf-8')
        if '\0' in text or re.search(r'^-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----', text, re.MULTILINE):
            raise ValueError(f'Binary/private material detected: {name}')
        files[name] = (content, 0o755 if info.st_mode & 0o111 else 0o644)
    return files


def package(base):
    files = public_files(base)
    version = (base / 'VERSION').read_text().strip()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?', version):
        raise ValueError('Invalid VERSION')
    dest = base / 'dist'
    if dest.is_symlink():
        raise ValueError('dist must not be a symlink')
    dest.mkdir(exist_ok=True)
    prefix = 'goodix-538d-linux'
    stem = f'{prefix}-{version}'
    zip_path = dest / (stem + '.zip')
    tar_path = dest / (stem + '.tar.gz')
    for path in (zip_path, tar_path, dest / 'SHA256SUMS'):
        if path.is_symlink():
            raise ValueError('Refusing a symlink output')
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, (content, mode) in files.items():
            item = zipfile.ZipInfo(f'{prefix}/{name}', date_time=(1980, 1, 1, 0, 0, 0))
            item.create_system = 3
            item.external_attr = (stat.S_IFREG | mode) << 16
            item.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(item, content)
    with tar_path.open('wb') as out, gzip.GzipFile(fileobj=out, mode='wb', filename='', mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode='w') as archive:
            for name, (content, mode) in files.items():
                item = tarfile.TarInfo(f'{prefix}/{name}')
                item.size, item.mode, item.mtime = len(content), mode, 0
                archive.addfile(item, io.BytesIO(content))
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in (zip_path, tar_path))
    (dest / 'SHA256SUMS').write_text(sums)
    return zip_path, tar_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Validate the allowlist without writing archives')
    args = parser.parse_args()
    if args.check:
        print(f'Validated {len(public_files(BASE))} allowlisted UTF-8 source files.')
    else:
        for path in package(BASE):
            print(path)


if __name__ == '__main__':
    main()
