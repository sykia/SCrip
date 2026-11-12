# SCrip development

Communicate with the user in Russian and maintain project documentation in English. Use C++20, Qt Quick and CMake. The interface is in `qml/Main.qml`, and the entry point is `src/main.cpp`. Account for Linux, Windows and Android.

Add resources and dependencies only when needed. Keep generated files in `build/`, and personal data and secrets outside the repository.

After making changes, build the application with `cmake --build --preset debug`, check the result and leave the current build open. For a clean build, run `cmake --preset debug` first.

After every completed task, launch the current SCrip build and leave its window open.
