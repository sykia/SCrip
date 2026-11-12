# If Windows denies access to the installer

Open **Downloads** in File Explorer and check whether the downloaded EXE is still there. If it disappeared, open **Windows Security → Protection history** and find the event at the time of launch. The event name and filename are useful when contacting support.

If the file is present, open PowerShell in its folder and check SHA-256:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\SCrip-1.5.0-Windows-x64.exe'
```

Compare the result with the installer's entry in `SHA256SUMS` from [release 1.5.0](https://github.com/sykia/SCrip/releases/tag/v1.5.0). Download the installer again if it differs. If the hash matches, report when the error appears and include the Protection history event. On a managed PC, an administrator can check execution policy using the filename and event time.

## Building with a signature

On Windows, open a terminal with SignTool from Windows SDK and provide the thumbprint of a trusted code-signing publisher certificate and its timestamp service URL:

```powershell
python packaging/windows/package.py payload build/SCrip-Setup-x64.exe --sign-certificate $env:SCRIP_CERT_SHA1 --timestamp-url $env:SCRIP_TIMESTAMP_URL
```

The packager signs an application copy before packaging, then signs the finished installer and verifies both signatures. The private key stays with the publisher. The public 1.5.0 installer is not signed with a publisher certificate.
