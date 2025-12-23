# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

@./rules/Rules.md

## Project Overview

TGFX (Tencent Graphics) is a lightweight 2D graphics library for modern GPUs, providing high-performance rendering of text, images, and vector graphics. It serves as an alternative to Skia with a smaller binary size and supports multiple platforms (iOS, Android, macOS, Windows, Linux, OpenHarmony, Web).

### Source Structure

- `/include/tgfx/`: Public API headers
- `/src/core/`: Core implementation (Canvas, Paint, Path, Shape, Image, TextBlob, Font)
- `/src/gpu/`: GPU rendering
  - `ops/`: DrawOp implementations
  - `processors/`: Processor implementations
  - `proxies/`: Proxy implementations
  - `resources/`: Resource implementations
  - `opengl/`: OpenGL backend per platform (cgl, eagl, egl, wgl, webgl)
- `/src/layers/`: Layers module
- `/src/svg/`: SVG module
- `/src/pdf/`: PDF module
- `/src/platform/`: Platform-specific code (android, apple, ohos, web, mock)
- `/test/src/`: Test files (`*Test.cpp`)

File extensions: `.h` (headers), `.cpp` (implementation), `.mm` (Objective-C++). Source files are auto-collected via `file(GLOB)` - no CMake changes needed for new files.

### Core API Classes

- **Device** - Platform-specific GPU interface abstraction
- **Window** - Native window wrapper for rendering
- **Context** - GPU resource and command container, manages drawing pipeline
- **Canvas** - Drawing interface with transform/clip stack
- **Surface** - Pixel buffer management, creates Canvas for drawing
- **Picture** - Recorded Canvas drawing commands for replay
- **Paint** - Drawing style composed of Brush (color, shader, blend, filter) and Stroke (width, cap, join)
- **Shader** - Color generation (gradients, patterns)
- **Path** - Geometric path representation
- **PathEffect** - Path modification (dash, corner, etc.)
- **Shape** - Deferred Path with delayed computation and GPU caching for repeated drawing
- **Image** - Immutable pixel/texture data
- **ImageCodec** - Image encoding/decoding
- **Bitmap** - Mutable pixel buffer
- **Pixmap** - Pixel data view with ImageInfo
- **ImageFilter/MaskFilter/ColorFilter** - Image post-processing
- **Typeface** - Font file representation
- **Font** - Typeface with size and styling
- **TextBlob** - Immutable text layout result for drawing

### Rendering Pipeline

```
Canvas API → RenderContext → OpsCompositor → DrawOp → Processor → RenderTask → GPU
```

- **DrawOp**: Drawing operations (RectDrawOp, ShapeDrawOp, AtlasTextOp, etc.)
- **Processor**: Shader code generation (FragmentProcessor, XferProcessor)
- **Proxy**: Deferred resource allocation (TextureProxy, RenderTargetProxy)
- **Resource**: GPU resources (Texture, RenderTarget, Buffer) with key-based caching

### Optional Modules

- **Layers**: Layer tree rendering system with DisplayList, supports layer hierarchy, transforms, masks, filters, and layer styles. Render modes: Direct, Partial (dirty region), Tiled (for zoom/scroll)
- **SVG**: SVG parsing (SVGDOM) and export (SVGExporter). Supports DOM traversal, rendering to Canvas, and Canvas-to-SVG conversion
- **PDF**: PDF document export via PDFDocument. Supports multi-page creation, metadata, font embedding, and image compression


## Build Commands

### Sync Dependencies
```bash
./sync_deps.sh          # macOS
npm install -g depsync && depsync  # Other platforms
```

### Build Verification

After modifying code, use this command to verify compilation. Must pass `-DTGFX_BUILD_TESTS=ON` to enable ALL modules (layers, svg, pdf, etc.).

```bash
cmake -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest -- -j 12
```

### Code Formatting (REQUIRED before commit)
```bash
./codeformat.sh
```
Run this before every commit. Ignore any error output - the script completes formatting regardless of reported errors.

## Testing

- Location: `/test/src/*Test.cpp`, based on Google Test framework
- Test code can access all private members via compile flags (no friend class needed)

### Run Tests
```bash
./cmake-build-debug/TGFXFullTest                                        # Run all tests
./cmake-build-debug/TGFXFullTest --gtest_filter=CanvasTest.drawRect     # Run specific test
./cmake-build-debug/TGFXFullTest --gtest_filter=Canvas*                 # Run pattern match
```


### Screenshot Tests

- Use `Baseline::Compare(pixels, key)` where key format is `{folder}/{name}`, e.g., `CanvasTest/Clip`
- Screenshots output to `test/out/{folder}/{name}.webp`, baseline is `{name}_base.webp` in same directory
- Comparison mechanism: compares version numbers in `test/baseline/version.json` (repo) vs `.cache/version.json` (local) for the same key; only performs baseline comparison when versions match, otherwise skips and returns success

### Updating Baselines

- To accept screenshot changes, copy `test/out/version.json` to `test/baseline/`, **but MUST satisfy both**:
    - Use `version.json` output from running ALL test cases in `TGFXFullTest`, never use partial test output
    - User explicitly confirms accepting all screenshot changes
- `UpdateBaseline` or `update_baseline.sh` syncs `test/baseline/version.json` to `.cache/` and generates local baseline cache. CMake warns when version.json files differ. **NEVER run this command automatically**