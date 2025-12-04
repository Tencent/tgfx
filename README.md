<img src="resources/readme/TGFX.jpg" alt="TGFX Logo" width="992"/>

[![license](https://img.shields.io/badge/license-BSD--3--Clause-blue)](https://github.com/Tencent/tgfx/blob/master/LICENSE.txt)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/Tencent/tgfx/pulls)
[![codecov](https://codecov.io/gh/Tencent/tgfx/branch/main/graph/badge.svg)](https://codecov.io/gh/Tencent/tgfx)
[![autotest](https://github.com/Tencent/tgfx/actions/workflows/autotest.yml/badge.svg?branch=main)](https://github.com/Tencent/tgfx/actions/workflows/autotest.yml)
[![build](https://github.com/Tencent/tgfx/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/Tencent/tgfx/actions/workflows/build.yml)
[![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/Tencent/tgfx)](https://github.com/Tencent/tgfx/releases)

## Introduction

TGFX (Tencent Graphics) is a lightweight 2D graphics library designed for modern GPUs. It delivers high-performance, feature-rich
rendering of text, images, and vector graphics on all major platforms, including iOS, Android, macOS, Windows, Linux, OpenHarmony, 
and the Web. Initially developed as the default graphics engine for the [PAG](https://pag.io) project starting from version 4.0, 
TGFX aims to be a compelling alternative to the Skia graphics library while maintaining a much smaller binary size.
Over time, it has found its way into many other products, such as [Hippy](https://github.com/Tencent/Hippy), [Tencent Docs](https://docs.qq.com) 
and various video-editing apps.

## Platform Support

- iOS 9.0+
- Android 5.0+
- HarmonyOS 5.0+
- macOS 10.15+
- Windows 7.0+
- Linux (No specific version requirement)
- Chrome 69.0+ (Web)
- Safari 11.3+ (Web)

## Rendering Backends

- OpenGL 3.2+ (Desktop)
- OpenGL ES 3.0+
- WebGL 2.0+
- Metal 1.1+ (in progress)
- Vulkan 1.1+ (in progress)
- WebGPU (in progress)

## Build Prerequisites

TGFX uses **C++17** features. Here are the minimum tools needed to build TGFX on different platforms:

- Xcode 11.0+
- GCC 9.0+
- Visual Studio 2019+
- NodeJS 14.14.0+
- Ninja 1.9.0+
- CMake 3.13.0+
- QT 5.13.0+
- NDK 20+ (**20.1.5948944 recommended**)
- Emscripten 3.1.58+ 


Please note the following additional notices:

- Ensure you have installed the **[Desktop development with C++]** and **[Universal Windows Platform development]** components for VS2019.
- It is **highly recommended** to use the **latest version of CMake**. Many older versions of CMake may have various bugs across different platforms.

## Branch Management

- The `main` branch is our active development branch, containing the latest features and bug fixes.
- The branches under `release/` are our stable milestone branches, which are fully tested. We
  periodically create a `release/{version}` branch from the `main` branch. Once a `release/{version}`
  branch is created, only high-priority fixes are checked into it.

## Dependencies

TGFX uses the [**depsync**](https://github.com/domchen/depsync) tool to manage third-party dependencies.

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

We provide concise demos for various platforms to help you integrate the tgfx library into your 
project. After building the project, you'll have a simple app that renders different test cases from
the `drawers/` directory. These test cases include rendering shapes, images, and basic text. You can
switch between test cases with a simple touch on the screen. For more guidance on API usage, check 
out the test cases in the `test/` directories, which can offer valuable insights and assistance.

Before building the demo projects, please carefully follow the instructions in the 
[**Build Prerequisites**](#build-prerequisites) and [**Dependencies**](#dependencies) sections. 
These will guide you through the necessary steps to set up your development environment.

### Android

The Android demo project requires the **Android NDK**. We recommend using version **20.1.5948944**,
which has been fully tested with the TGFX library. If you open the project with Android Studio, it
will automatically download the NDK during Gradle synchronization. Alternatively, you can download 
it from the [NDK Downloads](https://developer.android.com/ndk/downloads) page.

If you choose to manually download the Android NDK, please extract it to the default location.
On macOS, this would be:

```
/Users/yourname/Library/Android/sdk/ndk/20.1.5948944
```

On Windows, it would be：

```
C:\Users\yourname\AppData\Local\Android\Sdk\ndk\20.1.5948944
```

Alternatively, you can set one of the following environment variables to help tgfx locate the NDK:

```
["ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK", "NDK_HOME", "NDK_ROOT", "NDK_PATH"]
```

To get started, open the `android/` directory in Android Studio. If you encounter any issues during 
Gradle synchronization, make sure you haven't accidentally clicked on any pop-up hints for Gradle 
version upgrades. If you have, undo the changes and try synchronizing again. If the issue is related
to your IDE configuration, search for a solution on Google. If you believe the problem is with the 
project configuration, you can open an [Issue](https://github.com/Tencent/tgfx/issues/new/choose) to
address it.

### iOS

In the `ios/` directory, run the following command or double-click it:

```
./gen_ios
```

This will generate an Xcode project for iPhone devices. To generate a project for simulators instead,
use the following command:

```
./gen_simulator
```

This will generate a simulator project for the native architecture, such as `arm64` for Apple Silicon
Macs and `x64` for Intel Macs. If you want to generate a project for a specific architecture, use 
the `-a` option:

```
./gen_simulator -a x64
```

You can also pass CMake options using the `-D` flag. For example, to enable WebP encoding support, run:

```
./gen_ios -DTGFX_USE_WEBP_ENCODE=ON
```

Finally, open Xcode and launch the `ios/Hello2D.xcworkspace` to build and run the demo project.

### macOS


In the `mac/` directory, run the following command or double-click it:

```
./gen_mac
```

This will generate a project for the native architecture, such as `arm64` for Apple Silicon Macs or 
`x64` for Intel Macs. If you want to generate a project for a specific architecture, use the `-a` 
option, for example:

```
./gen_mac -a x64
```

You can also pass CMake options using the `-D` flag. For example, to enable FreeType support, run:

```
./gen_mac -DTGFX_USE_FREETYPE=ON
```

Finally, open Xcode and launch the `mac/Hello2D.xcworkspace`. You are all set!

### Web

To run the web demo, you need the **Emscripten SDK**. You can download and install it from the 
[official website](https://emscripten.org/). We recommend using the latest version. If you’re on 
macOS, you can also install it using the following script:

```
brew install emscripten
```

To get started, go to the `web/` directory and run the following command to install the necessary
node modules:

```
npm install
```

Then, in the `web/` directory, run the following command to build the demo project:

```
npm run build
```

This will generate the `hello2d.js` and `hello2d.wasm` files in the `web/demo/wasm` directory.
Next, you can start an HTTP server by running the following command:

```
npm run server
```

This will open [http://localhost:8081/web/demo/index.html](http://localhost:8081/web/demo/index.html) 
in your default browser. You can also open it manually to view the demo.

To debug the C++ code, install the browser plugin:
[**C/C++ DevTools Support (DWARF)**](https://chromewebstore.google.com/detail/cc++-devtools-support-dwa/pdcpmagijalfljmkmjngeonclgbbannb).
Then, open Chrome DevTools, go to Settings > Experiments, and enable the option
**WebAssembly Debugging: Enable DWARF support**.

Next, replace the previous build command with:

```
npm run build:debug
```

With these steps completed, you can debug C++ files directly in Chrome DevTools.

The above commands build and run a multithreaded version. 

>**⚠️** In the multithreaded version, if you modify the filename of the compiled output hello2d.js, you need to search for
> the keyword "hello2d.js" within the hello2d.js file and replace all occurrences of "hello2d.js" with the new filename.
> Failure to do this will result in the program failing to run. Here's an example of how to modify it:

Before modification:

```js
    // filename: hello2d.js
    var worker = new Worker(new URL("hello2d.js", import.meta.url), {
     type: "module",
     name: "em-pthread"
    });
```

After modification:

```js
    // filename: hello2d-test.js
    var worker = new Worker(new URL("hello2d-test.js", import.meta.url), {
     type: "module",
     name: "em-pthread"
    });
```

To build a single-threaded version, just add the suffix ":st" to each command. For example:

```
npm run build:st
npm run build:st:debug
npm run server:st
``` 

To build the demo project in CLion, open the `Settings` panel and go to `Build, Execution, Deployment` > `CMake`.
Create a new build target and set the `CMake options` to:

```
DCMAKE_TOOLCHAIN_FILE="path/to/emscripten/emscripten/version/cmake/Modules/Platform/Emscripten.cmake"
```

After creating the build target, adjust the `Configurations` to match the new build target. This will 
allow you to build the tgfx library in CLion.

Additionally, when using `ESModule` for your project, you need to manually include the generated 
`.wasm` file in the final web program. Common packing tools often ignore the `.wasm` file. Also, 
make sure to upload the `.wasm` file to a server so users can access it.

### Linux

On Linux, systems often lack GPU hardware support. To address this, we use the [**SwiftShader**](https://github.com/google/swiftshader) 
library to emulate a GPU rendering environment. Since SwiftShader depends on certain X11 header files,
you need to install the following packages before building the demo project:

```
yum install libX11-devel --nogpg
```

Next, run the following commands in the `linux/` directory:

```
cmake -B ./build -DCMAKE_BUILD_TYPE=Release
cmake --build ./build -- -j 12
```

The demo executable will be located in the build directory. You can also open the `linux/` directory
in CLion and build the demo project directly in the IDE.

### Windows

To get started, open the `win/` directory in CLion. Then, go to `File->Settings` and navigate to
`Build, Execution, Deployment->ToolChains`. Set the toolchain to `Visual Studio` with either `amd64`
(recommended) or `x86` architecture. It's also recommended to use the `Ninja` generator for CMake to
speed up the build process. You can set this in `Build, Execution, Deployment->CMake` by choosing 
`Ninja` in the `Generator` row. Once done, you'll be able to build and run the `Hello2D` target.

If you prefer using Visual Studio IDE, open the `x64 Native Tools Command Prompt for VS 2019` and 
run the following command in the `win/` directory:

```
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug" -B ./Debug-x64
```

This will generate a project for the `x64` architecture with the `Debug` configuration. To generate 
a project for the `x86` architecture with the `Release` configuration, open the 
`x86 Native Tools Command Prompt for VS 2019` and run the following command:

```
cmake -G "Visual Studio 16 2019" -A Win32 -DCMAKE_CONFIGURATION_TYPES="Release" -B ./Release-x86
```

Finally, open the `Hello2D.sln` file in the `Debug-x64/` or `Release-x86/` directory, and you’re all
set!

### QT

For **macOS** users, open the `qt/` directory in CLion. Then, go to the `qt/QTCMAKE.cfg` file and
update the QT path to your local QT installation path. After that, you can build and run the 
`Hello2D` target.

For **Windows** users, make sure the ToolChain in CLion is set to `Visual Studio` with the `amd64` 
architecture. Then, go to the `qt/` folder in CLion and find the `qt/QTCMAKE.cfg` file. Update the 
QT path to your local QT installation path. Next, in the configuration panel of the `Hello2D` target
in CLion, set the local QT DLL library path in the `Environment Variables` field, 
e.g., `PATH=C:\Qt\6.6.1\msvc2019_64\bin`. Finally, you can build and run the `Hello2D` target.


### OpenHarmony

The OpenHarmony demo project requires the **[DevEco](https://developer.huawei.com/consumer/cn/deveco-studio/)**
IDE, which comes with the OpenHarmony SDK pre-installed. To get started, open the `ohos/` directory
in DevEco. Then, go to `Preferences` > `Build, Execution, Deployment` > `Build Tools` > `Hvigor` and
uncheck the `Execute tasks in parallel mode (may require larger heap size)` option to avoid issues
with building third-party libraries.

Alternatively, you can manually build the third-party libraries by running the following command in
the root directory:

```
node build_vendor -p ohos
```

This command will build the third-party libraries for the OpenHarmony platform and cache them in
the `third_party/out/` directory.

Finally, connect your OpenHarmony device or start the simulator, then build and run the `hello2d`
target in DevEco. You're all set!


## Vcpkg Integration

TGFX provides official vcpkg port files for easy integration into projects using vcpkg dependency management. Due to maintenance costs, we haven't merged into the official vcpkg repository yet, but this may be considered in the future. Currently, you can use TGFX through manual vcpkg port configuration.

### Quick Start

1. Visit the [TGFX releases page](https://github.com/Tencent/tgfx/releases) and download the vcpkg port files for your target version
2. Copy the `tgfx/` directory to your vcpkg installation's `ports/` directory  
3. Run `vcpkg install tgfx` to install

### Using Specific Commit

If you need a specific commit version of TGFX, you can use the provided script in the root directory to generate the port configuration:

```bash
node update_vcpkg <commit-hash>
```

Example:
```bash
node update_vcpkg 6095b909b1109d4910991a034405f4ae30d6786f
```

The script will automatically download the source code, calculate the SHA512 hash, and update the `vcpkg/ports/tgfx/portfile.cmake` file.

## Build Library

Aside from directly integrating the source code of tgfx into your project, you also have the option
of linking with the precompiled libraries. TGFX uses the [**vendor\_tools**](https://github.com/libpag/vendor_tools) 
project as its build system, providing a unified way to build the tgfx library across all platforms.

To get started quickly, run the following command in the root directory:

```
node build_tgfx
```

This command builds the release version of the tgfx library for the native platform. After running
it, you will find the compiled tgfx libraries in the `out/release` directory. To target a specific 
platform, use the `-p [--platform]` option. Supported platforms are: `win`, `mac`, `ios`, `linux`, 
`android`, `web`, `ohos`.

```
node build_tgfx -p ios
```

For Apple platforms, you have the convenient `-x [--xcframework]` option available. This option 
enables you to effortlessly create xcframeworks:

```
node build_tgfx -p mac -x
```

After running the command, you will find the `tgfx.xcframework` in the `out/release/mac` directory.

You can also pass CMake options using the `-D` prefix. For example, to build tgfx with FreeType 
support enabled, run:

```
node build_tgfx -DTGFX_USE_FREETYPE=ON
```

For more details and options, run the command with the `-h` or `--help` flag:

```
node build_tgfx -h
```


## Contribution

If you have any ideas or suggestions to improve TGFX, feel free to start a 
[discussion](https://github.com/Tencent/tgfx/discussions/new/choose), open an [issue](https://github.com/Tencent/tgfx/issues/new/choose), 
or submit a [pull request](https://github.com/Tencent/tgfx/pulls). Before doing so, please read our
[Contributing Guide](./CONTRIBUTING.md).

## Support Us

If you find TGFX helpful, please give us a **Star**. We really appreciate your support :)

[![Star History Chart](https://api.star-history.com/svg?repos=Tencent/tgfx&type=Date)](https://star-history.com/#Tencent/tgfx&Date)

## License

TGFX is licensed under the [BSD-3-Clause License](./LICENSE.txt)

The copyright notice pertaining to the Tencent code in this repo was previously in the name of "THL 
A29 Limited". That entity has now been de-registered. You should treat all previously distributed
copies of the code as if the copyright notice was in the name of "Tencent".


