import importlib.util
import io
import json
import plistlib
import tarfile
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('macos_compliance', Path(__file__).parents[1] / 'packaging/macos/compliance.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MacBundleInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.bundle = Path(self.temporary.name) / 'Sange.app'
        executable = self.bundle / 'Contents/MacOS/Sange'
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b'application')

    def test_source_archive_matches_bundle_version(self):
        assets = Path(self.temporary.name) / 'assets'
        assets.mkdir()
        for relative in ['Contents/Frameworks/QtCore', 'Contents/Frameworks/libavcodec.dylib',
                         'Contents/Resources/legal/LICENSE', 'Contents/Resources/legal/DEPENDENCIES.md']:
            path = self.bundle / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture library or license')
        for name in ['qt-everywhere-src-6.12.0.tar.xz', 'ffmpeg-9.0.1.tar.xz', 'qt5-v6.12.0.tar.gz',
                     'Sange-2.1.0-source.tar.gz']:
            with tarfile.open(assets / name, 'w') as archive:
                for filename, content in [('src/main.cpp', b'int main() {}'), ('CMakeLists.txt', b'project(Fixture)')]:
                    entry = tarfile.TarInfo(filename)
                    entry.size = len(content)
                    archive.addfile(entry, io.BytesIO(content))
        (self.bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleShortVersionString': '2.1.0'}))
        image = assets / 'Sange-2.1.0-macOS-arm64.dmg'
        image.write_bytes(b'fixture disk image')
        manifest = assets / 'macOS-arm64-compliance.json'
        module.create(self.bundle, image, 'arm64', assets, manifest)
        data = json.loads(manifest.read_text())
        self.assertEqual(data['components']['sange']['sources'][0]['name'], 'Sange-2.1.0-source.tar.gz')
        (assets / 'Sange-2.1.0-source.tar.gz').unlink()
        with self.assertRaises(FileNotFoundError):
            module.create(self.bundle, image, 'arm64', assets, manifest)

    def test_framework_version_links_are_recorded(self):
        framework = self.bundle / 'Contents/Frameworks/Example.framework'
        (framework / 'Versions/A').mkdir(parents=True)
        (framework / 'Versions/A/Example').write_bytes(b'library')
        (framework / 'Versions/Current').symlink_to('A')
        (framework / 'Example').symlink_to('Versions/Current/Example')
        files, links = module.inventory(self.bundle)
        self.assertEqual(len(files), 2)
        self.assertEqual(len(links), 2)

    def test_external_framework_link_is_rejected(self):
        (self.bundle / 'escape').symlink_to('/etc/passwd')
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            module.inventory(self.bundle)

    def test_broken_internal_link_is_rejected(self):
        (self.bundle / 'broken').symlink_to('missing')
        with self.assertRaisesRegex(ValueError, 'broken'):
            module.inventory(self.bundle)

    def test_missing_executable_is_rejected(self):
        (self.bundle / 'Contents/MacOS/Sange').unlink()
        with self.assertRaisesRegex(ValueError, 'executable'):
            module.inventory(self.bundle)

    def test_modified_bundle_file_is_rejected(self):
        files, links = module.inventory(self.bundle)
        manifest = {'files': {p: {'sha256': sha, 'components': ['app']} for p, sha in files.items()},
                    'symlinks': links, 'components': {'app': {}}}
        (self.bundle / 'Contents/MacOS/Sange').write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            module.validate(self.bundle, manifest, Path(self.temporary.name))

    def test_empty_source_list_is_rejected(self):
        files, links = module.inventory(self.bundle)
        manifest = {'files': {p: {'sha256': sha, 'components': ['app']} for p, sha in files.items()},
                    'symlinks': links,
                    'components': {'app': {'license': 'GPL-3.0-only', 'sources': []}}}
        with self.assertRaisesRegex(ValueError, 'Missing sources'):
            module.validate(self.bundle, manifest, Path(self.temporary.name))


if __name__ == '__main__':
    unittest.main()
