import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WindowsInstallerTests(unittest.TestCase):
    def test_update_has_staging_backup_and_startup_recovery(self):
        script = (ROOT / "packaging/windows/installer.nsi").read_text(encoding="utf-8")
        self.assertIn('"$INSTDIR.__SCrip_stage"', script)
        self.assertIn('"$INSTDIR.__SCrip_backup"', script)
        self.assertIn('"$INSTDIR.__SCrip_marker"', script)
        self.assertIn('Function .onInstFailed', script)
        self.assertIn('Function RestorePrevious', script)
        self.assertIn('"$WINDIR\\Sysnative\\robocopy.exe" "$2" "$INSTDIR" /MIR', script)

    def test_uninstall_only_removes_manifest_entries_and_empty_directories(self):
        script = (ROOT / "packaging/windows/installer.nsi").read_text(encoding="utf-8")
        uninstall = script.split('Section "Uninstall"', 1)[1]
        self.assertIn('!include "${FILELIST}"', uninstall)
        self.assertIn('RMDir "$INSTDIR"', uninstall)
        self.assertNotIn("RMDir /r", uninstall)

    def test_package_manifest_rejects_symlinks_and_uses_stable_order(self):
        package = (ROOT / "packaging/windows/package.py").read_text(encoding="utf-8")
        self.assertIn("item.is_symlink()", package)
        self.assertIn(".casefold(), reverse=True", package)
        self.assertIn('command = "RMDir" if item.is_dir() else "Delete"', package)

    def test_windows_workflow_runs_real_nsis_integration(self):
        workflow = (ROOT / ".github/workflows/windows-installer-test.yml").read_text(encoding="utf-8")
        self.assertIn("windows-2022", workflow)
        self.assertIn("tests/windows_installer_integration.ps1", workflow)
        self.assertIn("pull_request:", workflow)
        self.assertIn("workflow_dispatch:", workflow)


if __name__ == "__main__":
    unittest.main()
