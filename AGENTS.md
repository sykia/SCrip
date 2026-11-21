# Sange development

Communicate with the user in Russian and maintain project documentation in English. Use C++20, Qt Quick and CMake. The interface is in `qml/Main.qml`, and the entry point is `src/main.cpp`. Account for Linux, Windows and Android.

Add resources and dependencies only when needed. Keep generated files in `build/`, and personal data and secrets outside the repository.

After making changes, build the application with `cmake --build --preset debug` and check the result. For a clean build, run `cmake --preset debug` first.

Launch Sange when needed to verify changes or investigate an issue; otherwise, do not launch it. No separate user permission is required.

For build or release work, read `docs/releases.md` first. It records reusable toolchain locations, platform workflows, signing and upgrade constraints, and the verified publication procedure. Preserve successful build caches and inspect existing CI runs before starting duplicate builds.
