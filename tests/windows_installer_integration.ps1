$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$installerDir = Join-Path $repo 'packaging\windows'
$work = Join-Path $env:RUNNER_TEMP ('scrip-installer-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $work 'installed'
$payload1 = Join-Path $work 'payload-v1'
$payload2 = Join-Path $work 'payload-v2'
$output1 = Join-Path $work 'v1.exe'
$output2 = Join-Path $work 'v2.exe'
$settings = Join-Path $work 'user-settings\settings.ini'

function Assert($condition, $message) {
    if (!$condition) { throw $message }
}

function Build-Installer($payload, $output, $version, $pauseAfterUninstall = $false) {
    $fileList = Join-Path $work "$version.uninstall.nsh"
    $entries = Get-ChildItem -LiteralPath $payload -Recurse -File |
        Sort-Object { $_.FullName.Length } -Descending |
        ForEach-Object {
            $relative = [IO.Path]::GetRelativePath($payload, $_.FullName).Replace('/', '\')
            'Delete "$INSTDIR\' + $relative.Replace('$', '$$') + '"'
        }
    $entries += Get-ChildItem -LiteralPath $payload -Recurse -Directory |
        Sort-Object { $_.FullName.Length } -Descending |
        ForEach-Object {
            $relative = [IO.Path]::GetRelativePath($payload, $_.FullName).Replace('/', '\')
            'RMDir "$INSTDIR\' + $relative.Replace('$', '$$') + '"'
        }
    Set-Content -LiteralPath $fileList -Value $entries -Encoding utf8
    $args = @("-DPAYLOAD=$payload", "-DOUTPUT=$output", "-DFILELIST=$fileList", "-DTEST_INSTALL_DIR=$install")
    if ($pauseAfterUninstall) { $args += '-DTEST_PAUSE_AFTER_UNINSTALL' }
    & makensis @args 'installer.nsi'
    if ($LASTEXITCODE -ne 0) { throw "makensis failed for $version ($LASTEXITCODE)" }
}

try {
    Push-Location $installerDir
    New-Item -ItemType Directory -Path $work, $payload1, $payload2 -Force | Out-Null
    Set-Content (Join-Path $payload1 'SCrip.exe') 'version-one'
    Set-Content (Join-Path $payload1 'obsolete.dll') 'old-only application file'
    Set-Content (Join-Path $payload2 'SCrip.exe') 'version-two'
    Build-Installer $payload1 $output1 'v1'
    Build-Installer $payload2 $output2 'v2' $true

    $initialInstall = Start-Process -FilePath $output1 -ArgumentList '/S' -PassThru -Wait
    Write-Host "Initial NSIS installer exit code: $($initialInstall.ExitCode)"
    Assert ($initialInstall.ExitCode -eq 0) 'Initial install failed.'
    if (!(Test-Path (Join-Path $install 'Uninstall.exe'))) {
        Get-ChildItem -LiteralPath $work -Force | Format-Table FullName, Length | Out-String | Write-Host
        $defaultInstall = Join-Path $env:LOCALAPPDATA 'Programs\SCrip'
        if (Test-Path $defaultInstall) { Get-ChildItem -LiteralPath $defaultInstall -Force | Format-Table FullName, Length | Out-String | Write-Host }
        $trace = "$install.trace"
        if (Test-Path $trace) { Get-Content -LiteralPath $trace -Raw | Write-Host }
        throw 'Installer did not use the isolated test directory.'
    }
    if (Test-Path "$install.trace") { Get-Content -LiteralPath "$install.trace" -Raw | Write-Host }
    Get-ChildItem -LiteralPath $install -Recurse -Force | Select-Object FullName, Length | Format-Table | Out-String | Write-Host
    Assert ((Get-Content (Join-Path $install 'SCrip.exe') -Raw).Trim() -eq 'version-one') 'Version 1 was not installed.'
    Assert (Test-Path (Join-Path $install 'obsolete.dll')) 'Legacy-only file was not installed for the upgrade test.'

    Set-Content (Join-Path $install 'user-notes.txt') 'keep this user file'
    New-Item -ItemType Directory -Path (Split-Path $settings) -Force | Out-Null
    Set-Content $settings 'keep user settings'

    # Pause after the old uninstaller has removed version 1. Kill setup to model
    # power loss in the exact interval that used to leave no working version.
    Set-Content "$install.__SCrip_test_pause" 'pause once'
    $upgrade = Start-Process -FilePath $output2 -ArgumentList '/S' -PassThru
    $marker = "$install.__SCrip_marker"
    $deadline = (Get-Date).AddSeconds(45)
    while ((Get-Date) -lt $deadline) {
        $upgrade.Refresh()
        if ($upgrade.HasExited) { throw "Upgrade exited too early ($($upgrade.ExitCode))." }
        if ((Test-Path $marker) -and !(Test-Path (Join-Path $install 'SCrip.exe'))) { break }
        Start-Sleep -Milliseconds 100
    }
    Assert ((Test-Path $marker) -and !(Test-Path (Join-Path $install 'SCrip.exe'))) 'Did not reach the interrupted-upgrade window.'
    Stop-Process -Id $upgrade.Id -Force
    try { $upgrade.WaitForExit() } catch { }
    Assert (!(Test-Path (Join-Path $install 'SCrip.exe'))) 'Fault injection did not interrupt setup after the old version was removed.'
    Remove-Item -LiteralPath "$install.__SCrip_test_pause" -Force -ErrorAction SilentlyContinue

    # The next real setup must recover v1 first, then finish upgrading to v2.
    & $output2 '/S'
    Assert ($LASTEXITCODE -eq 0) 'Recovery and upgrade failed.'
    if (Test-Path "$install.trace") { Write-Host 'Recovery trace:'; Get-Content -LiteralPath "$install.trace" -Raw | Write-Host }
    Get-ChildItem -LiteralPath $install -Recurse -Force | Select-Object FullName, Length | Format-Table | Out-String | Write-Host
    Assert ((Get-Content (Join-Path $install 'SCrip.exe') -Raw).Trim() -eq 'version-two') 'Version 2 was not installed after recovery.'
    Assert (!(Test-Path (Join-Path $install 'obsolete.dll'))) 'Stale application file survived the upgrade.'
    Assert ((Get-Content (Join-Path $install 'user-notes.txt') -Raw).Trim() -eq 'keep this user file') 'An unrelated file in the install folder was changed.'
    Assert ((Get-Content $settings -Raw).Trim() -eq 'keep user settings') 'User settings were changed.'
    Assert (!(Test-Path $marker)) 'Recovery marker was not cleared after successful upgrade.'

    # Reinstalling the same version must be safe and idempotent.
    Build-Installer $payload2 $output2 'v2-repeat'
    & $output2 '/S'
    Assert ($LASTEXITCODE -eq 0) 'Repeated installation failed.'
    Assert ((Get-Content (Join-Path $install 'user-notes.txt') -Raw).Trim() -eq 'keep this user file') 'Repeated installation removed a user file.'

    # Hold the executable exclusively: setup must time out and leave v2 intact.
    $lock = [IO.File]::Open((Join-Path $install 'SCrip.exe'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        & $output2 '/S'
        Assert ($LASTEXITCODE -ne 0) 'Setup unexpectedly succeeded while SCrip.exe was locked.'
    } finally { $lock.Dispose() }
    Assert ((Get-Content (Join-Path $install 'SCrip.exe') -Raw).Trim() -eq 'version-two') 'Locked-executable failure damaged the installed version.'

    & (Join-Path $install 'Uninstall.exe') '/S'
    Assert ($LASTEXITCODE -eq 0) 'Uninstall failed.'
    Assert (!(Test-Path (Join-Path $install 'SCrip.exe'))) 'Uninstall left the application executable behind.'
    Assert ((Get-Content (Join-Path $install 'user-notes.txt') -Raw).Trim() -eq 'keep this user file') 'Uninstall removed an unrelated user file.'
    Assert ((Get-Content $settings -Raw).Trim() -eq 'keep user settings') 'Uninstall removed user settings.'
    Write-Host 'Windows NSIS integration passed: install, interrupted upgrade recovery, stale files, user data, repeat install, locked EXE, uninstall.'
} finally {
    Pop-Location
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
