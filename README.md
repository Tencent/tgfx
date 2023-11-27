<img src="resources/readme/TGFX.jpg" alt="TGFX Logo" width="992"/>

[![license](https://img.shields.io/badge/license-BSD--3--Clause-blue)](https://github.com/Tencent/tgfx/blob/master/LICENSE.txt)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/Tencent/tgfx/pulls)
[![codecov](https://codecov.io/gh/Tencent/tgfx/branch/main/graph/badge.svg)](https://codecov.io/gh/Tencent/tgfx)
[![autotest](https://github.com/Tencent/tgfx/actions/workflows/autotest.yml/badge.svg?branch=main)](https://github.com/Tencent/tgfx/actions/workflows/autotest.yml)
[![build](https://github.com/Tencent/tgfx/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/Tencent/tgfx/actions/workflows/build.yml)
[![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/Tencent/tgfx)](https://github.com/Tencent/tgfx/releases)

## Introduction

TGFX (Tencent Graphics) is a lightweight 2D graphics library designed for rendering texts,
geometries, and images. It provides high-performance APIs that work across a variety of GPU hardware
and software platforms, including iOS, Android, macOS, Windows, Linux, Web, and more. TGFX was
originally designed to serve as the default graphics engine for the [PAG](https://pag.art) project
starting from version 4.0. Its main objective is to offer a compelling alternative to the Skia graphics
library while maintaining a much smaller binary size. Over time, it has found its way into many other
products, such as [Hippy](https://github.com/Tencent/Hippy), [Tencent Docs](https://docs.qq.com) and
various video-editing apps.

## Platform Support

- iOS 9.0 or later
- Android 4.4 or later
- macOS 10.15 or later
- Windows 7.0 or later
- Chrome 69.0 or later (Web)
- Safari 11.3 or later (Web)

## Backing Renderers

| Vector Backend |  GPU Backend   |      Target Platforms        |    Status     |
|:--------------:|:--------------:|:----------------------------:|:-------------:|
|    FreeType    |  OpenGL        |  All                         |   complete    |
|  CoreGraphics  |  OpenGL        |  iOS, macOS                  |   complete    |
|    Canvas2D    |  WebGL         |  Web                         |   complete    |
|  CoreGraphics  |  Metal         |  iOS, macOS                  |  in progress  |
|    FreeType    |  Vulkan        |  Android, Linux              |    planned    |


## Branch Management

- The `main` branch is our active developing branch which contains the latest features and bugfixes.
- The branches under `release/` are our stable milestone branches which are fully tested. We will
  periodically cut a `release/{version}` branch from the `main` branch. After one `release/{version}`
  branch is cut, only high-priority fixes are checked into it.

## Build Prerequisites

TGFX utilizes the **C++17** features for development. Below are the minimum tools needed for building tgfx on different platforms:

- Xcode 11.0+
- GCC 8.0+
- Visual Studio 2019+
- NodeJS 14.14.0+
- Ninja 1.9.0+
- CMake 3.13.0+
- QT 5.13.0+
- NDK 19+ (**19.2.5345600 recommended**)
- Emscripten 3.1.20+ (**3.1.20 recommended**)


Please pay attention to the following additional notices:

- Make sure you have installed at least the **[Desktop development with C++]** and **[Universal Windows Platform development]** components for VS2019.
- It is **highly recommended** to use the **latest version of CMake**, Numerous outdated versions of CMake may carry various bugs across different platforms.

## Dependencies

TGFX uses [**depsync**](https://github.com/domchen/depsync) tool to manage third-party dependencies.

**For macOS platform：**

Run the script in the root of the project:

```
./sync_deps.sh
```

This script will automatically install the necessary tools and synchronize all third-party repositories.

**For other platforms：**

First, make sure you have installed the latest version of node.js (You may need to restart your
computer after this step). And then run the following command to install depsync tool:

```
npm install -g depsync
```

And then run `depsync` in the root directory of the project.

```
depsync
```

Git account and password may be required during synchronizing. Please make sure you have enabled the
`git-credential-store` so that `CMakeList.txt` can trigger synchronizing automatically next time.


## Getting Started

We offer concise demos for different platforms, demonstrating how to integrate the tgfx library into
your project. Once you've built the project, you'll find a straightforward app rendering various test
cases from the `drawers/` directory. These test cases include rendering shapes, images, and simple 
texts. With a simple touch on the screen, you can switch between different test cases. If you are 
looking for further guidance on API usage, consider exploring the test cases found in the `test/` 
directories. They may provide valuable insights and assistance.

Before you begin building the demo projects, please make sure to carefully follow the instructions
provided above in the [**Build Prerequisites**](#build-prerequisites) and 
[**Dependencies**](#dependencies) sections. They will guide you through the 
necessary steps to configure your development environment.

### Android Demo

The android demo project requires the **Android NDK**. We recommend using the **19.2.5345600** 
version, which has been fully tested with the tgfx library. You can download it from 
[NDK Downloads](https://developer.android.com/ndk/downloads) or by Android Studio.

If you choose to manually download the Android NDK, please extract it to the default location. 
On macOS, this would be：

```
/Users/yourname/Library/Android/sdk/ndk/19.2.5345600
```

On Windows, it would be：

```
C:\Users\yourname\AppData\Local\Android\Sdk\ndk\19.2.5345600
```

Alternatively, you can set one of 
the following environment variables for tgfx to locate the NDK: 

```
["ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK", "NDK_HOME", "NDK_ROOT", "NDK_PATH"]
```

To get started, open the `android/` directory in Android Studio, and you'll be all set! If you 
encounter any issues during Gradle synchronization, please ensure that you haven't accidentally 
clicked on the pop-up hints for Gradle version upgrades. If you have, undo the changes you made to 
the project and attempt synchronization again. If the issue is related to your IDE configuration,
please search for a solution on Google. However, if you believe the problem is associated with the 
project configuration, you can open an [Issue](https://github.com/Tencent/tgfx/issues/new/choose)
to address it.

### iOS Demo

Run the following command in the `ios/` directory or double-click on it:

```
./gen_ios
```

This will generate an XCode project for iPhone devices. If you prefer to generate a project for 
the simulators, use the following command instead:

```
./gen_simulator
```

This will generate a simulator project for the native architecture, for example, `arm64` for
Apple Silicon Macs and `x64` for Intel Macs. If you want to generate a project for the specific
architecture, you can use the `-a` option:

```
./gen_simulator -a x64
```

Additionally, you can pass cmake options using the `-D` option. For instance, if you want to 
generate a project with webp encoding support, please run the following command:

```
./gen_ios -DTGFX_USE_WEBP_ENCODE=ON
```

Finally, open XCode and launch the `ios/Hello2D.xcworkspace` to build and run the demo project.

### macOS Demo


Run the following command in the `mac/` directory or double-click on it:

```
./gen_mac
```

This will generate a project for the native architecture, for example, `arm64` for Apple Silicon
Macs and `x64` for Intel Macs. If you want to generate a project for the specific architecture, you
can use the `-a` option, for example:

```
./gen_mac -a x64
```

Additionally, you can pass cmake options using the `-D` option. For example, if you want to generate
a project with freetype support, please run the following command:

```
./gen_mac -DTGFX_USE_FREETYPE=ON
```

At last, launch XCode and open the `mac/Hello2D.xcworkspace`. You'll be ready to go!

### Web Demo

The web demo project requires the **Emscripten SDK**. You can download and install
it from the [official website](https://emscripten.org/). We recommend using the **3.1.20** version,
which has been fully tested with the tgfx library.

To begin, navigate to the `web/` directory and execute the following command to install the 
necessary node modules:

```
npm install
```

And then run the following command in the `web/` directory to build the demo project:

```
npm run build
```

This will generate `hello2d.js` and `hello2d.wasm` files into the `web/demo/wasm` directory. 
Afterward, you can start an HTTP server by running the following command:

```
npm run server
```

This will open [http://localhost:8081/web/demo/index.html](http://localhost:8081/web/demo/index.html)
in your default browser. You can also open it manually to see the demo.

To debug the C++ code, ensure that you have installed the browser plugin:
[**C/C++ DevTools Support (DWARF)**](https://chromewebstore.google.com/detail/cc++-devtools-support-dwa/pdcpmagijalfljmkmjngeonclgbbannb).
Next, open Chrome DevTools and navigate to Settings > Experiments. Check the option
**WebAssembly Debugging: Enable DWARF support** to enable SourceMap support.

And then, replace the previous build command with the following:

```
npm run build:debug
```

With these steps completed, you will be able to debug C++ files directly within Chrome DevTools.

Additionally, please note that when using `ESModule` for your project, it is necessary to manually 
pack the generated `.wasm` file into the final web program. This is because common packing tools 
usually ignore the `.wasm` file. Moreover, remember to upload the `.wasm` file to a server, enabling 
users to access it from the network.


## Build TGFX

Aside from directly integrating the source code of tgfx into your project, you also have the option
of linking with the precompiled libraries. TGFX utilizes the [**vendor_tools**](https://github.com/libpag/vendor_tools)
project as its build system, enabling a unified approach to build the tgfx library across all platforms.

To quickly get started, execute the following command in the root directory:

```
node build_tgfx
```

This command will build the release version of the tgfx library for the native platform. After the
execution, you will find the compiled tgfx libraries in the `out/release` directory. If you wish to
target a specific platform, please use the `-p [--platform]` option. The supported platform names
are as follows: `win`, `mac`, `ios`, `linux`, `android`, `web`.

```
node build_tgfx -p ios
```

When developing for apple platforms, you have the convenient `-x [--xcframework]` option available. 
This option enables you to effortlessly create xcframeworks while also generating the corresponding 
`dSYM` symbol files.

```
node build_tgfx -p mac -x
```

After the execution, you will find the `tgfx.xcframework` in the `out/release/mac` directory.

Additionally, you can pass cmake options using the `-D` prefix. For example, if you want to build
tgfx with the freetype option enabled, please run the following command:

```
node build_tgfx -DTGFX_USE_FREETYPE=ON
```

To access more details and options, execute the command along with the `-h [--help]` option:

```
node build_tgfx -h
```

## Development

We recommend using the [**CLion**](https://www.jetbrains.com/clion/) IDE on the macOS platform for 
development. After synchronizing with `depsync`, there are a few configurations that need to be set
up before opening the project in CLion.

### macOS

No additional configurations of CLion are necessary. **Please note that currently, the unit test 
targets for tgfx can only be accessed on macOS**.

### Windows

Open the **File->Setting** panel, and go to **Build, Execution, Deployment->ToolChains**, then set 
the toolchain of CLion to **Visual Studio** with **amd64 (Recommended)** or **x86** architecture.

### Web

If you wish to target the web platform, you'll need to install the **Emscripten SDK** and follow 
these steps:

- Open the `Settings` panel in CLion and go to `Build, Execution, Deployment` > `CMake`. Create a new build target.
- Set the `CMake options` to the following value:

```
DCMAKE_TOOLCHAIN_FILE="path/to/emscripten/emscripten/version/cmake/Modules/Platform/Emscripten.cmake"
```

- After creating the build target, modify the `Configurations` located before the `Run / Debug` buttons to match the newly created build target.

Once you've completed these steps, you'll have the ability to build the tgfx library and conduct debugging in CLion.


## Contribution

If you have any ideas or suggestions to improve tgfx, welcome to open
a [discussion](https://github.com/Tencent/tgfx/discussions/new/choose)
/ [issue](https://github.com/Tencent/tgfx/issues/new/choose)
/ [pull request](https://github.com/Tencent/tgfx/pulls). Before making a pull request or issue,
please make sure to read [Contributing Guide](./CONTRIBUTING.md).

## Support Us

If you find tgfx is helpful, please give us a **Star**. We sincerely appreciate your support :)

[![Star History Chart](https://api.star-history.com/svg?repos=Tencent/tgfx&type=Date)](https://star-history.com/#Tencent/tgfx&Date)

## License

TGFX is licensed under the [BSD-3-Clause License](./LICENSE.txt)

