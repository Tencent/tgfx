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
- Visual Studio 2019
- NodeJS 14.14.0+
- Ninja 1.9.0+
- CMake 3.13.0+
- NDK 19.2.5345600

Please pay attention to the following additional notices:

- Make sure you have installed at least the **[Desktop development with C++]** and **[Universal Windows Platform development]** components for VS2019.
- It is highly recommended to **use the latest version of CMake**, Numerous outdated versions of CMake may carry various bugs across different platforms.
- Please use NDK version **`19.2.5345600`** to avoid potential failures. You can place the NDK in the default location or set environment variables
  (`NDK_HOME`, `NDK_PATH`, `ANDROID_NDK_HOME`, `ANDROID_NDK`) for tgfx to locate it.

## Dependency Management

TGFX uses [depsync](https://github.com/domchen/depsync) tool to manage third-party dependencies.

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

## Build TGFX

TGFX uses a set of [build tools](https://github.com/libpag/vendor_tools) written in NodeJS, enabling
a unified approach to build tgfx across all platforms.

To quickly get started, simply execute the following command in the root directory:

```
node build_tgfx
```

This command will build the release version of the tgfx library for the native platform. After the
execution, you will locate the compiled tgfx libraries in the `out/release` directory. If you wish to
target a specific platform, simply use the `-p [--platform]` option. The supported platform names
are as follows: `win`, `mac`, `ios`, `linux`, `android`, `web`.

```
node build_tgfx -p ios
```

When working with apple platforms, you have the additional `-x [--xcframework]` option at your disposal,
which allows you to effortlessly create xcframeworks.

```
node build_tgfx -p mac -x
```

After the execution, you will locate the `tgfx.xcframework` in the `out/release/mac` directory.

Additionally, you can pass cmake options using the `-D` prefix. For example, if you want to build
tgfx with the freetype option enabled, please run the following command:

```
node build_tgfx -DTGFX_USE_FREETYPE=ON
```

To access additional details and options, execute the command along with the `-h [--help]` option:

```
node build_tgfx -h
```

## Integration

We offer concise demos for different platforms, demonstrating the seamless integration of the tgfx
library into your project. For more information, please refer to the README.md documentation within
each demo directory. If you are looking for guidance on API usage, consider exploring the test cases 
found in the `test/` and `drawers/` directories. They may provide valuable insights and assistance.

## Development

We recommend using CLion IDE on the macOS platform for development. After the synchronization, you
can open the project with CLion and build the tgfx library.

**For macOS platform：**

- There are no extra configurations of CLion required.

**For Windows platform：**

- Open the **File->Setting** panel, and go to **Build, Execution, Deployment->ToolChains**, then set the toolchain of CLion to **Visual Studio** with **amd64 (Recommended)** or **x86** architecture.

And then, launch CLion and open the tgfx project. You'll be good to go!


## Support Us

If you find tgfx is helpful, please give us a **Star**. We sincerely appreciate your support :)

[![Star History Chart](https://api.star-history.com/svg?repos=Tencent/tgfx&type=Date)](https://star-history.com/#Tencent/tgfx&Date)

## License

TGFX is licensed under the [BSD-3-Clause License](./LICENSE.txt)

## Contribution

If you have any ideas or suggestions to improve tgfx, welcome to open
a [discussion](https://github.com/Tencent/tgfx/discussions/new/choose)
/ [issue](https://github.com/Tencent/tgfx/issues/new/choose)
/ [pull request](https://github.com/Tencent/tgfx/pulls). Before making a pull request or issue,
please make sure to read [Contributing Guide](./CONTRIBUTING.md).
