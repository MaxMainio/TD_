#!/usr/bin/env python3
"""Fetch/verify pinned model packages using only this plugin folder and stdlib."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def verify(package, model):
    if package.is_symlink() or not package.is_dir():
        raise ValueError(f'{package}: expected a local package directory')
    expected = {f['path']: f for f in model['files']}
    actual = {p.relative_to(package).as_posix(): p for p in package.rglob('*') if p.is_file()}
    if actual.keys() != expected.keys() or any(p.is_symlink() for p in package.rglob('*')):
        raise ValueError(f'{package}: missing, extra, or linked package files')
    for name, path in actual.items():
        record = expected[name]
        if path.stat().st_size != record['bytes'] or digest(path) != record['sha256']:
            raise ValueError(f'{path}: model checksum mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true', help='verify without downloading')
    parser.add_argument('--from-directory', type=Path, help='copy verified packages from an existing local library')
    parser.add_argument('--directory', type=Path, default=ROOT / 'models', help='model output folder (default: local models/)')
    args = parser.parse_args()
    catalog = json.loads((ROOT / 'models/catalog.json').read_text())
    if not args.verify:
        args.directory.mkdir(parents=True, exist_ok=True)
    for model in catalog['models']:
        dest = args.directory / model['package']
        if dest.exists() or args.verify:
            verify(dest, model)
        else:
            with tempfile.TemporaryDirectory(prefix='.fetch-', dir=args.directory) as temp:
                work = Path(temp)
                package = work / model['package']
                if args.from_directory:
                    source = args.from_directory / model['package']
                    verify(source, model)
                    shutil.copytree(source, package)
                else:
                    archive = work / 'model.zip'
                    with urllib.request.urlopen(model['url'], timeout=60) as response, archive.open('wb') as out:
                        shutil.copyfileobj(response, out)
                    if digest(archive) != model['archive_sha256']:
                        raise ValueError(f'{model["package"]}: archive checksum mismatch')
                    # Extract only the reviewed package members, not arbitrary ZIP paths.
                    with zipfile.ZipFile(archive) as z:
                        for file in model['files']:
                            target = package / file['path']
                            target.parent.mkdir(parents=True, exist_ok=True)
                            with z.open(model['package'] + '/' + file['path']) as src, target.open('wb') as out:
                                shutil.copyfileobj(src, out)
                verify(package, model)
                if dest.exists():
                    raise FileExistsError(dest)
                package.rename(dest)
        print('Verified ' + model['package'])


if __name__ == '__main__':
    main()
