# Interface languages

Sange supports English and Russian on desktop (Linux, Windows and macOS) and
Android. Select **Interface language** in Settings. Changes apply immediately,
including native desktop menus, and are saved under `interface/language` in
QSettings. On first launch, Russian is selected for a Russian system locale;
other system locales use English.

QML interface strings use `qsTr()`. The English source-to-translation catalog is
`src/translations_en.inc`; `src/languagecontroller.h` supplies it through Qt's
translator API. Russian is the source language. Add both the `qsTr()` call and
its English catalog entry when adding interface text. Native menu strings use
`QCoreApplication::translate()`. This does not require Linguist tools or extra
runtime dependencies. Track metadata, user content, plugin-provided content,
and backend/service error messages retain their original language.

Settings labels use wrapping and width constraints to accommodate both languages.
Language changes do not reload the QML engine or interrupt playback.

## Validation

For the initial implementation, QML static analysis and catalog coverage were
checked without compiling or launching the application, as requested. Runtime
layout checks on each platform remain to be performed after a build.
