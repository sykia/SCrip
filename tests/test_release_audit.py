import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).parents[1] / 'scripts'))
from audit_release import audit
from prepare_release_materials import prepare


class PartialReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / 'assets'
        self.assets.mkdir()
        self.materials = self.root / 'materials'
        platform = self.materials / 'Android'
        (platform / 'payload').mkdir(parents=True)
        notice = b'Test copyright and license'
        (platform / 'payload/NOTICE').write_bytes(notice)
        package = self.assets / 'test.apk'
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr('NOTICE', notice)
        source = platform / 'source.tar'
        with tarfile.open(source, 'w') as archive:
            for name, data in [('main.cpp', b'int main() {}'), ('README.md', b'Build instructions')]:
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
        sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        self.manifest = platform / 'compliance.json'
        self.manifest.write_text(json.dumps({
            'artifact_sha256': sha(package),
            'files': {'NOTICE': {'sha256': hashlib.sha256(notice).hexdigest(), 'components': ['app']}},
            'components': {'app': {'license': 'GPL-3.0-only', 'notice': 'payload/NOTICE',
                                   'source': 'source.tar', 'source_sha256': sha(source)}}}))

    def test_android_only_release(self):
        audit(self.assets, self.materials, ['Android'])

    def test_full_release_still_requires_windows(self):
        with self.assertRaises(ValueError):
            audit(self.assets, self.materials)

    def test_partial_release_does_not_ignore_modified_apk(self):
        with (self.assets / 'test.apk').open('ab') as package:
            package.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'другому пакету'):
            audit(self.assets, self.materials, ['Android'])

    def test_base_materials_hash_is_required(self):
        descriptor = self.root / 'descriptor'
        descriptor.mkdir()
        (descriptor / 'release.json').write_text(json.dumps({
            'base_materials': {'url': 'https://github.com/sykia/SCrip/releases/download/v1.4.0/compliance.tar.gz',
                               'sha256': '0' * 64}, 'platforms': ['Android']}))
        bundle = self.root / 'base.tar.gz'
        bundle.write_bytes(b'wrong archive')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            prepare(descriptor, self.root / 'out', bundle)

    def test_overlay_restores_sources_and_checks_real_package(self):
        descriptor = self.root / 'descriptor'
        descriptor.mkdir()
        bundle = self.root / 'base.tar.gz'
        with tarfile.open(bundle, 'w:gz') as archive:
            archive.add(self.materials / 'Android', arcname='Android')
        overlay = self.assets / 'materials-update.tar.gz'
        with tarfile.open(overlay, 'w:gz') as archive:
            archive.add(self.manifest, arcname='Android-compliance.json')
            archive.add(self.materials / 'Android/source.tar', arcname='source.tar')
        sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        info = {'base_materials': {
            'url': 'https://github.com/sykia/SCrip/releases/download/v1.4.0/compliance.tar.gz',
            'sha256': sha(bundle)}, 'platforms': ['Android'],
            'materials_overlay': {'name': overlay.name, 'sha256': sha(overlay)}}
        (descriptor / 'release.json').write_text(json.dumps(info))
        output = self.root / 'restored'
        prepare(descriptor, output, bundle, self.assets)
        audit(self.assets, output, ['Android'])
        with overlay.open('ab') as target:
            target.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'SHA-256 дополнения'):
            prepare(descriptor, self.root / 'tampered', bundle, self.assets)
