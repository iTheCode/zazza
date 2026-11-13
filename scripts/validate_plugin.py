#!/usr/bin/env python3
"""Validate the built VST3 with pinned pluginval binaries, including GUI tests."""
import argparse
import hashlib
from pathlib import Path
import platform
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = '1.0.4'
RELEASES = {
    'Darwin': ('pluginval_macOS.zip', '3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f'),
    'Windows': ('pluginval_Windows.zip', 'c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab'),
}


def verify_checksum(archive, expected):
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise ValueError('pluginval archive checksum mismatch: ' + str(archive))


def install(system, cache):
    filename, checksum = RELEASES[system]
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / filename
    if not archive.exists():
        url = f'https://github.com/Tracktion/pluginval/releases/download/v{VERSION}/{filename}'
        # A partial download never becomes the reusable cache entry.
        with tempfile.NamedTemporaryFile(dir=cache, suffix='.download', delete=False) as download:
            temporary = Path(download.name)
            with urllib.request.urlopen(url, timeout=60) as response:
                while chunk := response.read(1024 * 1024):
                    download.write(chunk)
        verify_checksum(temporary, checksum)
        temporary.replace(archive)
    verify_checksum(archive, checksum)
    destination = Path(tempfile.mkdtemp(prefix='pluginval-', dir=cache))
    if system == 'Darwin':
        subprocess.run(['ditto', '-x', '-k', str(archive), str(destination)], check=True)
        return destination / 'pluginval.app/Contents/MacOS/pluginval'
    with zipfile.ZipFile(archive) as zipped:
        zipped.extractall(destination)
    return destination / 'pluginval.exe'


def validation_command(executable, plugin, system, arch):
    if system not in RELEASES or arch not in ('arm64', 'x86_64') or (system == 'Windows' and arch != 'x86_64'):
        raise ValueError('Supported targets: macOS arm64/x86_64 and Windows x86_64')
    prefix = ['arch', '-' + arch] if system == 'Darwin' else []
    return prefix + [str(executable), '--strictness-level', '5', '--timeout-ms', '120000', '--validate', str(plugin)]


def validate(plugin, arch, log, cache, system=None):
    system = system or platform.system()
    if system not in RELEASES:
        raise ValueError('pluginval packaging is supported on macOS and Windows')
    plugin = plugin.resolve()
    if not plugin.exists():
        raise FileNotFoundError(plugin)
    executable = install(system, cache.resolve())
    command = validation_command(executable, plugin, system, arch)
    log.parent.mkdir(parents=True, exist_ok=True)
    print('+ ' + ' '.join(command), flush=True)
    with log.open('w', encoding='utf-8') as output:
        try:
            subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=600)
        finally:
            output.flush()
            print(log.read_text(encoding='utf-8', errors='replace'), flush=True)
    # Require both a successful exit and the validator's final success marker.
    if 'SUCCESS' not in log.read_text(encoding='utf-8', errors='replace').splitlines():
        raise RuntimeError('pluginval exited without confirming SUCCESS; see ' + str(log))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), required=True)
    parser.add_argument('--log', type=Path, default=ROOT / 'validation/pluginval.log')
    parser.add_argument('--cache', type=Path, default=ROOT / '.tools/pluginval')
    args = parser.parse_args()
    validate(args.plugin, args.arch, args.log, args.cache)
