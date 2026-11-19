import importlib.util
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
