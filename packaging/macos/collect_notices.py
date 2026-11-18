#!/usr/bin/env python3
"""Preserve upstream notices and attribution metadata from pinned sources."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile

QT_SOURCE_SHA256 = '98ff4f44bac6ec3e1e62ee2a4316ae0e3d15badb015d753cc0268a20db52f165'
SOURCES = {
    'qt-everywhere-src-6.12.0.tar.xz': QT_SOURCE_SHA256,
    'ffmpeg-9.0.1.tar.xz': 'cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635',
    'qt5-v6.12.0.tar.gz': '167449f1963e899ef6502330b8791f0e971fb81fdeb560b15af19abd53c15552',
}


def collect(source, destination):
    with source.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != SOURCES[source.name]:
            raise ValueError('Unexpected source archive SHA-256: ' + source.name)
    destination.mkdir(parents=True, exist_ok=True)
    index = destination / 'index.json'
    records = json.loads(index.read_text()) if index.exists() else []
    with tarfile.open(source, 'r|*') as archive:
        for entry in archive:
            path = Path(entry.name)
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Unsafe upstream archive path')
            name = path.name.lower()
            if not entry.isfile() or not (
                name.startswith(('license', 'licence', 'copying', 'copyright', 'notice'))
                or name == 'qt_attribution.json' or 'LICENSES' in path.parts
            ):
                continue
            target = destination / path
            target.parent.mkdir(parents=True, exist_ok=True)
            data = archive.extractfile(entry).read()
            target.write_bytes(data)
            records.append({'path': path.as_posix(), 'sha256': hashlib.sha256(data).hexdigest()})
    if not records:
        raise ValueError('No upstream notices found')
    index.write_text(json.dumps(records, indent=2) + '\n')
    print(f'Preserved {len(records)} upstream notice/attribution files')


if __name__ == '__main__':
    for name in SOURCES:
        collect(Path(sys.argv[1]) / name, Path(sys.argv[2]))
