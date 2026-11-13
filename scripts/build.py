#!/usr/bin/env python3
"""Build, test, verify and package Zazza on native macOS/Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
JUCE_REVISION = 'dabd5ad13cd869cba41046947fffc8e5ce5cd662'


def run(*args):
    print('+ ' + ' '.join(map(str, args)), flush=True)
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def parse_manifest(text):
    # Steinberg's moduleinfo writer allows trailing commas. Preserve quoted
    # strings (including escaped quotes) while removing only structural commas.
    normalized = re.sub(r'("(?:\\.|[^"\\])*")|,\s*(?=[}\]])', lambda match: match.group(1) or '', text)
    manifest = json.loads(normalized)
    if not re.fullmatch(r'\d+\.\d+\.\d+', manifest['Version']):
        raise ValueError('Unexpected VST3 version')
    return manifest


NOTARY_VARS = ('ASC_KEY_ID', 'ASC_ISSUER_ID', 'ASC_KEY_PATH')


def macos_signing():
    """Developer ID identity from MACOS_SIGN_IDENTITY, or None for ad-hoc sealing.

    Gatekeeper only accepts downloaded bundles that are Developer ID signed and
    notarized, so a real identity requires the App Store Connect API key too.
    """
    identity = os.environ.get('MACOS_SIGN_IDENTITY', '').strip()
    if not identity:
        return None
    if not identity.startswith('Developer ID Application:'):
        raise ValueError('MACOS_SIGN_IDENTITY must be a "Developer ID Application" identity')
    missing = [name for name in NOTARY_VARS if not os.environ.get(name)]
    if missing:
        raise RuntimeError('Notarization needs ' + ', '.join(missing))
    return identity


def sign_macos(bundle, identity):
    if not identity:
        # Ad-hoc sealing for local/PR builds: Gatekeeper blocks these when downloaded.
        run('codesign', '--force', '--sign', '-', bundle)
    else:
        run('codesign', '--force', '--timestamp', '--options', 'runtime', '--sign', identity, bundle)
    run('codesign', '--verify', '--deep', '--strict', '--verbose=2', bundle)


def notarize_macos(bundles, build_dir):
    build_dir.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='notarize-', dir=build_dir))
    for bundle in bundles:
        run('ditto', bundle, stage / 'bundles' / bundle.name)
    submission = stage / 'Zazza-notarize.zip'
    run('ditto', '-c', '-k', '--keepParent', stage / 'bundles', submission)
    run('xcrun', 'notarytool', 'submit', submission, '--wait', '--timeout', '30m',
        '--key', os.environ['ASC_KEY_PATH'], '--key-id', os.environ['ASC_KEY_ID'],
        '--issuer', os.environ['ASC_ISSUER_ID'])
    for bundle in bundles:
        run('xcrun', 'stapler', 'staple', bundle)
        run('xcrun', 'stapler', 'validate', bundle)
    app = next(bundle for bundle in bundles if bundle.suffix == '.app')
    run('spctl', '--assess', '--type', 'execute', '--verbose=2', app)


def build(args):
    system = platform.system()
    if system not in ('Darwin', 'Windows'):
        raise RuntimeError('Plugin packages require native macOS or Windows. Core tests work on Linux.')
    arch = args.arch or ('arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x86_64')
    if system == 'Windows' and arch != 'x86_64':
        raise ValueError('Windows builds target x64.')
    signing = macos_signing() if system == 'Darwin' else None
    cmake = shutil.which(args.cmake)
    if not cmake:
        raise RuntimeError('CMake >= 3.22 is required. Install it or pass --cmake /absolute/path/to/cmake.')
    build_dir = (ROOT / args.build_dir).resolve()
    dist = (ROOT / args.dist_dir).resolve()
    configure = [cmake, '-S', ROOT, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Release',
                 '-DBUILD_TESTING=ON', '-DZAZZA_BUILD_PLUGIN=ON']
    if system == 'Darwin':
        configure += ['-DCMAKE_OSX_ARCHITECTURES=' + arch]
    else:
        configure += ['-G', 'Visual Studio 17 2022', '-A', 'x64']
    if args.juce_source:
        source = Path(args.juce_source).resolve()
        revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
        if revision != JUCE_REVISION:
            raise ValueError('The local JUCE checkout must match the pinned revision: ' + JUCE_REVISION)
        configure += ['-DFETCHCONTENT_SOURCE_DIR_JUCE=' + str(source)]
    run(*configure)
    run(cmake, '--build', build_dir, '--config', 'Release', '--parallel', args.jobs)
    ctest = Path(cmake).with_name('ctest.exe' if system == 'Windows' else 'ctest')
    run(ctest, '--test-dir', build_dir, '-C', 'Release', '--output-on-failure')
    products = build_dir / 'Zazza_artefacts/Release'
    vst = products / 'VST3/Zazza.vst3'
    standalone = products / ('Standalone/Zazza.app' if system == 'Darwin' else 'Standalone/Zazza.exe')
    manifest = parse_manifest((vst / 'Contents/Resources/moduleinfo.json').read_text(encoding='utf-8'))
    version = manifest['Version']
    label = 'macOS-' + arch if system == 'Darwin' else 'windows-x64'
    stem = f'Zazza-{version}-{label}'
    for product in (vst, standalone):
        if not product.exists():
            raise FileNotFoundError(product)
        if system == 'Darwin':
            with (product / 'Contents/Info.plist').open('rb') as stream:
                info = plistlib.load(stream)
            if info['CFBundleShortVersionString'] != version:
                raise RuntimeError('Bundle version mismatch: ' + str(product))
            run('lipo', product / 'Contents/MacOS/Zazza', '-verify_arch', arch)
            sign_macos(product, signing)
    if system == 'Darwin' and signing:
        notarize_macos([vst, standalone], build_dir)
    if args.pluginval:
        run(sys.executable, ROOT / 'scripts/validate_plugin.py', '--plugin', vst, '--arch', arch,
            '--log', ROOT / 'validation' / ('pluginval-' + label + '.log'))
    if not args.package:
        return
    if any((dist / (stem + suffix)).exists() for suffix in ('.zip', '.zip.sha256', '.json')):
        raise FileExistsError('Package already exists. Select a different --dist-dir; existing data is preserved.')
    stage = Path(tempfile.mkdtemp(prefix='package-', dir=build_dir))
    for source, relative in ((vst, 'VST3/Zazza.vst3'), (standalone, 'Standalone/' + standalone.name)):
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if system == 'Darwin':
            run('ditto', source, destination)
        elif source.is_dir():
            shutil.copytree(source, destination)
        else:
            shutil.copy2(source, destination)
    (stage / 'licenses/fonts').mkdir(parents=True)
    for filename in ('LICENSE.txt', 'NOTICE.txt'):
        shutil.copy2(ROOT / 'assets/fonts' / filename, stage / 'licenses/fonts' / filename)
    for filename in ('LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(ROOT / filename, stage / filename)
    shutil.copytree(ROOT / 'docs', stage / 'docs')
    dist.mkdir(parents=True, exist_ok=True)
    archive = dist / (stem + '.zip')
    if system == 'Darwin':
        run('ditto', '-c', '-k', '--sequesterRsrc', stage, archive)
    else:
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as output:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    output.write(file, file.relative_to(stage).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (dist / (stem + '.zip.sha256')).write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
    metadata = {'version': version, 'platform': label, 'commit': os.environ.get('GITHUB_SHA', 'local'),
                'juce': JUCE_REVISION, 'archive': archive.name, 'sha256': digest, 'formats': ['VST3', 'Standalone']}
    metadata['pluginval'] = {'version': '1.0.4', 'strictness': 5, 'gui': True, 'passed': True} if args.pluginval else None
    (dist / (stem + '.json')).write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('Package ready: ' + str(archive))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('arm64', 'x86_64'))
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--build-dir', default='build')
    parser.add_argument('--dist-dir', default='dist')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--juce-source', help='Existing checkout of the exact pinned JUCE revision')
    parser.add_argument('--package', action='store_true')
    parser.add_argument('--pluginval', action='store_true', help='Require pluginval level 5 before packaging (downloads pinned validator)')
    build(parser.parse_args())
