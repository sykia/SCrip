#!/usr/bin/env python3
"""Inventory and audit the actual mounted DMG, including framework symlinks."""
import argparse
import hashlib
import json
import os
import plistlib
import re
from pathlib import Path
import subprocess
import tarfile
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(bundle):
    files, links = {}, {}
    bundle = bundle.resolve()
    for parent, dirs, names in os.walk(bundle, followlinks=False):
        for name in dirs + names:
            path = Path(parent) / name
            relative = path.relative_to(bundle).as_posix()
            if path.is_symlink():
                target = os.readlink(path)
                if Path(target).is_absolute() or not path.resolve().is_relative_to(bundle) or not path.exists():
                    raise ValueError('Unsafe or broken bundle symlink: ' + relative)
                links[relative] = target
            elif path.is_file():
                files[relative] = digest(path)
    if not files or 'Contents/MacOS/Sange' not in files:
        raise ValueError('Missing application executable')
    return files, links


def validate(bundle, manifest, assets):
    files, links = inventory(bundle)
    if files != {p: e['sha256'] for p, e in manifest['files'].items()} or links != manifest['symlinks']:
        raise ValueError('Bundle inventory/hash mismatch')
    used = {c for e in manifest['files'].values() for c in e['components']}
    if used != set(manifest['components']) or any(not e['components'] for e in manifest['files'].values()):
        raise ValueError('Incomplete component attribution')
    checked = set()
    for component in manifest['components'].values():
        if component['license'] not in {'GPL-3.0-only', 'LGPL-2.1-or-later'} or not component['sources']:
            raise ValueError('Missing sources or unreviewed primary component license')
        notice = component['notice']
        if notice not in files or not (bundle / notice).stat().st_size:
            raise ValueError('Missing bundled notice')
        for source in component['sources']:
            name = source['name']
            if Path(name).name != name:
                raise ValueError('Unsafe source filename')
            if name in checked:
                continue
            path = assets / name
            if digest(path) != source['sha256']:
                raise ValueError('Source SHA-256 mismatch: ' + name)
            with tarfile.open(path) as archive:
                names = [e.name for e in archive if e.isfile()]
            if not any(Path(n).suffix in {'.c', '.cpp', '.h', '.qml', '.sh', '.py'} for n in names):
                raise ValueError('Missing source code: ' + name)
            if not any(Path(n).name.lower() in {'cmakelists.txt', 'configure', 'readme.md', 'readme'} for n in names):
                raise ValueError('Missing source build materials: ' + name)
            if any(Path(n).is_absolute() or '..' in Path(n).parts for n in names):
                raise ValueError('Unsafe source archive')
            checked.add(name)
    return len(files), len(links)


def create(bundle, image, architecture, assets, output):
    files, links = inventory(bundle)
    qt = assets / 'qt-everywhere-src-6.12.0.tar.xz'
    ffmpeg = assets / 'ffmpeg-9.0.1.tar.xz'
    recipes = assets / 'qt5-v6.12.0.tar.gz'
    with (bundle / 'Contents/Info.plist').open('rb') as stream:
        version = plistlib.load(stream).get('CFBundleShortVersionString')
    if not isinstance(version, str) or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid application bundle version')
    app = assets / f'Sange-{version}-source.tar.gz'
    sources = {p.name: {'name': p.name, 'sha256': digest(p)} for p in (qt, app, ffmpeg, recipes)}
    notice_root = 'Contents/Resources/legal/'
    components = {
        'sange': {'license': 'GPL-3.0-only', 'notice': notice_root + 'LICENSE', 'sources': [sources[app.name]]},
        'qt': {'license': 'GPL-3.0-only', 'notice': notice_root + 'DEPENDENCIES.md',
               'sources': [sources[qt.name], sources[recipes.name]], 'third_party_notices': notice_root + 'upstream/index.json'},
        'ffmpeg': {'license': 'LGPL-2.1-or-later', 'notice': notice_root + 'DEPENDENCIES.md',
                   'sources': [sources[ffmpeg.name], sources[recipes.name]], 'third_party_notices': notice_root + 'upstream/index.json'},
    }
    entries = {}
    for name, sha in files.items():
        if name.startswith('Contents/Frameworks/libav') or name.startswith(('Contents/Frameworks/libswresample', 'Contents/Frameworks/libswscale')):
            ids = ['ffmpeg']
        elif name.startswith(('Contents/Frameworks/', 'Contents/PlugIns/', 'Contents/Resources/qml/', 'Contents/Resources/translations/')):
            ids = ['qt']
        elif '/legal/upstream/' in name:
            ids = ['qt', 'ffmpeg']
        else:
            ids = ['sange']
        entries[name] = {'sha256': sha, 'components': ids}
    manifest = {'schema': 1, 'architecture': architecture, 'artifact': image.name,
                'artifact_sha256': digest(image), 'files': entries, 'symlinks': links,
                'components': components}
    validate(bundle, manifest, assets)
    output.write_text(json.dumps(manifest, indent=2) + '\n')


def audit(image, manifest_path, assets):
    manifest = json.loads(manifest_path.read_text())
    if image.name != manifest['artifact'] or digest(image) != manifest['artifact_sha256']:
        raise ValueError('DMG SHA-256 mismatch')
    with tempfile.TemporaryDirectory() as temporary:
        mount = Path(temporary) / 'mount'
        subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', str(mount), str(image)], check=True)
        try:
            if {p.name for p in mount.iterdir()} - {'Sange.app', 'Applications', '.DS_Store', '.Trashes', '.fseventsd', '.VolumeIcon.icns'}:
                raise ValueError('Unexpected disk image content')
            if not (mount / 'Applications').is_symlink() or os.readlink(mount / 'Applications') != '/Applications':
                raise ValueError('Unexpected Applications shortcut')
            count, links = validate(mount / 'Sange.app', manifest, assets)
            subprocess.run(['codesign', '--verify', '--deep', '--strict', str(mount / 'Sange.app')], check=True)
            print(f'{image.name}: verified {count} files, {links} internal symlinks, bundled notices and corresponding sources')
        finally:
            subprocess.run(['hdiutil', 'detach', str(mount)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--architecture', choices=['arm64', 'x86_64'])
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    if args.bundle:
        create(args.bundle, args.image, args.architecture, args.assets, args.manifest)
    audit(args.image, args.manifest, args.assets)
