#!/usr/bin/env python3
"""Create a source ZIP with an explicit allowlist, suitable for a new GitHub repo."""
from pathlib import Path
import argparse
import zipfile

ROOT = Path(__file__).resolve().parent.parent
FILES = ('.gitignore', 'CMakeLists.txt', 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md')
TREES = {'Source': {'.cpp', '.h'}, 'Tests': {'.cpp', '.py'}, 'scripts': {'.py'},
         'assets': {'.gif', '.ttf', '.txt'}, 'docs': {'.md', '.png'}, '.github': {'.yml'}}


def package(destination):
    files = [ROOT / name for name in FILES]
    for folder, suffixes in TREES.items():
        files.extend(p for p in (ROOT / folder).rglob('*') if p.is_file() and p.suffix in suffixes and not p.is_symlink())
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(files):
            archive.write(file, 'Zazza/' + file.relative_to(ROOT).as_posix())
    print(f'Source package: {destination} ({len(files)} files)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/Zazza-source.zip')
    package(parser.parse_args().output.resolve())
