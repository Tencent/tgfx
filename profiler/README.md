## Introduction

Profiler is a specialized tool designed for analyzing and optimizing TGFX.
It provides TGFX with instrumentation macros for profiling and recording performance metrics, including function timing, DrawCalls, and triangle counts. Originally, [Tracy](https://github.com/wolfpld/tracy) was used for this purpose. However, due to its implementation being closely aligned with game engines, Profiler was built upon it with modifications to better adapt to TGFX's engine.

## Platform Support

- macOS 10.15 or later
- Windows 7.0 or later

## Build Prerequisites

Profiler uses **C++17** features. Here are the minimum tools needed to build TGFX on different platforms:

- Xcode 11.0+
- GCC 9.0+
- Visual Studio 2019+
- NodeJS 14.14.0+
- Ninja 1.9.0+
- CMake 3.13.0+
- QT 6.8.1+
- NDK 19.2+ (**19.2.5345600 recommended**)

## Dependencies

Profiler and TGFX both use depsync to manage third-party dependencies.

**For macOS platform：**

Run this script from the root of the project:

```
./sync_deps.sh
```

This script will automatically install the necessary tools and sync all third-party repositories.

**For other platforms:**

First, ensure you have the latest version of Node.js installed (you may need to restart your computer afterward).
Then, run the following command to install the depsync tool:

```
npm install -g depsync
```

Then, run `depsync` in the project's root directory.

```
depsync
```

You might need to enter your Git account and password during synchronization. Make sure you’ve
enabled the `git-credential-store` so that `CMakeLists.txt` can automatically trigger synchronization
next time.

## Getting Started

For **macOS** users, open the `profiler/` directory in CLion. Then, go to the `qt/QTCMAKE.cfg` file and
update the QT path to your local QT installation path. After that, you can build and run the
`Profiler` target.

For **Windows** users, make sure the ToolChain in CLion is set to `Visual Studio` with the `amd64`
architecture. Then, go to the `qt/` folder in CLion and find the `qt/QTCMAKE.cfg` file. Update the
QT path to your local QT installation path. Next, in the configuration panel of the `Profiler` target
in CLion, set the local QT DLL library path in the `Environment Variables` field,
e.g., `PATH=C:\Qt\6.6.1\msvc2019_64\bin`. Finally, you can build and run the `Profiler` target.
