import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WindowsInstallerTests(unittest.TestCase):
    def test_upgrade_runs_previous_uninstaller_silently_before_copying_files(self):
        script = (ROOT / "packaging/windows/installer.nsi").read_text(encoding="utf-8")
        install = script.split("Section\n", 1)[1].split("SectionEnd", 1)[0]
        self.assertLess(install.index('ExecWait \'"$INSTDIR\\Uninstall.exe" /S\''),
                        install.index('File /r "${PAYLOAD}\\*"'))
        self.assertIn("StrCmp $0 0 install_files upgrade_failed", install)

    def test_uninstall_only_removes_manifest_entries_and_empty_directories(self):
        script = (ROOT / "packaging/windows/installer.nsi").read_text(encoding="utf-8")
        uninstall = script.split('Section "Uninstall"', 1)[1]
        self.assertIn('!include "${FILELIST}"', uninstall)
        self.assertIn('RMDir "$INSTDIR"', uninstall)
        self.assertNotIn("RMDir /r", script)

    def test_package_manifest_rejects_symlinks_and_uses_stable_order(self):
        package = (ROOT / "packaging/windows/package.py").read_text(encoding="utf-8")
        self.assertIn("item.is_symlink()", package)
        self.assertIn(".casefold(), reverse=True", package)
        self.assertIn('command = "RMDir" if item.is_dir() else "Delete"', package)


if __name__ == "__main__":
    unittest.main()
