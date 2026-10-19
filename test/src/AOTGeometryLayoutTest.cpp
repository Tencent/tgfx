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
// PointwiseGraphShader GP_LAYOUT values). Each test drives one draw shape that the dedicated
// geometry kernel cannot serve (a color FP or a composed coverage), pins the exact variant that
// serves it (shader, GP_LAYOUT, TEXTURE_COUNT, HAS_MASK_TEXTURE, HAS_XP, RESOURCE_KIND), and
// compares the precompiled render against the runtime render.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ShaderCaps.h"
#include "gpu/shaders/level1/PointwiseChainShader.h"
#include "gpu/shaders/level1/PointwiseGraphShader.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
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

enum class LayoutPaint { Solid, Image, Gradient, GradientTimesImage };
enum class LayoutClip { None, TwoRRects, PathMask };

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
  } else if (variant.clip == LayoutClip::PathMask) {
    // A path clip has no analytic form: it rasterizes into a device-space mask texture.
    Path clipPath = {};
    clipPath.moveTo(4, 12);
    clipPath.lineTo(156, 30);
    clipPath.lineTo(110, 156);
    clipPath.lineTo(14, 140);
    clipPath.close();
    canvas->clipPath(clipPath);
  }
  // A non-trivial transform: leaf coordinates go through the Matrix uniform and the GP's uvMatrix.
  canvas->translate(6, 4);
  canvas->rotate(8);
  canvas->scale(1.08f, 0.96f);
  Paint paint = {};
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.0f);
  paint.setColor(Color::FromRGBA(250, 245, 235, 255));
  if (auto shader = MakeLayoutShader(variant.paint, image)) {
    paint.setShader(shader);
  }
  if (variant.dstRead) {
    paint.setBlendMode(BlendMode::Difference);
  }
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
    const bool graph = std::string(variant.shaderName) == "PointwiseGraphShader";
    using FD = PointwiseChainShader::FD;
    bool served = false;
    std::string seen = {};
    for (const auto& record : cache->hitRecords()) {
      if (record.effectSignature.find(gpName) == std::string::npos) {
        continue;
      }
      auto frag = graph ? PointwiseGraphShader::FD::domain().decode(record.fragPermutationIndex)
                        : PointwiseChainShader::FD::domain().decode(record.fragPermutationIndex);
      seen += record.shaderName + "#" + std::to_string(record.fragPermutationIndex) + " ";
      if (record.shaderName != variant.shaderName || frag.size() < FD::COUNT) {
        continue;
      }
      bool resourceMatches = !graph || frag[PointwiseGraphShader::FD::RESOURCE_KIND] == 1;
      if (frag[FD::GP_LAYOUT] == variant.gpLayout && frag[FD::HAS_XP] == expectedXP &&
          frag[FD::TEXTURE_COUNT] == variant.textureCount &&
          frag[FD::HAS_MASK_TEXTURE] == variant.maskTexture && resourceMatches) {
        served = true;
      }
    }
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

// ---- Reported shapes: draws an integrating application produced that were misses ----

enum ReportedClip : int {
  ClipNone = 0,
  ClipPath = 1,       // no analytic form: a device-space mask (DeviceSpaceTextureEffect)
  ClipRect = 2,       // AA, fractional, under a rotation: an analytic RectEffect
  ClipRRect = 4,      // AA rounded rect: an analytic RRectEffect
  ClipSecondRRect = 8,
};

struct ReportedShape {
  const char* label = "";
  // The exact effect signature from the miss report; the runtime render must produce it.
  const char* signature = "";
  bool gradient = false;
  bool alphaThreshold = false;
  int clips = ClipNone;
};

void RenderReportedShape(Context* context, const ReportedShape& shape, Bitmap* outBitmap) {
  auto surface = Surface::Make(context, SceneSize, SceneSize);
  ASSERT_NE(surface, nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(40, 90, 160, 255));
  canvas->save();
  // Each clip is applied under its own transform, so the clip stack cannot merge two analytic
  // elements into one (different matrices) and each stays a separate FP of the composed coverage.
  // Every clip cuts into the hairlines: a clip containing the whole draw is dropped by the stack.
  if (shape.clips & ClipPath) {
    Path clipPath = {};
    clipPath.moveTo(4, 12);
    clipPath.lineTo(156, 30);
    clipPath.lineTo(110, 156);
    clipPath.lineTo(14, 140);
    clipPath.close();
    canvas->clipPath(clipPath);
  }
  if (shape.clips & ClipRect) {
    // Rotate only for the clip (restore() would pop the clip with the matrix).
    canvas->rotate(4, 80, 80);
    canvas->clipRect(Rect::MakeLTRB(30.5f, 9.25f, 150.75f, 120.5f));
    canvas->resetMatrix();
  }
  if (shape.clips & ClipRRect) {
    RRect rrect = {};
    rrect.setRectXY(Rect::MakeXYWH(6, 6, 140, 118), 40, 40);
    canvas->clipRRect(rrect);
  }
  if (shape.clips & ClipSecondRRect) {
    canvas->rotate(-3, 80, 80);
    RRect rrect = {};
    rrect.setRectXY(Rect::MakeXYWH(40, 30, 115, 126), 34, 34);
    canvas->clipRRect(rrect);
    canvas->resetMatrix();
  }
  canvas->translate(6, 4);
  canvas->rotate(8);
  Paint paint = {};
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.0f);
  paint.setAntiAlias(true);
  paint.setColor(Color::FromRGBA(250, 245, 235, 255));
  if (shape.gradient) {
    paint.setShader(MakeLayoutShader(LayoutPaint::Gradient, nullptr));
  }
  if (shape.alphaThreshold) {
    paint.setColorFilter(ColorFilter::AlphaThreshold(0.85f));
  }
  auto path = MakeZigzagPath();
  canvas->drawPath(path, paint);
  canvas->translate(0, 20);
  canvas->drawPath(path, paint);
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
    RenderReportedShape(context, shape, &reference);
    bool reproduced = false;
    std::string seen = {};
    for (const auto& record : cache->fallbackRecords()) {
      if (record.effectSignature.find("HairlineLineGeometryProcessor") == std::string::npos) {
        continue;
      }
      seen += record.effectSignature + "\n";
      reproduced = reproduced || record.effectSignature == shape.signature;
    }
    EXPECT_TRUE(reproduced) << shape.label << ": the scene did not produce\n  " << shape.signature
                            << "\nhairline signatures seen:\n" << seen;
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
    RenderReportedShape(context, shape, &candidate);
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

}  // namespace

// Rows 12-22 of the integration miss report: HairlineLineGeometryProcessor with a color FP or a
// coverage form the dedicated HairlineLineShader cannot carry.

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineDeviceMask) {
  ExpectReportedShapeServed(
      {"row12", "GP=HairlineLineGeometryProcessor;ColorFP=[];CoverageFP=[DeviceSpaceTextureEffect];"
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
  ExpectReportedShapeServed(
      {"row14",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=[ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RRectEffect)];XP=EmptyXferProcessor",
       true, false, ClipPath | ClipRRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMask) {
  ExpectReportedShapeServed(
      {"row15",
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
  ExpectReportedShapeServed(
      {"row21",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=[ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RectEffect)];XP=EmptyXferProcessor",
       true, false, ClipPath | ClipRect});
}

TGFX_TEST(AOTGeometryLayoutTest, ReportedHairlineGradientDeviceMaskRectRRect) {
  ExpectReportedShapeServed(
      {"row22",
       "GP=HairlineLineGeometryProcessor;ColorFP=[ClampedGradientEffect("
       "SingleIntervalGradientColorizer,LinearGradientLayout)];CoverageFP=[ComposeFragmentProcessor("
       "DeviceSpaceTextureEffect,RectEffect,RRectEffect)];XP=EmptyXferProcessor",
       true, false, ClipPath | ClipRect | ClipRRect});
}

namespace {

}  // namespace

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
  ExpectHairlineLineVariant({"hairline-chain-texture-dst", LayoutPaint::Image,
                             LayoutClip::PathMask, true, "PointwiseChainShader", 0, 1, 0});
}

// ---- HairlineLine on PointwiseGraphShader (GP_LAYOUT=2, RESOURCE_KIND=1: the gradient) ----

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradient) {
  ExpectHairlineLineVariant({"hairline-graph-gradient", LayoutPaint::Gradient, LayoutClip::None,
                             false, "PointwiseGraphShader", 0, 0, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-dst", LayoutPaint::Gradient,
                             LayoutClip::TwoRRects, true, "PointwiseGraphShader", 0, 0, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDeviceMask) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-device-mask", LayoutPaint::Gradient,
                             LayoutClip::PathMask, false, "PointwiseGraphShader", 0, 0, 1});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientDeviceMaskDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-device-mask-dst", LayoutPaint::Gradient,
                             LayoutClip::PathMask, true, "PointwiseGraphShader", 0, 0, 1});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientTexture) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-texture", LayoutPaint::GradientTimesImage,
                             LayoutClip::None, false, "PointwiseGraphShader", 0, 1, 0});
}

TGFX_TEST(AOTGeometryLayoutTest, HairlineGraphGradientTextureDstRead) {
  ExpectHairlineLineVariant({"hairline-graph-gradient-texture-dst",
                             LayoutPaint::GradientTimesImage, LayoutClip::TwoRRects, true,
                             "PointwiseGraphShader", 0, 1, 0});
}

}  // namespace tgfx
