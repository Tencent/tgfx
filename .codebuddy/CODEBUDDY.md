# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

@./rules/Rules.md

## Project Overview

TGFX (Tencent Graphics) is a lightweight 2D graphics library for modern GPUs, providing high-performance rendering of text, images, and vector graphics. It serves as an alternative to Skia with a smaller binary size and supports multiple platforms (iOS, Android, macOS, Windows, Linux, OpenHarmony, Web).

## Quick Start Commands

### Sync Dependencies
```bash
./sync_deps.sh          # macOS
npm install -g depsync && depsync  # Other platforms
```

### Build & Test (macOS - Primary Development)
```bash
# Build with tests enabled (REQUIRED for full module compilation)
mkdir build && cd build
cmake -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
cmake --build . --target TGFXFullTest -- -j 12

# Run all tests
./TGFXFullTest

# Run specific test case
./TGFXFullTest --gtest_filter=CanvasTest.drawRect

# Run tests matching pattern
./TGFXFullTest --gtest_filter=Canvas*

# Run with JSON output
./TGFXFullTest --gtest_output=json:TGFXFullTest.json

# Quick autotest (builds and runs)
./autotest.sh
```

### Code Formatting (REQUIRED before commit)
```bash
./codeformat.sh
```
Run this before every commit. Ignore any error output - the script completes formatting regardless of reported errors.

### Platform-Specific Project Generation
```bash
# macOS
cd mac && ./gen_mac

# iOS device
cd ios && ./gen_ios

# iOS simulator
cd ios && ./gen_simulator

# Pass CMake options
cd mac && ./gen_mac -DTGFX_USE_FREETYPE=ON
```

### Web Development
```bash
cd web
npm install
npm run build        # Build multithreaded
npm run build:debug  # Build with debug symbols
npm run server       # Start dev server
```

### Build Library
```bash
node build_tgfx              # Native platform
node build_tgfx -p ios       # Specific platform
node build_tgfx -p mac -x    # Create xcframework
```

## Architecture Overview

### Source Structure (`/src/`)
- **core/**: Core graphics API (Canvas, Paint, Path, Shape, Image, TextBlob, Font)
- **gpu/**: GPU backend abstraction, render ops, processors, resource management
  - `opengl/`: OpenGL implementations per platform (cgl, eagl, egl, wgl, webgl)
- **layers/**: Optional compositing layer system (`TGFX_BUILD_LAYERS`)
- **svg/**: Optional SVG support (`TGFX_BUILD_SVG`)
- **pdf/**: Optional PDF export (`TGFX_BUILD_PDF`)
- **platform/**: Platform-specific code (android, apple, ohos, web, mock)

### Public API (`/include/tgfx/`)
All public headers with detailed API documentation.

## Testing

- Location: `/test/src/*Test.cpp`, based on Google Test framework
- Test code can access all private members via compile flags (no friend class needed)

### Screenshot Comparison

Use `Baseline::Compare(pixels, key)` for visual regression testing:
- **Key format**: `{TestClass}/{CaseName}`, e.g., `CanvasTest/Clip`
- **Output**: `test/out/{TestClass}/{CaseName}.webp`
- **Baseline**: `test/out/{TestClass}/{CaseName}_base.webp`
- **Version control**: Compares `test/baseline/version.json` (repo) vs `.cache/version.json` (local). Only compares images when versions match; mismatched versions skip comparison and return success (allowing screenshot changes)

### Updating Baselines

Copy `test/out/version.json` to `test/baseline/` to accept changes. **Requirements**:
1. Must use output from a FULL `TGFXFullTest` run (all test cases)
2. User must explicitly confirm accepting all screenshot changes

**Note**: `update_baseline.sh` syncs baseline to local cache. CMake warns on version mismatch. **NEVER run automatically**

## Key CMake Options

| Option | Description |
|--------|-------------|
| `TGFX_BUILD_TESTS` | Build test targets (REQUIRED for full compilation) |
| `TGFX_BUILD_LAYERS` | Enable layers module |
| `TGFX_BUILD_SVG` | Enable SVG module |
| `TGFX_BUILD_PDF` | Enable PDF module |
| `TGFX_USE_FREETYPE` | Use FreeType for fonts (default ON for non-Apple) |
| `TGFX_USE_SWIFTSHADER` | Software rendering |

## File Conventions

- Headers: `.h`, Implementation: `.cpp`, Objective-C++: `.mm`
- Test files: `*Test.cpp` in `/test/src/`
- Source files auto-collected via `file(GLOB)` - no CMake changes needed for new files
