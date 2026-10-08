#!/usr/bin/env python3
"""Scoped systemd integration. SPDX-License-Identifier: LGPL-2.1-or-later."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile

BASE = Path(__file__).resolve().parent.parent
TARGET = Path('/opt/goodix-538d-existing')
DROPIN = Path('/etc/systemd/system/fprintd.service.d/90-goodix-538d-existing.conf')
SERVICE = 'fprintd.service'
LIBRARY = 'libfprint-2.so.2.0.0'
ENV = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'HOME': '/root', 'LANG': 'C.UTF-8'}
PRODUCT = 'goodix-538d-linux'
DROPIN_TEXT = '''# Managed by goodix-538d-linux; see docs/INSTALL.md
[Service]
Environment=LD_LIBRARY_PATH=/opt/goodix-538d-existing/lib
Environment=GOODIX_538D_PSK_FILE=/opt/goodix-538d-existing/private/existing.psk
UnsetEnvironment=GOODIX53XD_DUMP GOODIX_538D_TRANSPORT_ONLY G_MESSAGES_DEBUG G_USB_DEBUG LIBUSB_DEBUG
UMask=0077
LimitCORE=0
ReadOnlyPaths=/opt/goodix-538d-existing
'''


def run(*args, check=True):
    result = subprocess.run([str(x) for x in args], env=ENV, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError(f'{args[0]} failed: {result.stderr.strip() or result.stdout.strip()}')
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact_paths():
    return {
        f'lib/{LIBRARY}': BASE / '.work/stage/opt/goodix-538d-existing/lib' / LIBRARY,
        'bin/probe': BASE / '.work/stage/probe',
        'bin/cancel-probe': BASE / '.work/stage/cancel-probe',
        'share/LICENSE': BASE / 'LICENSE',
        'share/SOURCE.json': BASE / 'SOURCE.json',
        'share/integration.patch': BASE / 'patches/0001-existing-key-integration.patch',
    }


def regular(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise RuntimeError(f'Expected a regular, single-link file: {path}')
    return info


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')
    path.chmod(0o600)


def manifest(create=False):
    paths = artifact_paths()
    for path in paths.values():
        regular(path)
    current = {name: digest(path) for name, path in paths.items()}
    file = BASE / '.work/tested.json'
    if create:
        write_json(file, current)
    elif json.loads(file.read_text()) != current:
        raise RuntimeError('Built artifacts changed. Run scripts/build.sh again.')
    return current


def read_key(path):
    if not path.is_absolute():
        raise RuntimeError('The key file path must be absolute.')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        info = os.fstat(fd)
        allowed = {0, os.geteuid(), int(os.environ.get('SUDO_UID', os.geteuid()))}
        if (not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600
                or info.st_nlink != 1 or info.st_size != 32 or info.st_uid not in allowed):
            raise RuntimeError('Key must be an owned, single-link, mode 0600 regular file of 32 raw bytes.')
        key = os.read(fd, 33)
        if len(key) != 32 or not any(key):
            raise RuntimeError('Key must contain exactly 32 raw, nonzero bytes.')
        return key
    finally:
        os.close(fd)


def owned():
    if TARGET.is_symlink() or TARGET.stat().st_uid != 0:
        raise RuntimeError('Installation directory is not root-owned or is a symlink.')
    marker = TARGET / 'installation.json'
    info = regular(marker)
    if info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o600:
        raise RuntimeError('Invalid installation marker permissions.')
    data = json.loads(marker.read_text())
    if data.get('product') != PRODUCT:
        raise RuntimeError('Unrecognized installation; refusing to change it.')
    for name in (f'lib/{LIBRARY}', 'bin/probe', 'bin/cancel-probe'):
        path = TARGET / name
        info = regular(path)
        if info.st_uid != 0 or info.st_mode & 0o022 or digest(path) != data['artifacts'][name]:
            raise RuntimeError('Installed artifacts changed; inspect installation before proceeding.')
    return data


def check_service():
    state = run('systemctl', 'is-enabled', SERVICE, check=False).stdout.strip()
    if state.startswith('masked'):
        raise RuntimeError('fprintd is already masked. Resolve that existing configuration first.')
    if run('systemctl', 'show', SERVICE, '-p', 'LoadState', '--value').stdout.strip() != 'loaded':
        raise RuntimeError('Install the distribution fprintd service first.')
    user = run('systemctl', 'show', SERVICE, '-p', 'User', '--value').stdout.strip()
    dynamic = run('systemctl', 'show', SERVICE, '-p', 'DynamicUser', '--value').stdout.strip()
    if user not in ('', 'root') or dynamic == 'yes':
        raise RuntimeError('This installer requires a root-run fprintd service.')


def check_device():
    if run('lsusb', '-d', '27c6:538d', check=False).returncode:
        raise RuntimeError('USB device 27c6:538d was not found.')


@contextlib.contextmanager
def exclusive_device():
    check_service()
    active = run('systemctl', 'is-active', '--quiet', SERVICE, check=False).returncode == 0
    masked = False
    try:
        run('systemctl', 'mask', '--runtime', SERVICE)
        masked = True
        run('systemctl', 'stop', SERVICE)
        yield
    finally:
        if masked:
            run('systemctl', 'unmask', '--runtime', SERVICE)
        if active:
            run('systemctl', 'start', SERVICE)


def probe(kind):
    with tempfile.TemporaryDirectory(prefix='goodix538d-', dir='/run') as state:
        args = ['timeout', '--kill-after=3s', '40s', 'env', '-i',
                'PATH=' + ENV['PATH'], 'HOME=/root', 'STATE_DIRECTORY=' + state,
                'LD_LIBRARY_PATH=' + str(TARGET / 'lib'),
                'GOODIX_538D_PSK_FILE=' + str(TARGET / 'private/existing.psk')]
        if kind == 'transport':
            args += ['GOODIX_538D_TRANSPORT_ONLY=1', str(TARGET / 'bin/probe')]
        elif kind == 'runtime':
            args += [str(TARGET / 'bin/probe'), '--twice']
        else:
            args += [str(TARGET / 'bin/cancel-probe')]
        # Probes never print credentials; retain their ordinary progress/errors.
        subprocess.run(args, env=ENV, check=True)


def atomic_copy(source, destination, mode):
    fd, name = tempfile.mkstemp(prefix='.next-', dir=destination.parent)
    try:
        with os.fdopen(fd, 'wb') as out, source.open('rb') as src:
            shutil.copyfileobj(src, out)
        os.chmod(name, mode)
        os.replace(name, destination)
    finally:
        Path(name).unlink(missing_ok=True)


def stage(key_path):
    artifacts = manifest()
    check_service()
    check_device()
    if TARGET.exists() or TARGET.is_symlink() or DROPIN.exists() or DROPIN.is_symlink():
        raise RuntimeError('An installation/override already exists. Use update for this package; never overwrite another setup.')
    key = read_key(key_path)
    TARGET.parent.mkdir(mode=0o755, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.goodix-stage-', dir=TARGET.parent) as temp:
        root = Path(temp) / 'install'
        root.mkdir(mode=0o755)
        for name, src in artifact_paths().items():
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
            atomic_copy(src, dest, 0o755 if name.startswith('bin/') else 0o644)
        (root / 'lib/libfprint-2.so.2').symlink_to(LIBRARY)
        (root / 'private').mkdir(mode=0o700)
        (root / 'private/existing.psk').write_bytes(key)
        (root / 'private/existing.psk').chmod(0o600)
        write_json(root / 'installation.json', {'product': PRODUCT, 'artifacts': artifacts,
                   'transport_passed': False, 'version': (BASE / 'VERSION').read_text().strip()})
        root.rename(TARGET)
    print('Staged privately. fprintd/PAM unchanged. Next: sudo python3 scripts/manage.py probe')


def transport():
    data = owned()
    check_device()
    data['transport_passed'] = False
    write_json(TARGET / 'installation.json', data)
    with exclusive_device():
        probe('transport')
    data['transport_passed'] = True
    write_json(TARGET / 'installation.json', data)
    print('Existing-key transport passed. Follow the Windows checkpoint in docs/INSTALL.md.')


def check_linkage():
    command = run('systemctl', 'show', SERVICE, '-p', 'ExecStart', '--value').stdout
    match = re.search(r'path=([^ ;]+)', command)
    if not match or Path(match[1]).name != 'fprintd':
        raise RuntimeError('Cannot determine a direct fprintd executable from ExecStart.')
    result = run('env', 'LD_LIBRARY_PATH=' + str(TARGET / 'lib'), 'ldd', '-r', match[1])
    report = result.stdout + result.stderr
    if 'not found' in report or 'undefined symbol' in report:
        raise RuntimeError('Installed fprintd is not compatible with this library.')


def start_verified():
    run('systemctl', 'daemon-reload')
    run('systemctl', 'restart', SERVICE)
    pid = run('systemctl', 'show', SERVICE, '-p', 'MainPID', '--value').stdout.strip()
    if not pid.isdigit() or pid == '0':
        raise RuntimeError('fprintd did not stay running.')
    if str(TARGET / 'lib' / LIBRARY) not in Path(f'/proc/{pid}/maps').read_text():
        raise RuntimeError('fprintd did not load the scoped driver.')


def require_our_dropin():
    if DROPIN.is_symlink() or DROPIN.read_text() != DROPIN_TEXT:
        raise RuntimeError('Override is not this package\'s unmodified configuration; inspect it manually.')


def activate():
    data = owned()
    if not data.get('transport_passed'):
        raise RuntimeError('Run the transport probe successfully before activation.')
    if DROPIN.exists() or DROPIN.is_symlink():
        raise RuntimeError('Override exists. Use update or deactivate; refusing to overwrite.')
    unit = run('systemctl', 'cat', SERVICE).stdout
    if any(x in unit for x in ('LD_LIBRARY_PATH', 'GOODIX_', 'GOODIX53XD_')):
        raise RuntimeError('Another driver/debug override exists; resolve it before activation.')
    check_device()
    with exclusive_device():
        probe('runtime')
        probe('cancel')
        check_linkage()
    try:
        DROPIN.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        DROPIN.write_text(DROPIN_TEXT)
        DROPIN.chmod(0o644)
        start_verified()
    except BaseException:
        run('systemctl', 'stop', SERVICE, check=False)
        DROPIN.unlink(missing_ok=True)
        run('systemctl', 'daemon-reload')
        run('systemctl', 'start', SERVICE, check=False)
        raise
    print('fprintd loaded the driver. PAM unchanged. Enroll and verify before enabling login.')


def deactivate():
    check_service()
    if not DROPIN.exists() and not DROPIN.is_symlink():
        print('No package override is active. Staged files and fingerprints retained.')
        return
    require_our_dropin()
    run('systemctl', 'stop', SERVICE)
    DROPIN.unlink()
    run('systemctl', 'daemon-reload')
    run('systemctl', 'start', SERVICE)
    print('Distribution service restored. Key, staged files, prints and PAM retained.')


def update():
    data = owned()
    require_our_dropin()
    artifacts = manifest()
    check_device()
    paths = artifact_paths()
    with tempfile.TemporaryDirectory(prefix='.goodix-backup-', dir=TARGET.parent) as temp:
        backup = Path(temp)
        for name in paths:
            dest = backup / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(TARGET / name, dest)
        try:
            with exclusive_device():
                for name, source in paths.items():
                    atomic_copy(source, TARGET / name, 0o755 if name.startswith('bin/') else 0o644)
                probe('transport')
                probe('runtime')
                probe('cancel')
                check_linkage()
            start_verified()
            write_json(TARGET / 'installation.json', {**data, 'artifacts': artifacts,
                       'transport_passed': True, 'version': (BASE / 'VERSION').read_text().strip()})
        except BaseException:
            with exclusive_device():
                for name in paths:
                    atomic_copy(backup / name, TARGET / name, 0o755 if name.startswith('bin/') else 0o644)
                write_json(TARGET / 'installation.json', data)
            run('systemctl', 'restart', SERVICE, check=False)
            print('Update failed; previous artifacts restored.', file=sys.stderr)
            raise
    print('Updated driver passed transport, reopen, cancellation and fprintd mapping checks.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['manifest', 'stage', 'probe', 'activate', 'update', 'deactivate'])
    parser.add_argument('--key', type=Path, help='Absolute path only; never supply key bytes.')
    args = parser.parse_args()
    if args.action == 'manifest':
        manifest(create=True)
        return
    if os.geteuid() != 0:
        parser.error('Run system integration with sudo python3 scripts/manage.py ...')
    if platform.machine() != 'x86_64' or not Path('/run/systemd/system').is_dir():
        parser.error('This installer targets x86_64 Linux with systemd.')
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    # Prevent this short-lived installer from dumping key bytes too.
    import ctypes
    if ctypes.CDLL(None, use_errno=True).prctl(4, 0, 0, 0, 0) != 0:  # PR_SET_DUMPABLE
        raise RuntimeError('Cannot disable process dumps.')
    os.umask(0o077)
    import fcntl
    lock_fd = os.open('/run/lock/goodix-538d-linux.lock',
                      os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise RuntimeError('Another integration operation is already running.') from None
    if args.action == 'stage':
        if args.key is None:
            parser.error('stage requires --key /absolute/private/path')
        stage(args.key)
    elif args.key is not None:
        parser.error('--key is only accepted for stage')
    else:
        {'probe': transport, 'activate': activate, 'update': update, 'deactivate': deactivate}[args.action]()


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
