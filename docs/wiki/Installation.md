[Home](Home) › Installation

Download packages from [GitHub Releases](https://github.com/sykia/sange/releases/latest). The filenames below refer to **2.1.0**, whose published assets include `SHA256SUMS`. For another release, use its filenames and checksums.

## Choose a package

| Platform | Package for 2.1.0 |
| --- | --- |
| Windows 10/11, x64 | `Sange-2.1.0-Windows-x64.exe` |
| Debian 13, amd64 | `Sange-2.1.0-debian13-amd64.deb` |
| Arch Linux, x86-64 | `scrip-2.1.0-1-x86_64.pkg.tar.zst` |
| Android 9+, API 28+ | `Sange-2.1.0-Android-universal.apk` |
| macOS 14.4+, Apple Silicon | `Sange-2.1.0-macOS-arm64.dmg` |
| macOS 14.4+, Intel | `Sange-2.1.0-macOS-x86_64.dmg` |

macOS packages use ad-hoc signing without Apple notarization. Other Linux distributions may require a [source build](Building-from-Source); the Debian package is not a universal Linux package.

## Windows

1. Download the EXE and run it.
2. Follow the installer and open Sange.
3. [Sign in](SoundCloud-Authentication) through the browser.

The public 2.1.0 installer has no publisher certificate signature. If Windows denies access, follow [installer troubleshooting](Troubleshooting#windows-installer-is-blocked-or-disappears).

## macOS

Download the DMG for your Mac architecture, open it and copy Sange to Applications. See the [macOS guide](https://github.com/sykia/sange/blob/main/docs/macos.md) for requirements, installation and validation limits.

## Debian 13

In the download directory:

```sh
sudo apt install ./Sange-2.1.0-debian13-amd64.deb
```

APT resolves the dependencies declared by the package. Start **Sange** from the application menu or run `scrip`. Sign-in needs a running, unlocked Secret Service such as GNOME Keyring or a compatible KWallet configuration.

## Arch Linux

In the download directory:

```sh
sudo pacman -U ./scrip-2.1.0-1-x86_64.pkg.tar.zst
```

Start **Sange** from the application menu or run `scrip`. A running, unlocked Secret Service is needed for sign-in. The package remains named `scrip` for upgrade compatibility.

## Android

1. Download and open the APK on the device.
2. If prompted, allow that browser or file manager to install this APK.
3. Install, open Sange and [sign in](SoundCloud-Authentication).

The universal build targets `arm64-v8a`, `armeabi-v7a` and `x86_64`. To open SoundCloud web links in Sange on Android 12+, select supported domains under **Settings → Apps → Sange → Open by default → Supported web addresses**. Device labels vary. You can also share a SoundCloud link to Sange or paste it into search.

## Verify a download

Download `SHA256SUMS` from the **same release** and compare the matching file entry.

Windows PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\Sange-2.1.0-Windows-x64.exe'
```

Linux, for example:

```sh
sha256sum Sange-2.1.0-debian13-amd64.deb
```

If the hash differs, download the file again before installing.

## Upgrading

Install the new package over the existing installation. Do not uninstall or clear application data first. Sange retains SCrip's settings and storage identities; Android keeps `org.scrip.app` and requires the installed release's signing certificate.

Windows, Linux and Android 2.0.0 and older builds allow only the former repository download path. Download 2.1.0 manually from Releases for this first upgrade. Plugins whose host version range excludes 2.1.0 need an updated manifest from their author. An expired or revoked SoundCloud session still requires sign-in.

See [2.1.0 release notes](https://github.com/sykia/sange/blob/main/release-notes/2.1.0.md).

### Updating from Settings

1. Open **Settings → Sange → Check for updates**.
2. When a package is available, choose **Download update**. Download progress and cancellation are available.
3. Choose **Install now** after verification, then complete the platform installation flow.

The updater checks the expected package size and SHA-256 from GitHub release metadata, and checks the file again before installation. A matching package and usable digest must be available for the platform.

| Platform | Installation flow |
| --- | --- |
| Windows | Opens the installer wizard; the result is checked on the next launch |
| Debian / Arch Linux | Runs the package manager through `pkexec`; a working authorization agent and administrative approval are needed |
| Android | Opens the system APK installer; if asked, allow installation from Sange and press **Install now** again |
| macOS | Opens the verified DMG in Finder; replace Sange in Applications |

Other Linux distributions are not supported by this updater. Finish installation and restart Sange to use the installed version. If the handoff fails, use the [manual package instructions](Installation#choose-a-package). Older binaries may still need the manual fallback after the repository rename.

## Opening SoundCloud links

Pasting a track, playlist, station or profile URL into Sange's search works without changing browser settings.

### Desktop

Installed desktop packages register `scrip://open?url=<percent-encoded-SoundCloud-URL>`. Linux packages also advertise the `soundcloud` scheme; Windows setup registers `scrip` for the current user. A source checkout alone does not register these handlers. Desktop command-line arguments also accept ordinary SoundCloud URLs and forward them to the running instance.

For ordinary link clicks in browser pages, an optional extension is supplied in [`packaging/browser-link-bridge/`](https://github.com/sykia/sange/tree/main/packaging/browser-link-bridge):

- Chromium browsers: open the extensions page, enable developer mode and choose **Load unpacked**, selecting that directory from the downloaded/cloned source.
- Firefox development install: open **about:debugging → This Firefox → Load Temporary Add-on** and select its `manifest.json`. Permanent Firefox distribution requires signing.

Install the desktop package first. The extension converts ordinary SoundCloud link clicks into the custom protocol; it does not intercept URLs typed in the address bar. Its **Open in Sange** button can open the current SoundCloud page. Browser external-application prompts still apply. It does not fetch, transmit or store browsing data.

### Android

Use **Share → Sange**, paste a link into search, or select supported SoundCloud web domains in Android's **Open by default** settings as described [above](Installation#android). Domain opening requires user selection because Sange cannot verify domains owned by SoundCloud.

---
[Next: Features →](Features) · [Troubleshooting](Troubleshooting)
