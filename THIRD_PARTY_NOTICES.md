# Third-party components and review status

SCrip's license applies to the project's own materials. Rights to dependencies and user content remain with their respective holders. This document is an initial inventory and record of known limitations, rather than a complete license bundle or confirmation of release compliance.

## Qt

SCrip uses Qt Core, GUI, Widgets (desktop), QML, Quick, Network and Multimedia. Individual modules and their bundled third-party components have their own licenses. Applicable modules may be used under LGPLv3 if its conditions are met; some Qt components may require GPL or a commercial license.

LGPL distribution requires license texts and notices, corresponding library sources or an appropriate offer to provide them, and the ability to replace libraries and run the relinked application. Android also requires consideration of APK rebuilding and signing. Dynamic linking alone does not satisfy every condition.

- https://www.qt.io/development/open-source-lgpl-obligations
- https://doc.qt.io/qt-6/licenses-used-in-qt.html

## FFmpeg — identified Windows 1.3.0 limitation

Inspection of `build/windows-installer/payload/avutil-61.dll`, included in the published 1.3.0 installer, found `--enable-gpl`, `--enable-version3` and the string `libavutil license: GPL version 3 or later`.

SCrip is now distributed under GNU GPLv3, resolving the earlier conflict with GPLv3 FFmpeg components at the level of the project's own code license.

Compatibility of other dependencies and GPL requirements for sources, license notices and build materials still require verification. Changing LICENSE alone does not confirm compliance of previously published builds.

- https://ffmpeg.org/legal.html

## Other dependencies

Linux uses system libsecret (LGPL-2.1-or-later) for Secret Service; it is dynamically linked and is not bundled in the SCrip package. Source and license: https://gitlab.gnome.org/GNOME/libsecret.

Android includes OpenSSL from android_openssl. OpenSSL 3 uses Apache License 2.0; exact versions and additional materials for the selected build must be checked. The Windows installer also contains transitive MSYS2 libraries; some license texts are in the installed application's `licenses` directory. That directory does not replace checking notice completeness and providing sources under applicable licenses.

Before the next release, a complete inventory of actually shipped libraries, license texts, corresponding sources/offers and LGPL library replacement instructions is required. On Linux, distinguish system dependencies from libraries shipped directly in the package.

## SoundCloud and content

SCrip is an independent client. The SoundCloud name and related identifiers belong to their respective holders; user audio, artwork, avatars and other materials are not covered by SCrip's license. Their appearance in screenshots does not transfer rights.

API terms require, among other things, attribution, data handling rules and a privacy policy. They also restrict alternative listening services. Compliance of specific functionality and the scope of granted permission require separate review; SCrip's license is not SoundCloud's consent.

- https://developers.soundcloud.com/docs/api/terms-of-use

## November 7, 2026 audit: GPL-3.0-only

The author's GPL-3.0-only choice is retained. GPLv3 FFmpeg is compatible with this license; replacing codecs or disabling features is unnecessary. OpenSSL 3 under Apache-2.0 is compatible with GPLv3 (https://www.apache.org/licenses/GPL-compatibility). LGPLv2.1-or-later/LGPLv3 and GPLv3 options for core Qt modules also permit a GPLv3 application. This conclusion concerns the named licenses, rather than certification of the entire installer.

The actual local Windows payload contains 199 DLLs, including plugins; the APK contains 285 native `.so` files and one DEX. DEB/Arch system dependencies are listed in packaging/linux/control and PKGBUILD; they differ from libraries bundled in EXE/APK packages. Android includes Qt Java code, AndroidX Core and transitive dependencies, which must be accounted for in DEX analysis.

Complete per-file attribution for the 199 DLLs and 285 SOs, DEX dependencies, and exact sources with patches and build instructions are **not yet confirmed**. An MSYS2 licenses directory or upstream links do not replace corresponding sources. Release 1.3.0 status: source and notice provision compliance is unconfirmed. Older packages have not been replaced or declared corrected.

Added extraction of actual release packages, binding manifests to release-file SHA-256, exact inventory checks, file SHA-256 checks, local source and bundled notice checks, negative tests and CI. CI requires separate materials per platform and fails when they are missing. This checks presence and integrity, rather than automatically proving legal compatibility or reproducibility. Format: licensing/README.md.

## Release 1.4.0

Inventory, SHA-256, notice and corresponding source materials are supplied in `compliance.tar.gz` separately for Windows, Android, Debian and Arch. Validation compares them with the actual extracted packages. This checks presence and integrity of materials; it does not replace legal advice or reproducibility verification.

For Windows, FFmpeg 9.0.2 was rebuilt from source without external codecs or libavfilter, with HTTPS through Windows Schannel. The librsvg → gdk-pixbuf → TIFF → JBIG chain is no longer shipped. The LGPL-2.1-or-later FFmpeg option is used. Recipe: `packaging/windows/build-ffmpeg.sh`. Corresponding MSYS2 versions and patches are retained in source archives. Qt uses its available GPLv3 option, with original third-party notices preserved. The LLVM exception, bzip2 and composite libjpeg-turbo license were checked against bundled NOTICE texts and added to the validator's exact allowlist of expressions.

Android uses Qt 6.10.1, FFmpeg 7.1.2, OpenSSL 3.1.8 and NDK r27c. FFmpeg with OpenSSL 3 uses the permitted LGPLv3 option from LGPL-2.1-or-later. Materials include the complete Qt source archive, FFmpeg and OpenSSL sources with build parameters, the base LLVM revision with Android patches, and published Java runtime dependency sources. Notices are also in the APK's assets/licenses. The Maven runtime dependency tree and version coordinates are recorded.

Debian and Arch packages contain the application and SCrip materials; Qt and codecs remain system dependencies supplied by the package manager.
