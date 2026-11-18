# Sange 2.0.1 macOS dependency materials

Sange is GPL-3.0-only. This package dynamically links the official Qt 6.12.0
clang_64 libraries and Qt's bundled FFmpeg 9.0.1 libraries. No downstream
changes were made to these libraries. Deployment rewrites Mach-O install names
and applies an ad-hoc signature; it does not modify library source code.

Qt modules in the bundle originate from qtbase, qtdeclarative, qtsvg and
qtmultimedia. The GPLv3 option is used for Qt's own code. Qt's incorporated
third-party components retain their original licenses. FFmpeg uses its
LGPL-2.1-or-later option and its embedded third-party license terms. Do not
interpret the Qt license choice as relicensing incorporated third-party code.

`upstream/` preserves upstream license, copyright and notice files and Qt
attribution JSON from the complete, unmodified Qt source distribution, including
third-party source trees. `upstream/index.json` lists their hashes. This includes
additional upstream components that are not used by Sange; their inclusion does
not mean their binaries are shipped. The per-file release manifests identify
which files are actually distributed. Apple system frameworks and system
libraries are OS dependencies and are not copied into the package.

The release provides these corresponding-source assets beside the DMGs:

- `Sange-2.0.1-source.tar.gz`: application sources, CMake files, packaging scripts,
  native CI recipe and this document.
- `qt-everywhere-src-6.12.0.tar.xz`: complete original Qt 6.12.0 source distribution,
  including bundled third-party code and qtmultimedia's FFmpeg attribution files. SHA-256:
  `98ff4f44bac6ec3e1e62ee2a4316ae0e3d15badb015d753cc0268a20db52f165`.
- `ffmpeg-9.0.1.tar.xz`: original FFmpeg sources; SHA-256
  `cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635`.
- `qt5-v6.12.0.tar.gz`: Qt supermodule's original provisioning/build recipes;
  SHA-256 `167449f1963e899ef6502330b8791f0e971fb81fdeb560b15af19abd53c15552`.

Qt archive origin:
https://download.qt.io/archive/qt/6.12/6.12.0/single/qt-everywhere-src-6.12.0.tar.xz
Qt FFmpeg provenance:
https://doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html

## Building and replacing libraries

Use Xcode command-line tools, CMake and Ninja on macOS 14.4 or newer. Extract
the Qt archive. Qt's `configure` and CMake files and each third-party source
directory contain the upstream build scripts. Use `-release -opensource
-confirm-license -nomake examples -nomake tests`, set an installation prefix,
and build qtbase, qtdeclarative, qtsvg, qtshadertools and qtmultimedia for the
chosen `arm64` or `x86_64` architecture. The FFmpeg build scripts under
`qt5-6.12.0/coin/provisioning/common/macos/install-ffmpeg-macos.sh`,
`common/unix/ffmpeg-installation-utils.sh` and
`common/shared/ffmpeg_config_options.txt` supply Qt's FFmpeg recipe. Extract
the provided FFmpeg source archive instead of redownloading it; the deployed
binary's actual configure string and Qt installed configuration are recorded
in `BUILD-INFO.txt`. Use those settings for the matching library configuration.
Build with CMake/Ninja and install to the chosen prefix. Set `CMAKE_PREFIX_PATH`
to that prefix when configuring Sange using the commands in `docs/macos.md`.
The `.github/workflows/macos-build.yml` recipe specifies the released application
configuration, architecture, dependency versions and tests.

Users may modify and rebuild Sange and its libraries. There is no application
enforced library signature check or DRM. To replace libraries in a local copy,
copy the replacement frameworks/dylibs and compatible plugins into
`Sange.app/Contents/Frameworks` and `Contents/PlugIns`, retain the bundle's relative
install names (or use `install_name_tool` to adjust them), then apply a local
ad-hoc signature with `codesign --force --deep --sign - Sange.app`. This does not
require the publisher's signing key. Alternatively rebuild and redeploy the
entire app with the replacement Qt installation using `macdeployqt` and the
packaging recipe. Do not disable Gatekeeper globally.
