/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Per-variant tests for the geometry layouts of the pointwise kernels (PointwiseChainShader and
// the merged chain family GP_LAYOUT values). Each test drives one draw shape that the dedicated
// geometry kernel cannot serve (a color FP or a composed coverage), pins the exact variant that
// serves it (shader, GP_LAYOUT, TEXTURE_COUNT, HAS_MASK_TEXTURE, HAS_XP, RESOURCE_KIND), and
// compares the precompiled render against the runtime render.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatch.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ShaderCaps.h"
#include "gpu/shaders/kernels/PointwiseChainShader.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

constexpr int SceneSize = 160;
constexpr int HairlineLineLayout = 2;
constexpr int ComplexRRectFillLayout = 3;
constexpr int ComplexRRectStrokeLayout = 4;

enum class LayoutPaint { Solid, Image, Gradient, GradientTimesImage };
enum class LayoutClip { None, TwoRRects, PathMask, PathMaskRRect };
// The geometry under test: hairline zigzags (HairlineLineGeometryProcessor), or rounded rects with
// per-corner radii (ComplexEllipseGeometryProcessor), filled or stroked.
enum class LayoutGeometry { Hairline, ComplexRRectFill, ComplexRRectStroke };

struct LayoutVariant {
  const char* label = "";
  LayoutPaint paint = LayoutPaint::Solid;
  LayoutClip clip = LayoutClip::None;
  // Difference reads the destination, which puts a PorterDuff XP on the draw.
  bool dstRead = false;
  // The expected serving variant.
  const char* shaderName = "";
  int gpLayout = 0;
  int textureCount = 0;
  int maskTexture = 0;
  LayoutGeometry geometry = LayoutGeometry::Hairline;
  // Two batched rrects of different colors: the op carries per-vertex colors (read through the
  // unconditional color attribute; the color axis is no longer a permutation dimension).
  bool multiColor = false;
};

// The XP variant a dst-reading draw resolves to: framebuffer fetch when the runtime uses it with a
// bundle loaded (mirrors OpsCompositor's UsesRuntimeFrameBufferFetch), the dst texture otherwise.
int ExpectedXPType(Context* context, bool dstRead) {
  if (!dstRead) {
    return 0;
  }
  auto* caps = context->shaderCaps();
  if (!caps->frameBufferFetchSupport) {
    return 1;
  }
  if (context->backend() == Backend::OpenGL &&
      caps->frameBufferFetchExtensionString != "GL_EXT_shader_framebuffer_fetch") {
    return 1;
  }
  return 2;
}

std::shared_ptr<Image> MakePatternImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(32, 32)) {
    return nullptr;
  }
  auto* pixels = static_cast<uint8_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  auto rowBytes = bitmap.rowBytes();
  for (int y = 0; y < 32; ++y) {
    auto* row = pixels + static_cast<size_t>(y) * rowBytes;
    for (int x = 0; x < 32; ++x) {
      bool checker = ((x / 8) + (y / 8)) % 2 == 0;
      row[x * 4 + 0] = static_cast<uint8_t>(checker ? 230 : 40);
      row[x * 4 + 1] = static_cast<uint8_t>(x * 8);
      row[x * 4 + 2] = static_cast<uint8_t>(y * 8);
      row[x * 4 + 3] = 255;
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

std::shared_ptr<Shader> MakeLayoutShader(LayoutPaint paint, const std::shared_ptr<Image>& image) {
  auto gradient = [] {
    return Shader::MakeLinearGradient(
        Point::Make(0, 0), Point::Make(150, 120),
        {Color::FromRGBA(255, 80, 40, 230), Color::FromRGBA(40, 220, 120, 200)}, {});
  };
  switch (paint) {
    case LayoutPaint::Solid:
      return nullptr;
    case LayoutPaint::Image:
      return Shader::MakeImageShader(image);
    case LayoutPaint::Gradient:
      return gradient();
    case LayoutPaint::GradientTimesImage:
      return Shader::MakeBlend(BlendMode::Multiply, gradient(), Shader::MakeImageShader(image));
  }
  return nullptr;
}

// A zigzag of straight segments only: the hairline decomposition emits HairlineLineOp alone (no
// quad op), so every hairline record of the scene belongs to the layout under test.
Path MakeZigzagPath() {
  Path path = {};
  path.moveTo(10, 14);
  for (int i = 1; i <= 12; ++i) {
    auto x = 10.0f + static_cast<float>(i) * 11.0f;
    auto y = (i % 2 == 0) ? 14.0f : 118.0f;
    path.lineTo(x, y);
  }
  return path;
}

const char* GeometryProcessorName(LayoutGeometry geometry) {
  return geometry == LayoutGeometry::Hairline ? "HairlineLineGeometryProcessor"
                                              : "ComplexEllipseGeometryProcessor";
}

// Per-corner radii mark the rrect complex, which routes it onto ComplexEllipseGeometryProcessor.
// Every corner keeps its axis ratio under 2 and its radii above the half stroke width, so the
// stroked form stays on the rrect op instead of falling back to the path stroker.
RRect MakeComplexRRect(const Rect& rect) {
  std::array<Point, 4> radii = {Point::Make(30, 22), Point::Make(18, 26), Point::Make(24, 14),
                                Point::Make(16, 20)};
  RRect rrect = {};
  rrect.setRectRadii(rect, radii);
  return rrect;
}

// Two complex rrects batched into one op. With multiColor the second paint color differs, so the
// op carries per-vertex colors; otherwise both share the common Color uniform.
void DrawComplexRRects(Canvas* canvas, Paint paint, bool stroke, bool multiColor) {
  paint.setAntiAlias(true);
  if (stroke) {
    paint.setStyle(PaintStyle::Stroke);
    paint.setStrokeWidth(6.0f);
  } else {
    paint.setStyle(PaintStyle::Fill);
  }
  canvas->drawRRect(MakeComplexRRect(Rect::MakeXYWH(12, 10, 120, 64)), paint);
  if (multiColor) {
    paint.setColor(Color::FromRGBA(120, 220, 250, 210));
  }
  canvas->drawRRect(MakeComplexRRect(Rect::MakeXYWH(24, 82, 120, 64)), paint);
}

void RenderLayoutScene(Context* context, const LayoutVariant& variant,
                       const std::shared_ptr<Image>& image, Bitmap* outBitmap,
                       bool drawGeometry = true) {
  auto surface = Surface::Make(context, SceneSize, SceneSize);
  ASSERT_NE(surface, nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(40, 90, 160, 255));
  // A varied destination under the hairlines, so a dst-reading blend is observable.
  Paint background = {};
  background.setColor(Color::FromRGBA(230, 200, 60, 255));
  canvas->drawRect(Rect::MakeXYWH(20, 20, 80, 120), background);

  canvas->save();
  if (variant.clip == LayoutClip::TwoRRects) {
    // Two partially overlapping rounded clips: neither contains the other, so the clip stack keeps
    // both and the draw carries Compose(RRectEffect, RRectEffect) coverage.
    RRect first = {};
    first.setRectXY(Rect::MakeXYWH(6, 6, 140, 118), 40, 40);
    canvas->clipRRect(first);
    RRect second = {};
    second.setRectXY(Rect::MakeXYWH(22, 26, 130, 126), 34, 34);
    canvas->clipRRect(second);
  } else if (variant.clip == LayoutClip::PathMask || variant.clip == LayoutClip::PathMaskRRect) {
    // A path clip has no analytic form: it rasterizes into a device-space mask texture.
    Path clipPath = {};
    clipPath.moveTo(4, 12);
    clipPath.lineTo(156, 30);
    clipPath.lineTo(110, 156);
    clipPath.lineTo(14, 140);
    clipPath.close();
    canvas->clipPath(clipPath);
    if (variant.clip == LayoutClip::PathMaskRRect) {
      // Adds an analytic rounded clip, so the coverage is Compose(mask, RRect): beyond the
      // dedicated kernels' single-coverage contract.
      RRect rounded = {};
      rounded.setRectXY(Rect::MakeXYWH(8, 8, 142, 140), 38, 38);
      canvas->clipRRect(rounded);
    }
  }
  Paint paint = {};
  paint.setColor(Color::FromRGBA(250, 245, 235, 255));
  if (auto shader = MakeLayoutShader(variant.paint, image)) {
    paint.setShader(shader);
  }
  if (variant.dstRead) {
    paint.setBlendMode(BlendMode::Difference);
  }
  if (variant.geometry != LayoutGeometry::Hairline) {
    // No rotation: the rrect op keeps axis-aligned rrects. The non-uniform scale still exercises
    // the device-space vertices and the leaf coordinate transforms.
    canvas->translate(6, 4);
    canvas->scale(1.04f, 0.96f);
    if (drawGeometry) {
      DrawComplexRRects(canvas, paint, variant.geometry == LayoutGeometry::ComplexRRectStroke,
                        variant.multiColor);
    }
    canvas->restore();
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(SceneSize, SceneSize));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    return;
  }
  // A non-trivial transform: leaf coordinates go through the Matrix uniform and the GP's uvMatrix.
  canvas->translate(6, 4);
  canvas->rotate(8);
  canvas->scale(1.08f, 0.96f);
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.0f);
  auto path = MakeZigzagPath();
  if (drawGeometry) {
    // AA on and off: AAEnabled is a runtime uniform of the same variant.
    paint.setAntiAlias(true);
    canvas->drawPath(path, paint);
    canvas->translate(0, 20);
    paint.setAntiAlias(false);
    canvas->drawPath(path, paint);
  }
  canvas->restore();
  context->flushAndSubmit(true);

  ASSERT_TRUE(outBitmap->allocPixels(SceneSize, SceneSize));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_NE(pixels, nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
}

// Max channel delta within tolerance, plus the structural checks a per-channel bound misses: the
// mean alpha and the non-empty pixel ratio must not drift.
void ExpectRendersNear(const char* label, Bitmap& candidate, Bitmap& reference, int tolerance) {
  auto* a = static_cast<const uint8_t*>(candidate.lockPixels());
  auto* r = static_cast<const uint8_t*>(reference.lockPixels());
  ASSERT_TRUE(a != nullptr && r != nullptr);
  auto pixelCount = static_cast<size_t>(SceneSize) * static_cast<size_t>(SceneSize);
  int maxDiff = 0;
  size_t diffBytes = 0;
  double candidateAlpha = 0.0;
  double referenceAlpha = 0.0;
  size_t candidateNonEmpty = 0;
  size_t referenceNonEmpty = 0;
  for (size_t i = 0; i < pixelCount * 4; ++i) {
    int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(r[i]));
    diffBytes += d > 0 ? 1 : 0;
    maxDiff = std::max(maxDiff, d);
    if (i % 4 == 3) {
      candidateAlpha += a[i];
      referenceAlpha += r[i];
      candidateNonEmpty += a[i] != 0 ? 1 : 0;
      referenceNonEmpty += r[i] != 0 ? 1 : 0;
    }
  }
  candidate.unlockPixels();
  reference.unlockPixels();
  auto count = static_cast<double>(pixelCount);
  EXPECT_LE(maxDiff, tolerance) << label << ": maxChannelDiff=" << maxDiff
                                << " diffBytes=" << diffBytes << "/" << pixelCount * 4;
  EXPECT_NEAR(candidateAlpha / count, referenceAlpha / count, 0.5) << label;
  EXPECT_NEAR(static_cast<double>(candidateNonEmpty) / count,
              static_cast<double>(referenceNonEmpty) / count, 0.001)
      << label;
}

// The number of pixels the geometry changed: guards against a vacuous comparison where both
// routes draw nothing (a broken or clipped-away scene would still compare equal).
size_t CountChangedPixels(Bitmap& rendered, Bitmap& background) {
  auto* a = static_cast<const uint32_t*>(rendered.lockPixels());
  auto* b = static_cast<const uint32_t*>(background.lockPixels());
  size_t changed = 0;
  if (a != nullptr && b != nullptr) {
    auto pixelCount = static_cast<size_t>(SceneSize) * static_cast<size_t>(SceneSize);
    for (size_t i = 0; i < pixelCount; ++i) {
      changed += a[i] != b[i] ? 1 : 0;
    }
  }
  rendered.unlockPixels();
  background.unlockPixels();
  return changed;
}

// Renders the scene on the runtime route (reference) and with the embedded bundle (candidate),
// requires the hairline draws to be served by exactly the expected variant with no dropped draw or
// runtime program, and compares the two renders.
void ExpectLayoutVariant(const LayoutVariant& variant, const char* gpName) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  SKIP_ON_SWIFTSHADER(context);
  std::shared_ptr<Image> image = nullptr;
  if (variant.paint == LayoutPaint::Image || variant.paint == LayoutPaint::GradientTimesImage) {
    image = MakePatternImage();
    ASSERT_NE(image, nullptr);
  }
  auto* cache = context->precompiledShaderCache();
  Bitmap reference = {};
  Bitmap candidate = {};
  Bitmap background = {};
  {
    cache->unload();
    context->globalCache()->clearPrograms();
    ScopedAOTStatsPause referencePause(context, true);
    RenderLayoutScene(context, variant, image, &reference);
    RenderLayoutScene(context, variant, image, &background, false);
  }
  // Two zigzags of 12 segments each: a working scene changes well over a thousand pixels.
  EXPECT_GT(CountChangedPixels(reference, background), 1000u) << variant.label;
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    RenderLayoutScene(context, variant, image, &candidate);

    EXPECT_EQ(cache->drawStats().droppedDraws, 0u) << variant.label;
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u) << variant.label;
    const int expectedXP = ExpectedXPType(context, variant.dstRead);
    // A gradient paint rides the merged family's extended resource kind (RESOURCE_KIND 1).
    const bool gradientKind =
        variant.paint == LayoutPaint::Gradient || variant.paint == LayoutPaint::GradientTimesImage;
    // An empty shader name leaves the serving kernel unpinned: the draw must still be served by a
    // precompiled program (no drop, no runtime program) and render like the runtime.
    const bool pinned = variant.shaderName[0] != '\0';
    using FD = PointwiseChainShader::FD;
    const auto fragDomain = FD::domain();
    const size_t gpLayoutPos = fragDomain.indexOf(FD::GP_LAYOUT.defineName);
    const size_t hasXPPos = fragDomain.indexOf(FD::HAS_XP.defineName);
    const size_t textureCountPos = fragDomain.indexOf(FD::TEXTURE_COUNT.defineName);
    const size_t hasMaskTexturePos = fragDomain.indexOf(FD::HAS_MASK_TEXTURE.defineName);
    const size_t resourceKindPos = fragDomain.indexOf(FD::RESOURCE_KIND.defineName);
    bool served = !pinned;
    std::string seen = {};
    for (const auto& record : cache->hitRecords()) {
      if (record.effectSignature.find(gpName) == std::string::npos) {
        continue;
      }
      seen += record.shaderName + "#" + std::to_string(record.fragPermutationIndex) + " ";
      if (record.shaderName != variant.shaderName) {
        continue;
      }
      auto frag = fragDomain.decode(record.fragPermutationIndex);
      if (frag.size() < fragDomain.dimensionCount()) {
        continue;
      }
      bool resourceMatches = !gradientKind || frag[resourceKindPos] == 1;
      if (frag[gpLayoutPos] == variant.gpLayout && frag[hasXPPos] == expectedXP &&
          frag[textureCountPos] == variant.textureCount &&
          frag[hasMaskTexturePos] == variant.maskTexture && resourceMatches) {
        served = true;
      }
    }
    EXPECT_FALSE(seen.empty()) << variant.label << ": no precompiled hit for " << gpName;
    EXPECT_TRUE(served) << variant.label << ": expected " << variant.shaderName
                        << " GP_LAYOUT=" << variant.gpLayout << " HAS_XP=" << expectedXP
                        << " TEXTURE_COUNT=" << variant.textureCount
                        << " HAS_MASK_TEXTURE=" << variant.maskTexture << ", served: " << seen;
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectRendersNear(variant.label, candidate, reference, 1);
}

void ExpectHairlineLineVariant(LayoutVariant variant) {
  variant.gpLayout = HairlineLineLayout;
  ExpectLayoutVariant(variant, "HairlineLineGeometryProcessor");
}

void ExpectComplexRRectVariant(LayoutVariant variant, bool stroke) {
  variant.geometry = stroke ? LayoutGeometry::ComplexRRectStroke : LayoutGeometry::ComplexRRectFill;
  variant.gpLayout = stroke ? ComplexRRectStrokeLayout : ComplexRRectFillLayout;
  ExpectLayoutVariant(variant, "ComplexEllipseGeometryProcessor");
}

// ---- Reported shapes: draws an integrating application produced that were misses ----

enum ReportedClip : int {
  ClipNone = 0,
  ClipPath = 1,   // no analytic form: a device-space mask (DeviceSpaceTextureEffect)
  ClipRect = 2,   // AA, fractional, under a rotation: an analytic RectEffect
  ClipRRect = 4,  // AA rounded rect: an analytic RRectEffect
  ClipSecondRRect = 8,
};

struct ReportedShape {
  const char* label = "";
  // The exact effect signature from the miss report; the runtime render must produce it.
  const char* signature = "";
  bool gradient = false;
  bool alphaThreshold = false;
  int clips = ClipNone;
  LayoutGeometry geometry = LayoutGeometry::Hairline;
  // A three-stop gradient (DualIntervalGradientColorizer) instead of the two-stop one.
  bool dualGradient = false;
  // A repeating image shader (TiledTextureEffect) as the paint.
  bool tiledImage = false;
  // Apply the RRect clip before the Rect clip: the composed coverage lists its FPs in clip order.
  bool rrectBeforeRect = false;
};

void ApplyReportedRectClip(Canvas* canvas) {
  // Rotate only for the clip (restore() would pop the clip with the matrix).
  canvas->rotate(4, 80, 80);
  canvas->clipRect(Rect::MakeLTRB(30.5f, 9.25f, 150.75f, 120.5f));
  canvas->resetMatrix();
}

void ApplyReportedRRectClip(Canvas* canvas) {
  RRect rrect = {};
  rrect.setRectXY(Rect::MakeXYWH(6, 6, 140, 118), 40, 40);
  canvas->clipRRect(rrect);
}

void RenderReportedShape(Context* context, const ReportedShape& shape,
                         const std::shared_ptr<Image>& image, Bitmap* outBitmap) {
  auto surface = Surface::Make(context, SceneSize, SceneSize);
  ASSERT_NE(surface, nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(40, 90, 160, 255));
  canvas->save();
  // Each clip is applied under its own transform, so the clip stack cannot merge two analytic
  // elements into one (different matrices) and each stays a separate FP of the composed coverage.
  // Every clip cuts into the geometry: a clip containing the whole draw is dropped by the stack.
  if (shape.clips & ClipPath) {
    Path clipPath = {};
    clipPath.moveTo(4, 12);
    clipPath.lineTo(156, 30);
    clipPath.lineTo(110, 156);
    clipPath.lineTo(14, 140);
    clipPath.close();
    canvas->clipPath(clipPath);
  }
  if (shape.rrectBeforeRect) {
    if (shape.clips & ClipRRect) {
      ApplyReportedRRectClip(canvas);
    }
    if (shape.clips & ClipRect) {
      ApplyReportedRectClip(canvas);
    }
  } else {
    if (shape.clips & ClipRect) {
      ApplyReportedRectClip(canvas);
    }
    if (shape.clips & ClipRRect) {
      ApplyReportedRRectClip(canvas);
    }
  }
  if (shape.clips & ClipSecondRRect) {
    canvas->rotate(-3, 80, 80);
    RRect rrect = {};
    rrect.setRectXY(Rect::MakeXYWH(40, 30, 115, 126), 34, 34);
    canvas->clipRRect(rrect);
    canvas->resetMatrix();
  }
  canvas->translate(6, 4);
  Paint paint = {};
  paint.setAntiAlias(true);
  paint.setColor(Color::FromRGBA(250, 245, 235, 255));
  if (shape.gradient) {
    if (shape.dualGradient) {
      paint.setShader(Shader::MakeLinearGradient(
          Point::Make(0, 0), Point::Make(150, 120),
          {Color::FromRGBA(255, 80, 40, 230), Color::FromRGBA(250, 230, 60, 255),
           Color::FromRGBA(40, 220, 120, 200)},
          {}));
    } else {
      paint.setShader(MakeLayoutShader(LayoutPaint::Gradient, nullptr));
    }
  } else if (shape.tiledImage) {
    paint.setShader(Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat));
  }
  if (shape.alphaThreshold) {
    paint.setColorFilter(ColorFilter::AlphaThreshold(0.85f));
  }
  if (shape.geometry == LayoutGeometry::Hairline) {
    canvas->rotate(8);
    paint.setStyle(PaintStyle::Stroke);
    paint.setStrokeWidth(0.0f);
    auto path = MakeZigzagPath();
    canvas->drawPath(path, paint);
    canvas->translate(0, 20);
    canvas->drawPath(path, paint);
  } else {
    DrawComplexRRects(canvas, paint, shape.geometry == LayoutGeometry::ComplexRRectStroke, false);
  }
  canvas->restore();
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(SceneSize, SceneSize));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_NE(pixels, nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
}

void ExpectReportedShapeServed(const ReportedShape& shape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  SKIP_ON_SWIFTSHADER(context);
  std::shared_ptr<Image> image = nullptr;
  if (shape.tiledImage) {
    image = MakePatternImage();
    ASSERT_NE(image, nullptr);
  }
  const char* gpName = GeometryProcessorName(shape.geometry);
  auto* cache = context->precompiledShaderCache();
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    // Runtime route with diagnostics on: every program records its original effect signature,
    // which proves the scene reproduces the reported shape.
    cache->unload();
    context->globalCache()->clearPrograms();
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    RenderReportedShape(context, shape, image, &reference);
    bool reproduced = false;
    std::string seen = {};
    for (const auto& record : cache->fallbackRecords()) {
      if (record.effectSignature.find(gpName) == std::string::npos) {
        continue;
      }
      seen += record.effectSignature + "\n";
      reproduced = reproduced || record.effectSignature == shape.signature;
    }
    EXPECT_TRUE(reproduced) << shape.label << ": the scene did not produce\n  " << shape.signature
                            << "\n"
                            << gpName << " signatures seen:\n"
                            << seen;
    cache->setDiagnosticRecordingEnabled(false);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    RenderReportedShape(context, shape, image, &candidate);
    EXPECT_EQ(cache->drawStats().droppedDraws, 0u) << shape.label;
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u) << shape.label;
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u) << shape.label;
    std::string misses = {};
    for (const auto& record : cache->fallbackRecords()) {
      misses += record.effectSignature + "\n";
    }
    EXPECT_TRUE(misses.empty()) << shape.label << " misses:\n" << misses;
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectRendersNear(shape.label, candidate, reference, 1);
}

// A draw scene under test: paints its geometry on an already-cleared canvas.
using ReportedScene = std::function<void(Canvas*)>;

// Renders one scene twice: first on the runtime route with diagnostics on, which must produce an
// effect signature exactly equal to `signature` for `gpName` (proving the scene reproduces the
// reported draw), then with the embedded bundle, which must serve it without a dropped draw, a
// NoMatchingRule fallback or a runtime program. The two renders must match within 1/255.
void ExpectSceneServed(const char* label, const char* gpName, const char* signature,
                       const ReportedScene& scene) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  SKIP_ON_SWIFTSHADER(context);
  auto* cache = context->precompiledShaderCache();
  auto render = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, SceneSize, SceneSize);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::FromRGBA(40, 90, 160, 255));
    canvas->save();
    scene(canvas);
    canvas->restore();
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(SceneSize, SceneSize));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    context->globalCache()->clearPrograms();
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    render(&reference);
    bool reproduced = false;
    std::string seen = {};
    std::string allSeen = {};
    for (const auto& record : cache->fallbackRecords()) {
      allSeen += "  " + record.effectSignature + "\n";
      if (record.effectSignature.find(gpName) == std::string::npos) {
        continue;
      }
      seen += record.effectSignature + "\n";
      reproduced = reproduced || record.effectSignature == signature;
    }
    EXPECT_TRUE(reproduced) << label << ": the scene did not produce\n  " << signature << "\n"
                            << gpName << " signatures seen:\n"
                            << (seen.empty() ? allSeen : seen);
    cache->setDiagnosticRecordingEnabled(false);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    render(&candidate);
    EXPECT_EQ(cache->drawStats().droppedDraws, 0u) << label;
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u) << label;
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u) << label;
    std::string misses = {};
    for (const auto& record : cache->fallbackRecords()) {
      misses +=
          record.effectSignature + " [" +
          PermutationMatchFailureName(static_cast<PermutationMatchFailure>(record.matchFailure)) +
          "]\n";
    }
    EXPECT_TRUE(misses.empty()) << label << " misses:\n" << misses;
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectRendersNear(label, candidate, reference, 1);
}

}  // namespace

// Rows 12-22 of the integration miss report: HairlineLineGeometryProcessor with a color FP or a
// coverage form the dedicated HairlineLineShader cannot carry.

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineDeviceMask) {
  ExpectReportedShapeServed(
      {"row12",
       "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[DeviceSpaceTextureEffect];"
       "XP=EmptyXferProcessor",
       false, false, ClipPath});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineDeviceMaskRect) {
  ExpectReportedShapeServed(
      {"row13",
       "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RectEffect)];XP=EmptyXferProcessor",
       false, false, ClipPath | ClipRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMaskRRect) {
  ExpectReportedShapeServed({"row14",
                             "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
                             "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=["
                             "ComposeFragmentProcessor("
                             "DeviceSpaceTextureEffect,RRectEffect)];XP=EmptyXferProcessor",
                             true, false, ClipPath | ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMask) {
  ExpectReportedShapeServed({"row15",
                             "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
                             "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=["
                             "DeviceSpaceTextureEffect];XP=EmptyXferProcessor",
                             true, false, ClipPath});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineTwoRRects) {
  ExpectReportedShapeServed(
      {"row16",
       "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor("
       "RRectEffect,RRectEffect)];XP=EmptyXferProcessor",
       false, false, ClipRRect | ClipSecondRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineRectRRect) {
  ExpectReportedShapeServed(
      {"row17",
       "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor("
       "RectEffect,RRectEffect)];XP=EmptyXferProcessor",
       false, false, ClipRect | ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineDeviceMaskRRect) {
  ExpectReportedShapeServed(
      {"row18",
       "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RRectEffect)];XP=EmptyXferProcessor",
       false, false, ClipPath | ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientRRect) {
  ExpectReportedShapeServed(
      {"row19",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=[RRectEffect];"
       "XP=EmptyXferProcessor",
       true, false, ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientThresholdRRect) {
  ExpectReportedShapeServed(
      {"row20",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout),AlphaStepFragmentProcessor];"
       "CoverageFP=[RRectEffect];XP=EmptyXferProcessor",
       true, true, ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMaskRect) {
  ExpectReportedShapeServed({"row21",
                             "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
                             "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=["
                             "ComposeFragmentProcessor("
                             "DeviceSpaceTextureEffect,RectEffect)];XP=EmptyXferProcessor",
                             true, false, ClipPath | ClipRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMaskRectRRect) {
  ExpectReportedShapeServed(
      {"row22",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=["
       "ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RectEffect,RRectEffect)];XP=EmptyXferProcessor",
       true, false, ClipPath | ClipRect | ClipRRect});
}

namespace {}  // namespace

// ---- HairlineLine on PointwiseChainShader (GP_LAYOUT=2) ----
// Leaf-free, no mask: a solid hairline under two rounded clips (Compose(RRect, RRect)), which the
// dedicated HairlineLineShader's single-clip contract cannot carry.

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainComposedClip) {
  ExpectHairlineLineVariant({"hairline-chain-composed-clip", LayoutPaint::Solid,
                             LayoutClip::TwoRRects, false, "PointwiseChainShader", 0, 0, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainComposedClipDstRead) {
  ExpectHairlineLineVariant({"hairline-chain-composed-clip-dst", LayoutPaint::Solid,
                             LayoutClip::TwoRRects, true, "PointwiseChainShader", 0, 0, 0});
}

// Leaf-free with the compile-time mask: a solid hairline under a path clip (device mask).

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainDeviceMask) {
  ExpectHairlineLineVariant({"hairline-chain-device-mask", LayoutPaint::Solid, LayoutClip::PathMask,
                             false, "PointwiseChainShader", 0, 0, 1});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainDeviceMaskDstRead) {
  ExpectHairlineLineVariant({"hairline-chain-device-mask-dst", LayoutPaint::Solid,
                             LayoutClip::PathMask, true, "PointwiseChainShader", 0, 0, 1});
}

// Four-leaf: an image-shaded hairline (the dst-read case also carries a path clip, which the
// four-leaf variant applies through the runtime HasMaskTexture uniform).

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainTexture) {
  ExpectHairlineLineVariant({"hairline-chain-texture", LayoutPaint::Image, LayoutClip::None, false,
                             "PointwiseChainShader", 0, 1, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineChainTextureDstRead) {
  ExpectHairlineLineVariant({"hairline-chain-texture-dst", LayoutPaint::Image, LayoutClip::PathMask,
                             true, "PointwiseChainShader", 0, 1, 0});
}

// ---- HairlineLine on PointwiseGraphShader (GP_LAYOUT=2, RESOURCE_KIND=1: the gradient) ----

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradient) {
  ExpectHairlineLineVariant({"hairline-graph-gradient", LayoutPaint::Gradient, LayoutClip::None,
                             false, "PointwiseChainShader", 0, 0, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-dst", LayoutPaint::Gradient,
                             LayoutClip::TwoRRects, true, "PointwiseChainShader", 0, 0, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDeviceMask) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-device-mask", LayoutPaint::Gradient,
                             LayoutClip::PathMask, false, "PointwiseChainShader", 0, 0, 1});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDeviceMaskDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-device-mask-dst", LayoutPaint::Gradient,
                             LayoutClip::PathMask, true, "PointwiseChainShader", 0, 0, 1});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientTexture) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-texture", LayoutPaint::GradientTimesImage,
                             LayoutClip::None, false, "PointwiseChainShader", 0, 1, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientTextureDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-texture-dst", LayoutPaint::GradientTimesImage,
                             LayoutClip::TwoRRects, true, "PointwiseChainShader", 0, 1, 0});
}

// Rows 4-9 of the integration miss report: ComplexEllipseGeometryProcessor (rounded rects with
// per-corner radii) with a color FP or a composed coverage the dedicated ComplexEllipseFillShader
// cannot carry.

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectGradient) {
  ReportedShape shape = {"row4",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[ClampedGradientEffect("
                         "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=[];"
                         "XP=EmptyXferProcessor",
                         true, false, ClipNone};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  ExpectReportedShapeServed(shape);
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectGradientThreshold) {
  ReportedShape shape = {"row5",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[ClampedGradientEffect("
                         "SingleIntervalGradientColorizer,LinearGradientLayout),"
                         "AlphaStepFragmentProcessor];CoverageFP=[];XP=EmptyXferProcessor",
                         true, true, ClipNone};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  ExpectReportedShapeServed(shape);
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectDualGradientRRect) {
  ReportedShape shape = {"row6",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[ClampedGradientEffect("
                         "DualIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=["
                         "RRectEffect];XP=EmptyXferProcessor",
                         true, false, ClipRRect};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  shape.dualGradient = true;
  ExpectReportedShapeServed(shape);
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectTiledImage) {
  ReportedShape shape = {"row7",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[TiledTextureEffect];"
                         "CoverageFP=[];XP=EmptyXferProcessor",
                         false, false, ClipNone};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  shape.tiledImage = true;
  ExpectReportedShapeServed(shape);
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectRRectRect) {
  ReportedShape shape = {"row8",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[];CoverageFP=["
                         "ComposeFragmentProcessor(RRectEffect,RectEffect)];XP=EmptyXferProcessor",
                         false, false, ClipRRect | ClipRect};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  shape.rrectBeforeRect = true;
  ExpectReportedShapeServed(shape);
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedComplexRRectTiledImageRRectRect) {
  ReportedShape shape = {"row9",
                         "GP=ComplexEllipseGeometryProcessor;ColorFP=[TiledTextureEffect];"
                         "CoverageFP=[ComposeFragmentProcessor(RRectEffect,RectEffect)];"
                         "XP=EmptyXferProcessor",
                         false, false, ClipRRect | ClipRect};
  shape.geometry = LayoutGeometry::ComplexRRectFill;
  shape.tiledImage = true;
  shape.rrectBeforeRect = true;
  ExpectReportedShapeServed(shape);
}

// ---- ComplexEllipse on the pointwise kernels (GP_LAYOUT=3 fill, GP_LAYOUT=4 stroke) ----
// Each test runs both layouts. The chain variants are pinned (GP_LAYOUT, TEXTURE_COUNT,
// HAS_MASK_TEXTURE, HAS_XP); the gradient cases only require a precompiled, undropped,
// runtime-identical draw, since which kernel carries a gradient is a routing decision.

namespace {

void ExpectComplexRRectBothLayouts(const std::string& name, LayoutVariant variant) {
  for (bool stroke : {false, true}) {
    auto label = "complex-rrect-" + std::string(stroke ? "stroke-" : "fill-") + name;
    variant.label = label.c_str();
    ExpectComplexRRectVariant(variant, stroke);
  }
}

}  // namespace

// Leaf-free, no mask: a solid rrect under two rounded clips (Compose(RRect, RRect)), beyond the
// dedicated kernel's single-coverage contract.
TGFX_TEST(AOTGeometryLayoutTest, ComplexRRectChainComposedClip) {
  ExpectComplexRRectBothLayouts(
      "chain-composed-clip",
      {"", LayoutPaint::Solid, LayoutClip::TwoRRects, false, "PointwiseChainShader", 0, 0, 0});
  ExpectComplexRRectBothLayouts(
      "chain-composed-clip-dst",
      {"", LayoutPaint::Solid, LayoutClip::TwoRRects, true, "PointwiseChainShader", 0, 0, 0});
}

// Leaf-free with the compile-time mask: a solid rrect under a path clip plus a rounded clip
// (Compose(mask, RRect)).
TGFX_TEST(AOTGeometryLayoutTest, ComplexRRectChainDeviceMask) {
  ExpectComplexRRectBothLayouts(
      "chain-device-mask",
      {"", LayoutPaint::Solid, LayoutClip::PathMaskRRect, false, "PointwiseChainShader", 0, 0, 1});
  ExpectComplexRRectBothLayouts(
      "chain-device-mask-dst",
      {"", LayoutPaint::Solid, LayoutClip::PathMaskRRect, true, "PointwiseChainShader", 0, 0, 1});
}

// Four-leaf: an image-shaded rrect (the dst-read case also carries a path clip, which the
// four-leaf variant applies through the runtime HasMaskTexture uniform).
TGFX_TEST(AOTGeometryLayoutTest, ComplexRRectChainTexture) {
  ExpectComplexRRectBothLayouts("chain-texture", {"", LayoutPaint::Image, LayoutClip::None, false,
                                                  "PointwiseChainShader", 0, 1, 0});
  ExpectComplexRRectBothLayouts("chain-texture-dst", {"", LayoutPaint::Image, LayoutClip::PathMask,
                                                      true, "PointwiseChainShader", 0, 1, 0});
}

// Per-vertex colors (two batched rrects of different colors): the color attribute is always
// present, so these reuse the same leaf-free and four-leaf variants as a uniform-color draw.
TGFX_TEST(AOTGeometryLayoutTest, ComplexRRectChainVertexColor) {
  LayoutVariant leafFree = {
      "", LayoutPaint::Solid, LayoutClip::TwoRRects, false, "PointwiseChainShader", 0, 0, 0};
  leafFree.multiColor = true;
  ExpectComplexRRectBothLayouts("chain-vertex-color", leafFree);
  leafFree.dstRead = true;
  ExpectComplexRRectBothLayouts("chain-vertex-color-dst", leafFree);
  LayoutVariant texture = {
      "", LayoutPaint::Image, LayoutClip::None, false, "PointwiseChainShader", 0, 1, 0};
  texture.multiColor = true;
  ExpectComplexRRectBothLayouts("chain-vertex-color-texture", texture);
  texture.clip = LayoutClip::PathMask;
  texture.dstRead = true;
  ExpectComplexRRectBothLayouts("chain-vertex-color-texture-dst", texture);
}

// Row 23: a non-AA rounded rect under an analytic rounded clip. The dedicated NonAARRectFillShader
// reads the clip from the runtime clip contract uniforms.
TGFX_TEST(AOTGeometryLayoutTest, ReportedNonAARRectClip) {
  ExpectSceneServed(
      "row23", "NonAARRectGeometryProcessor",
      "GP=NonAARRectGeometryProcessor;ColorFP=[];CoverageFP=[RRectEffect];XP=EmptyXferProcessor",
      [](Canvas* canvas) {
        // A large radius keeps the clip on the coverage-FP route: it cuts into the shape, so it
        // cannot be reduced to a device-space rect test.
        RRect clip = {};
        clip.setRectXY(Rect::MakeXYWH(10, 12, 122, 112), 46, 46);
        canvas->clipRRect(clip);
        // Uniform radii keep the draw on the non-complex NonAARRect path.
        RRect shape = {};
        shape.setRectXY(Rect::MakeXYWH(20, 20, 120, 120), 18, 22);
        Paint paint = {};
        paint.setColor(Color::FromRGBA(70, 130, 180, 255));
        paint.setAntiAlias(false);
        canvas->drawRRect(shape, paint);
      });
}

// Row 37: a round-capped stroke segment (RoundStrokeRectGeometryProcessor) under an analytic rect
// clip. The dedicated RoundStrokeRectFillShader reads the clip from the same uniforms.
TGFX_TEST(AOTGeometryLayoutTest, ReportedRoundStrokeRectClip) {
  ExpectSceneServed("row37", "RoundStrokeRectGeometryProcessor",
                    "GP=RoundStrokeRectGeometryProcessor;ColorFP=[];CoverageFP=[RectEffect];XP="
                    "EmptyXferProcessor",
                    [](Canvas* canvas) {
                      // Rotated and fractional, so the clip stays an analytic AA rect in local space.
                      canvas->rotate(4, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(30.5f, 9.25f, 150.75f, 120.5f));
                      canvas->resetMatrix();
                      Paint paint = {};
                      paint.setColor(Color::FromRGBA(250, 245, 235, 255));
                      paint.setAntiAlias(true);
                      paint.setStyle(PaintStyle::Stroke);
                      paint.setStroke(Stroke(9.0f, LineCap::Round, LineJoin::Round));
                      canvas->drawRect(Rect::MakeXYWH(24, 28, 96, 64), paint);
                    });
}

// Row 24: two analytic rect clips in the same space (Compose(RectEffect, RectEffect)) on a quad.
// The chain kernel now carries two rect parameter sets per space.
TGFX_TEST(AOTGeometryLayoutTest, ReportedTwoRectClips) {
  ExpectSceneServed("row24", "QuadPerEdgeAAGeometryProcessor",
                    "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[];CoverageFP=["
                    "ComposeFragmentProcessor(RectEffect,"
                    "RectEffect)];XP=EmptyXferProcessor",
                    [](Canvas* canvas) {
                      // Two overlapping fractional clips under different transforms: neither contains the other
                      // and neither is a device-space rect.
                      canvas->rotate(5, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
                      canvas->resetMatrix();
                      canvas->rotate(-3, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(28.5f, 22.75f, 150.25f, 140.5f));
                      canvas->resetMatrix();
                      Paint paint = {};
                      paint.setColor(Color::FromRGBA(250, 245, 235, 255));
                      canvas->drawRect(Rect::MakeXYWH(6, 6, 148, 148), paint);
                    });
}

// Row 26: the same two rect clips with an image shader (a four-leaf chain).
TGFX_TEST(AOTGeometryLayoutTest, ReportedTwoRectClipsTexture) {
  auto image = MakePatternImage();
  ExpectSceneServed("row26", "QuadPerEdgeAAGeometryProcessor",
                    "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[TextureEffect];CoverageFP=["
                    "ComposeFragmentProcessor(RectEffect,RectEffect)];XP=EmptyXferProcessor",
                    [image](Canvas* canvas) {
                      canvas->rotate(5, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
                      canvas->resetMatrix();
                      canvas->rotate(-3, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(28.5f, 22.75f, 150.25f, 140.5f));
                      canvas->resetMatrix();
                      Paint paint = {};
                      paint.setShader(Shader::MakeImageShader(image));
                      canvas->drawRect(Rect::MakeXYWH(6, 6, 148, 148), paint);
                    });
}

// Row 1: atlas text under two analytic rect clips. The atlas chain route folds the glyph leaf and
// the composed clip stack into one pass.
TGFX_TEST(AOTGeometryLayoutTest, ReportedAtlasTextTwoRectClips) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto textBlob = TextBlob::MakeFrom("TGFX", Font(typeface, 46));
  ASSERT_TRUE(textBlob != nullptr);
  ExpectSceneServed(
      "row1", "AtlasTextGeometryProcessor",
      "GP=AtlasTextGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor(RectEffect,"
      "RectEffect)];XP=EmptyXferProcessor",
      [textBlob](Canvas* canvas) {
        canvas->rotate(5, 80, 80);
        canvas->clipRect(Rect::MakeLTRB(10.5f, 26.25f, 150.75f, 118.5f));
        canvas->resetMatrix();
        canvas->rotate(-3, 80, 80);
        canvas->clipRect(Rect::MakeLTRB(6.25f, 18.5f, 128.5f, 140.75f));
        canvas->resetMatrix();
        Paint paint = {};
        paint.setColor(Color::FromRGBA(250, 245, 235, 255));
        canvas->drawTextBlob(textBlob, 16, 96, paint);
      });
}

// The rows 25/27/29/31/32/35/36 class: a shader mask filter (SrcIn against a sampled texture,
// i.e. Xfermode-dst) composed with two analytic rect clips. The chain folds the mask as the
// coverage subtree and the clip stack into the two rect slots.
TGFX_TEST(AOTGeometryLayoutTest, ReportedShaderMaskTwoRectClips) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "coverage-mask-two-rect-clips", "QuadPerEdgeAAGeometryProcessor",
      "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[];CoverageFP=[XfermodeFragmentProcessor - "
      "dst(TextureEffect),ComposeFragmentProcessor(RectEffect,RectEffect)];XP=EmptyXferProcessor",
      [image](Canvas* canvas) {
        canvas->rotate(5, 80, 80);
        canvas->clipRect(Rect::MakeLTRB(10.5f, 26.25f, 150.75f, 118.5f));
        canvas->resetMatrix();
        canvas->rotate(-3, 80, 80);
        canvas->clipRect(Rect::MakeLTRB(6.25f, 18.5f, 128.5f, 140.75f));
        canvas->resetMatrix();
        Paint paint = {};
        paint.setColor(Color::FromRGBA(250, 245, 235, 255));
        // A Clamp image-shader mask filter resolves to a hardware-sampled TextureEffect, i.e. the
        // local-space shape mask class.
        paint.setMaskFilter(MaskFilter::MakeShader(Shader::MakeImageShader(image)));
        canvas->drawRect(Rect::MakeXYWH(8, 8, 146, 146), paint);
      });
}

TGFX_TEST(AOTGeometryLayoutTest, ComplexRRectGradient) {
  ExpectComplexRRectBothLayouts("gradient",
                                {"", LayoutPaint::Gradient, LayoutClip::None, false, "", 0, 0, 0});
  ExpectComplexRRectBothLayouts("gradient-device-mask", {"", LayoutPaint::Gradient,
                                                         LayoutClip::PathMask, false, "", 0, 0, 0});
  ExpectComplexRRectBothLayouts("gradient-texture-dst", {"", LayoutPaint::GradientTimesImage,
                                                         LayoutClip::TwoRRects, true, "", 0, 0, 0});
}

// Row 11: a small filled path (rasterized into a coverage texture) under a path clip and a rect
// clip. The clip stack composes the path clip's device mask with the analytic rect, so the
// coverage is [Compose(DeviceSpaceTextureEffect, RectEffect), TextureEffect]; the chain carries the
// device mask as its mask child and the rect as a clip slot.
TGFX_TEST(AOTGeometryLayoutTest, ReportedPathMaskRectClipShapeMask) {
  ExpectSceneServed("row11", "DefaultGeometryProcessor",
                    "GP=DefaultGeometryProcessor;ColorFP=[];CoverageFP=[ComposeFragmentProcessor("
                    "DeviceSpaceTextureEffect,RectEffect),TextureEffect];XP=EmptyXferProcessor",
                    [](Canvas* canvas) {
                      Path clipPath = {};
                      clipPath.moveTo(4, 12);
                      clipPath.lineTo(156, 30);
                      clipPath.lineTo(110, 156);
                      clipPath.lineTo(14, 140);
                      clipPath.close();
                      canvas->clipPath(clipPath);
                      canvas->rotate(5, 80, 80);
                      canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
                      canvas->resetMatrix();
                      Path star = {};
                      for (int i = 0; i < 10; ++i) {
                        auto angle = static_cast<float>(i) * 3.14159265f / 5 - 3.14159265f / 2;
                        auto radius = i % 2 == 0 ? 70.0f : 31.5f;
                        auto x = 80 + radius * std::cos(angle);
                        auto y = 80 + radius * std::sin(angle);
                        if (i == 0) {
                          star.moveTo(x, y);
                        } else {
                          star.lineTo(x, y);
                        }
                      }
                      star.close();
                      Paint paint = {};
                      paint.setAntiAlias(true);
                      paint.setColor(Color::FromRGBA(250, 245, 235, 255));
                      canvas->drawPath(star, paint);
                    });
}

// The same composed clip after a shader mask filter: [mask filter, Compose(DeviceSpaceTextureEffect,
// RectEffect)]. The mask filter lowers as the coverage subtree, the device mask as the mask child
// and the rect as a clip slot.
TGFX_TEST(AOTGeometryLayoutTest, ShaderMaskPathMaskRectClip) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "shader-mask-path-mask-rect", "QuadPerEdgeAAGeometryProcessor",
      "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[];CoverageFP=[XfermodeFragmentProcessor - "
      "dst(TextureEffect),ComposeFragmentProcessor(DeviceSpaceTextureEffect,RectEffect)];"
      "XP=EmptyXferProcessor",
      [image](Canvas* canvas) {
        Path clipPath = {};
        clipPath.moveTo(4, 12);
        clipPath.lineTo(156, 30);
        clipPath.lineTo(110, 156);
        clipPath.lineTo(14, 140);
        clipPath.close();
        canvas->clipPath(clipPath);
        canvas->rotate(5, 80, 80);
        canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
        canvas->resetMatrix();
        Paint paint = {};
        paint.setColor(Color::FromRGBA(250, 245, 235, 255));
        paint.setMaskFilter(MaskFilter::MakeShader(Shader::MakeImageShader(image)));
        canvas->drawRect(Rect::MakeXYWH(8, 8, 146, 146), paint);
      });
}

// Rows 28/33/34 of the integration miss report: an inverted shader mask filter whose shader yields
// no processor (here an image shader under a non-invertible matrix) becomes a constant coverage FP
// (ConstColorProcessor, transparent, ignoring its input), alone or followed by an analytic clip.
// The image is a drop-shadow-filtered subset, the reported color form. The chain carries the
// constant as its coverage root, so the draw composites exactly as the runtime's: through the
// constant coverage, under every blend mode.
namespace {

void DrawConstantMaskImage(Canvas* canvas, const std::shared_ptr<Image>& image, int clip,
                           BlendMode blend) {
  if (clip == 1) {
    canvas->rotate(5, 80, 80);
    canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
    canvas->resetMatrix();
  } else if (clip == 2) {
    RRect rrect = {};
    rrect.setRectXY(Rect::MakeXYWH(6, 6, 140, 118), 40, 40);
    canvas->clipRRect(rrect);
  }
  Paint paint = {};
  paint.setAntiAlias(true);
  paint.setBlendMode(blend);
  paint.setMaskFilter(MaskFilter::MakeShader(
      Shader::MakeImageShader(image)->makeWithMatrix(Matrix::MakeScale(0, 1)), true));
  auto shadow = ImageFilter::DropShadowOnly(3, 3, 0, 0, Color::FromRGBA(20, 30, 40, 200));
  canvas->translate(6, 6);
  canvas->scale(4.4f, 4.4f);
  canvas->drawImage(image->makeSubset(Rect::MakeLTRB(3, 3, 29, 29))->makeWithFilter(shadow),
                    &paint);
}

#define TGFX_CONST_MASK_COLOR                                               \
  "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[XfermodeFragmentProcessor - " \
  "two(ConstColorProcessor,"                                                \
  "TiledTextureEffect)];"

}  // namespace

TGFX_TEST(AOTGeometryLayoutTest, ReportedConstantMaskCoverage) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "row28", "QuadPerEdgeAAGeometryProcessor",
      TGFX_CONST_MASK_COLOR "CoverageFP=[ConstColorProcessor];XP=EmptyXferProcessor",
      [image](Canvas* canvas) { DrawConstantMaskImage(canvas, image, 0, BlendMode::SrcOver); });
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedConstantMaskCoverageRect) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "row33", "QuadPerEdgeAAGeometryProcessor",
      TGFX_CONST_MASK_COLOR "CoverageFP=[ConstColorProcessor,RectEffect];XP=EmptyXferProcessor",
      [image](Canvas* canvas) { DrawConstantMaskImage(canvas, image, 1, BlendMode::SrcOver); });
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedConstantMaskCoverageRRect) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "row34", "QuadPerEdgeAAGeometryProcessor",
      TGFX_CONST_MASK_COLOR "CoverageFP=[ConstColorProcessor,RRectEffect];XP=EmptyXferProcessor",
      [image](Canvas* canvas) { DrawConstantMaskImage(canvas, image, 2, BlendMode::SrcOver); });
}

// The same constant coverage under a dst-reading blend, and under Src, which overwrites the
// destination wherever the coverage is nonzero: both must keep the destination untouched.
TGFX_TEST(AOTGeometryLayoutTest, ConstantMaskCoverageBlendModes) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "constant-mask-multiply", "QuadPerEdgeAAGeometryProcessor",
      TGFX_CONST_MASK_COLOR
      "CoverageFP=[ConstColorProcessor,RectEffect];XP=PorterDuffXferProcessor",
      [image](Canvas* canvas) { DrawConstantMaskImage(canvas, image, 1, BlendMode::Multiply); });
  ExpectSceneServed(
      "constant-mask-src", "QuadPerEdgeAAGeometryProcessor",
      TGFX_CONST_MASK_COLOR
      "CoverageFP=[ConstColorProcessor,RectEffect];XP=PorterDuffXferProcessor",
      [image](Canvas* canvas) { DrawConstantMaskImage(canvas, image, 1, BlendMode::Src); });
}

#undef TGFX_CONST_MASK_COLOR

// SrcRectConstraint::Strict image draws whose coverage the dedicated QuadTextureFillShader cannot
// carry (a shader mask filter, or two analytic clips). The quad carries a per-quad subset, so the
// chain serves them through its HAS_SUBSET variants: leaf 0 clamps to the subset varying ahead of
// its Subset uniform. The image is magnified and the source rect sits inside it, so a missing or
// misplaced clamp bleeds the neighboring texels into the edges.
namespace {

enum class StrictCoverage { MaskRect, MaskTwoRects, TwoRects, MaskRRect };

void DrawStrictImage(Canvas* canvas, const std::shared_ptr<Image>& image, StrictCoverage coverage,
                     BlendMode blend) {
  if (coverage != StrictCoverage::MaskRRect) {
    canvas->rotate(5, 80, 80);
    canvas->clipRect(Rect::MakeLTRB(12.25f, 8.5f, 132.75f, 118.25f));
    canvas->resetMatrix();
  } else {
    RRect rrect = {};
    rrect.setRectXY(Rect::MakeXYWH(6, 6, 140, 118), 40, 40);
    canvas->clipRRect(rrect);
  }
  if (coverage == StrictCoverage::MaskTwoRects || coverage == StrictCoverage::TwoRects) {
    canvas->rotate(-3, 80, 80);
    canvas->clipRect(Rect::MakeLTRB(28.5f, 22.75f, 150.25f, 140.5f));
    canvas->resetMatrix();
  }
  Paint paint = {};
  paint.setAntiAlias(true);
  paint.setBlendMode(blend);
  if (coverage != StrictCoverage::TwoRects) {
    paint.setMaskFilter(
        MaskFilter::MakeShader(Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat)));
  }
  canvas->drawImageRect(image, Rect::MakeLTRB(5.5f, 4.25f, 27.75f, 26.5f),
                        Rect::MakeLTRB(6.5f, 9.25f, 151.75f, 148.5f), SamplingOptions(), &paint,
                        SrcRectConstraint::Strict);
}

#define TGFX_STRICT_QUAD "GP=QuadPerEdgeAAGeometryProcessor;ColorFP=[TextureEffect];"
#define TGFX_STRICT_MASK "CoverageFP=[XfermodeFragmentProcessor - dst(TiledTextureEffect),"

}  // namespace

TGFX_TEST(AOTGeometryLayoutTest, StrictImageMaskRectClip) {
  auto image = MakePatternImage();
  ExpectSceneServed("strict-mask-rect", "QuadPerEdgeAAGeometryProcessor",
                    TGFX_STRICT_QUAD TGFX_STRICT_MASK "RectEffect];XP=EmptyXferProcessor",
                    [image](Canvas* canvas) {
                      DrawStrictImage(canvas, image, StrictCoverage::MaskRect, BlendMode::SrcOver);
                    });
}

TGFX_TEST(AOTGeometryLayoutTest, StrictImageMaskTwoRectClips) {
  auto image = MakePatternImage();
  ExpectSceneServed("strict-mask-two-rects", "QuadPerEdgeAAGeometryProcessor",
                    TGFX_STRICT_QUAD TGFX_STRICT_MASK
                    "ComposeFragmentProcessor(RectEffect,RectEffect)];XP=EmptyXferProcessor",
                    [image](Canvas* canvas) {
                      DrawStrictImage(canvas, image, StrictCoverage::MaskTwoRects,
                                      BlendMode::SrcOver);
                    });
}

TGFX_TEST(AOTGeometryLayoutTest, StrictImageMaskRRectClipDstRead) {
  auto image = MakePatternImage();
  ExpectSceneServed("strict-mask-rrect-multiply", "QuadPerEdgeAAGeometryProcessor",
                    TGFX_STRICT_QUAD TGFX_STRICT_MASK "RRectEffect];XP=PorterDuffXferProcessor",
                    [image](Canvas* canvas) {
                      DrawStrictImage(canvas, image, StrictCoverage::MaskRRect,
                                      BlendMode::Multiply);
                    });
}

TGFX_TEST(AOTGeometryLayoutTest, StrictImageTwoRectClips) {
  auto image = MakePatternImage();
  ExpectSceneServed(
      "strict-two-rects", "QuadPerEdgeAAGeometryProcessor",
      TGFX_STRICT_QUAD
      "CoverageFP=[ComposeFragmentProcessor(RectEffect,RectEffect)];XP=EmptyXferProcessor",
      [image](Canvas* canvas) {
        DrawStrictImage(canvas, image, StrictCoverage::TwoRects, BlendMode::SrcOver);
      });
}

#undef TGFX_STRICT_QUAD
#undef TGFX_STRICT_MASK

}  // namespace tgfx
