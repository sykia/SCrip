# SCrip development

Communicate with the user in Russian and maintain project documentation in English. Use C++20, Qt Quick and CMake. The interface is in `qml/Main.qml`, and the entry point is `src/main.cpp`. Account for Linux, Windows and Android.

Add resources and dependencies only when needed. Keep generated files in `build/`, and personal data and secrets outside the repository.

After making changes, build the application with `cmake --build --preset debug` and check the result. For a clean build, run `cmake --preset debug` first.

Launch SCrip when needed to verify changes or investigate an issue; otherwise, do not launch it. No separate user permission is required.
