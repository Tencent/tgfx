# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

@./rules/Coding.md
@./rules/Commit.md
@./rules/Review.md
@./rules/Testing.md

## Project Overview

TGFX (Tencent Graphics) is a lightweight 2D graphics library for modern GPUs, providing high-performance rendering of text, images, and vector graphics. It serves as an alternative to Skia with a smaller binary size and supports multiple platforms (iOS, Android, macOS, Windows, Linux, OpenHarmony, Web).

## Build System & Dependencies

### Dependency Management
- **depsync**: Third-party dependencies are managed via the `depsync` tool (Node.js-based)
- **vendor_tools**: Build system from `vendor_tools` project provides unified builds across platforms
- Run `./sync_deps.sh` (macOS) or `depsync` command to synchronize third-party dependencies
- Dependencies are stored in `third_party/` directory

### Common Build Commands

#### macOS/iOS Development
```bash
# Generate iOS device project
cd ios && ./gen_ios

# Generate iOS simulator project (native arch)
cd ios && ./gen_simulator

# Generate simulator for specific arch
cd ios && ./gen_simulator -a x64

# Generate macOS project (native arch)
cd mac && ./gen_mac

# Generate macOS with specific arch
cd mac && ./gen_mac -a x64

# Pass CMake options
cd mac && ./gen_mac -DTGFX_USE_FREETYPE=ON
```

#### Web/Emscripten
```bash
cd web

# Install dependencies
npm install

# Build multithreaded version (wasm-mt)
npm run build

# Build with debug symbols
npm run build:debug

# Start dev server (multithreaded)
npm run server

# Build single-threaded version
npm run build:st

# Start dev server (single-threaded)
npm run server:st

# Development mode with watch
npm run dev
```

#### Linux
```bash
cd linux

# Configure with Release build
cmake -B ./build -DCMAKE_BUILD_TYPE=Release

# Build with parallel jobs
cmake --build ./build -- -j 12
```

#### Windows (Visual Studio)
```bash
# x64 Debug configuration
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug" -B ./Debug-x64

# x86 Release configuration
cmake -G "Visual Studio 16 2019" -A Win32 -DCMAKE_CONFIGURATION_TYPES="Release" -B ./Release-x86
```

#### Library Building
```bash
# Build release library for native platform
node build_tgfx

# Build for specific platform
node build_tgfx -p ios

# Create xcframework (Apple platforms)
node build_tgfx -p mac -x

# Pass CMake options
node build_tgfx -DTGFX_USE_FREETYPE=ON

# Show all options
node build_tgfx -h
```

### Testing

#### Run Full Test Suite
```bash
# Run autotest (builds and runs TGFXFullTest)
./autotest.sh

# Run with SwiftShader (software rendering)
./autotest.sh USE_SWIFTSHADER
```

#### Build and Run Tests Manually
```bash
mkdir build && cd build

# Configure with tests enabled
cmake -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../

# Build test target
cmake --build . --target TGFXFullTest -- -j 12

# Run tests with JSON output
./TGFXFullTest --gtest_output=json:TGFXFullTest.json
```

#### Update Baseline Images
```bash
# Update test baseline images
./update_baseline.sh
```

### Code Formatting
```bash
# Format all source files with clang-format
./codeformat.sh

# Requires clang-format version 14
# Script will auto-install via pip3 on macOS
```

### Code Coverage
```bash
# Generate coverage report
./codecov.sh
```

## Architecture

### Core Module Structure

#### `/src/core` - Core Graphics API
- **Canvas**: Main drawing interface (similar to HTML5 Canvas or Skia Canvas)
- **Paint/Brush**: Styling and fill/stroke properties
- **Path**: Vector path representation
- **Shape**: Higher-level geometric shapes
- **Image**: Raster image representation
- **TextBlob/GlyphRun**: Text rendering primitives
- **Font/Typeface**: Font management
- **filters/**: Image filters and effects (blur, color matrix, etc.)
- **images/**: Image codecs and encoding/decoding
- **shapes/**: Shape implementations (rect, round rect, etc.)
- **shaders/**: Shader implementations (gradients, image shaders)
- **utils/**: Utility classes (matrices, data structures)
- **vectors/**: Vector text rendering (FreeType integration)
- **codecs/**: Image format codecs (PNG, JPEG, WebP)

#### `/src/gpu` - GPU Backend
- **Context/Device**: GPU device abstraction and context management
- **Surface**: Render targets
- **ops/**: Draw operations (render ops that get batched and executed)
- **processors/**: Fragment and geometry processors
- **resources/**: GPU resource management (textures, buffers, render targets)
- **tasks/**: Asynchronous GPU tasks
- **proxies/**: Lazy resource proxies
- **glsl/**: GLSL shader generation
- **opengl/**: OpenGL backend implementation
  - `cgl/`: Core OpenGL (macOS)
  - `eagl/`: OpenGL ES (iOS)
  - `egl/`: EGL (Android, Linux)
  - `wgl/`: Windows OpenGL
  - `webgl/`: WebGL
  - `qt/`: Qt OpenGL integration

#### `/src/layers` - Layer System
- Compositing layer system for advanced composition (optional module)
- Enabled via `TGFX_BUILD_LAYERS` CMake option

#### `/src/svg` - SVG Support
- SVG rendering and export functionality (optional module)
- Enabled via `TGFX_BUILD_SVG` CMake option

#### `/src/pdf` - PDF Support
- PDF document generation and export (optional module)
- Enabled via `TGFX_BUILD_PDF` CMake option

#### `/src/platform` - Platform-Specific Code
- `android/`: Android-specific implementations
- `apple/`: iOS/macOS shared code
- `ohos/`: OpenHarmony platform support
- `web/`: Web/Emscripten platform support
- `mock/`: Mock implementations for testing

#### `/src/inspect` - Inspector/Profiling
- Performance profiling and debugging tools
- Enabled in Debug builds via `TGFX_USE_INSPECTOR` option

### Public API Headers

All public headers are in `/include/tgfx/`:
- `core/`: Core graphics API
- `gpu/`: GPU context and device management
- `layers/`: Layer system API
- `svg/`: SVG export API
- `pdf/`: PDF export API
- `platform/`: Platform utilities

### Demo Applications

Each platform has a demo app in its respective directory:
- **iOS**: `ios/Hello2D/` - Touch to cycle through test cases
- **Android**: `android/app/` - Gradle-based Android app
- **macOS**: `mac/Hello2D/` - Native macOS app
- **Web**: `web/demo/` - WebGL/WebAssembly demo
- **Linux**: `linux/src/` - X11-based demo
- **Windows**: `win/src/` - Win32 demo
- **Qt**: `qt/` - Qt-based cross-platform demo
- **OpenHarmony**: `ohos/hello2d/` - HarmonyOS app

Demo apps render test cases from `/drawers/src/` directory.

### Test Infrastructure

- **Test Framework**: Google Test (googletest)
- **Test Files**: `/test/src/*Test.cpp`
- **Test Utilities**: `/test/src/utils/`
  - `Baseline.h/cpp`: Image baseline comparison
  - `DevicePool.h/cpp`: GPU device management for tests
  - `TestUtils.h/cpp`: Common test utilities
  - `ContextScope.h/cpp`: RAII GPU context management
  - `TextShaper.h/cpp`: Text shaping utilities
- **Baseline Images**: `/test/baseline/` - Reference images for visual tests
- **Test Output**: `/test/out/` - Generated test output images

## CMake Build Options

Key CMake options for configuring TGFX builds:

### Module Selection
- `TGFX_BUILD_SVG`: Enable SVG module (default: OFF)
- `TGFX_BUILD_PDF`: Enable PDF module (default: OFF)
- `TGFX_BUILD_LAYERS`: Enable layers module (default: OFF)
- `TGFX_BUILD_DRAWERS`: Build tgfx-drawers test library (default: OFF)
- `TGFX_BUILD_TESTS`: Build test targets (default: ON for macOS CLion, OFF otherwise)
- `TGFX_BUILD_FRAMEWORK`: Build as framework on Apple platforms (default: OFF)

### Backend Options
- `TGFX_USE_OPENGL`: Use OpenGL GPU backend (default: ON)
- `TGFX_USE_QT`: Enable Qt framework support (default: OFF)
- `TGFX_USE_SWIFTSHADER`: Use SwiftShader for software rendering (default: OFF)
- `TGFX_USE_ANGLE`: Use ANGLE library (default: OFF)

### Feature Options
- `TGFX_USE_THREADS`: Enable multithreaded rendering (default: ON, except single-threaded Web)
- `TGFX_USE_FREETYPE`: Use FreeType for vector fonts (default: ON for non-Apple platforms)
- `TGFX_USE_TEXT_GAMMA_CORRECTION`: Enable text gamma correction (default: OFF)
- `TGFX_USE_INSPECTOR`: Enable profiling/debugging tools (default: ON in Debug builds)

### Image Format Support
- `TGFX_USE_PNG_DECODE/ENCODE`: PNG support
- `TGFX_USE_JPEG_DECODE/ENCODE`: JPEG support
- `TGFX_USE_WEBP_DECODE/ENCODE`: WebP support

Defaults vary by platform - see CMakeLists.txt lines 48-57 for platform-specific defaults.

## Development Workflow

### Code Style
- Follow [Google C++ Style Guide](http://zh-google-styleguide.readthedocs.io/en/latest/google-cpp-styleguide/)
- Use 2 spaces for indentation (no tabs)
- Run `./codeformat.sh` before committing
- Configuration in `.clang-format` (based on Google style with 100 char line limit)

### Branch Strategy
- `main`: Active development branch - submit PRs here
- `release/{version}`: Stable release branches - only high-priority fixes

### Contributing
1. Fork repo and create branch from `main`
2. Update documentation if APIs changed
3. Add BSD-3-Clause copyright notice to new files
4. Run code formatting: `./codeformat.sh`
5. Run tests and verify they pass
6. Submit PR to `main` branch

### Platform-Specific Notes

#### Android
- Requires NDK 20+ (20.1.5948944 recommended)
- Open `android/` directory in Android Studio
- Avoid clicking Gradle version upgrade prompts

#### Web/Emscripten
- Requires Emscripten 3.1.58+
- Install: `brew install emscripten` (macOS)
- Multithreaded builds require special handling: if renaming output files, replace all "hello2d.js" references inside the JS file itself
- Enable C++ debugging: Install Chrome DevTools DWARF extension and use `npm run build:debug`

#### Linux
- Requires X11 development headers: `yum install libX11-devel --nogpg`
- Uses SwiftShader for software GPU emulation

#### Windows
- Use Visual Studio 2019+ with "Desktop development with C++" and "Universal Windows Platform development"
- Recommended: Use Ninja generator in CLion for faster builds
- Use Native Tools Command Prompt for command-line builds

#### Qt
- Update `qt/QTCMAKE.cfg` with local Qt installation path
- Windows: Set `PATH` environment variable to include Qt DLLs in run configuration

## Key Third-Party Dependencies

Located in `third_party/`:
- **googletest**: Test framework
- **freetype**: Vector font rendering
- **harfbuzz**: Text shaping
- **libpng**: PNG codec
- **libjpeg-turbo**: JPEG codec
- **libwebp**: WebP codec
- **pathkit**: Path operations
- **flatbuffers**: Serialization (for inspector)
- **lz4**: Compression (for inspector)
- **skcms**: Color management
- **vendor_tools**: Build system utilities

## File Naming Conventions

- Header files: `.h` extension
- Implementation files: `.cpp` extension
- Platform-specific Objective-C: `.mm` (implementation), `.m` (Objective-C only)
- Test files: `*Test.cpp` in `/test/src/`
