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

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/DrawingManager.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/YUVData.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "utils/TestUtils.h"

namespace tgfx {

// M_PI is a POSIX extension that MSVC does not define without _USE_MATH_DEFINES, so the star
// fixtures carry their own constant.
constexpr float kStarPi = 3.14159265358979323846f;

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

// Systematic AOT-vs-runtime equivalence check. Each scene is rendered twice: once with the
// precompiled bundle loaded (PrecompiledProgramCreator serves matched variants from the AOT
// artifacts) and once with no bundle (ProgramBuilder generates the shader at runtime). The two
// renders must be byte-identical: a precompiled variant that diverges from the runtime codegen
// (e.g. the tiled-fill Dimension-normalization class of bug) shows up as a mismatch here, even for
// scenes that no screenshot baseline happens to cover.

static std::string ConsistencyBundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

// Renders the given paint over a full-surface rect into outBitmap. When useBundle is true the
// precompiled bundle is loaded so matched draws take the AOT path; otherwise the cache is unloaded
// so every draw goes through ProgramBuilder.
static void RenderPaintOnce(const Paint& paint, int width, int height, bool useBundle,
                            Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawRect(
      Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

// Renders the given image (optionally through an image filter) into outBitmap, with or without the
// precompiled bundle.
static void RenderImageOnce(const std::shared_ptr<Image>& image,
                            const std::shared_ptr<ImageFilter>& filter, int width, int height,
                            bool useBundle, Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto drawImage = filter != nullptr ? image->makeWithFilter(filter) : image;
  ASSERT_TRUE(drawImage != nullptr);
  surface->getCanvas()->drawImage(drawImage, 0, 0);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectBitmapsIdentical(const char* label, const Bitmap& aotBitmap,
                                   const Bitmap& runtimeBitmap, int width, int height) {
  auto* aotPixels = const_cast<Bitmap&>(aotBitmap).lockPixels();
  auto* runtimePixels = const_cast<Bitmap&>(runtimeBitmap).lockPixels();
  ASSERT_TRUE(aotPixels != nullptr && runtimePixels != nullptr);
  size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  int cmp = std::memcmp(aotPixels, runtimePixels, totalBytes);
  int maxDiff = 0;
  size_t diffCount = 0;
  auto* a = static_cast<const uint8_t*>(aotPixels);
  auto* r = static_cast<const uint8_t*>(runtimePixels);
  for (size_t i = 0; i < totalBytes; i++) {
    int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(r[i]));
    if (d > 0) {
      diffCount++;
    }
    if (d > maxDiff) {
      maxDiff = d;
    }
  }
  const_cast<Bitmap&>(aotBitmap).unlockPixels();
  const_cast<Bitmap&>(runtimeBitmap).unlockPixels();
  EXPECT_EQ(cmp, 0) << "AOT vs runtime render diverged for scene: " << label
                    << " (maxChannelDiff=" << maxDiff << ", diffBytes=" << diffCount << "/"
                    << totalBytes << ")";
}

static void ExpectShaderConsistent(const char* label, const std::shared_ptr<Shader>& shader,
                                   int width, int height) {
  ASSERT_TRUE(shader != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderPaintOnce(paint, width, height, true, &aotBitmap);
  RenderPaintOnce(paint, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// Renders the given paint into a Display-P3 surface, so any sRGB content is converted through a
// ColorSpaceXformEffect (the shader whose pipeline flags were folded into the CSFlags uniform).
static void RenderPaintToP3Once(const Paint& paint, int width, int height, bool useBundle,
                                Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height, false, 1, false, 0, ColorSpace::DisplayP3());
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawRect(
      Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectShaderConsistentP3(const char* label, const std::shared_ptr<Shader>& shader,
                                     int width, int height) {
  ASSERT_TRUE(shader != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderPaintToP3Once(paint, width, height, true, &aotBitmap);
  RenderPaintToP3Once(paint, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

static void ExpectImageFilterConsistent(const char* label, const std::shared_ptr<Image>& image,
                                        const std::shared_ptr<ImageFilter>& filter, int width,
                                        int height) {
  ASSERT_TRUE(image != nullptr);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderImageOnce(image, filter, width, height, true, &aotBitmap);
  RenderImageOnce(image, filter, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// Tiled texture fills: an image shader drawn over a rect produces a TiledTextureEffect for the
// non-clamp tile modes. Covers the ShaderMode values the precompiled TiledTextureFillShader
// supports (repeat/mirror/clamp-to-border), which is where the Dimension-normalization bug lived.
TGFX_TEST(AOTRenderConsistencyTest, TiledTextureFillModes) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = 200;
  int height = 200;
  ExpectShaderConsistent("tiled-repeat",
                         Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat), width,
                         height);
  ExpectShaderConsistent("tiled-mirror",
                         Shader::MakeImageShader(image, TileMode::Mirror, TileMode::Mirror), width,
                         height);
  ExpectShaderConsistent("tiled-clamp",
                         Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp), width,
                         height);
  ExpectShaderConsistent("tiled-decal",
                         Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal), width,
                         height);
  ExpectShaderConsistent("tiled-repeat-mirror",
                         Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror), width,
                         height);
}

// Gaussian blur over a tiled source: exercises GaussianBlur1DShader with a TiledTextureEffect child
// (HAS_TILED_CHILD) for the non-clamp tile modes, plus the plain-texture child for clamp.
// Fractional-offset image draw with a shader mask filter: the QuadAA vertices carry sub-1.0
// coverage, and the mask produces an Xfermode-dst coverage FP, so the chain's coverage subtree
// reads the real per-vertex coverage through the -3 unit input. Any mis-wiring (opaque where the
// true coverage belongs, or a doubled vCoverage modulation) shows up as a byte difference along
// the AA edges.
TGFX_TEST(AOTRenderConsistencyTest, AACoverageXferDstFold) {
  auto image = MakeImage("resources/apitest/imageReplacement.jpg");
  ASSERT_TRUE(image != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto maskShader = Shader::MakeLinearGradient(Point{0, 0}, Point{100, 0},
                                                 {Color::White(), Color::Transparent()}, {});
    Paint paint = {};
    paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
    canvas->drawImage(image, 50.3f, 25.4f, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("aa-xfer-dst-fold", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, ChainMaskWithSolidFill) {
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath()));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    canvas->save();
    Path clipPath = {};
    clipPath.addRoundRect(Rect::MakeXYWH(30, 30, 140, 140), 24, 24);
    canvas->clipPath(clipPath);
    Paint paint = {};
    paint.setColor(Color::Red());
    canvas->drawRect(Rect::MakeWH(160, 160), paint);
    canvas->restore();
    context->flushAndSubmit(true);
    outBitmap->allocPixels(200, 200);
    auto* pixels = outBitmap->lockPixels();
    surface->readPixels(outBitmap->info(), pixels);
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("chain-mask-solid-fill", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, ChainMaskWithSubsetLeaf) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // A round-rect clip produces a device-space mask coverage; the strict-constraint image draw
    // gives the color leaf a real subset rect. The chain must keep the two Subset writes apart.
    canvas->save();
    Path clipPath = {};
    clipPath.addRoundRect(Rect::MakeXYWH(30, 30, 140, 140), 24, 24);
    canvas->clipPath(clipPath);
    canvas->drawImageRect(image, Rect::MakeXYWH(16, 16, 80, 80), Rect::MakeWH(160, 160),
                          SamplingOptions{}, nullptr, SrcRectConstraint::Strict);
    canvas->restore();
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("chain-mask-subset-leaf", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, PerspectiveChainLeaf) {
  if (std::string(TGFX_BACKEND_NAME) != "metal") {
    // Software backends (SwiftShader) serve this shape with LSB-level precision differences;
    // Metal is the byte-exact AOT verification backend.
    GTEST_SKIP();
  }
  auto imageA = MakeImage("resources/apitest/imageReplacement.png");
  auto imageB = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // A perspective canvas matrix puts the w component into the leaf coordinate transforms; the
    // two-shader blend forces the chain route.
    Matrix matrix = {};
    matrix.setAll(1.0f, 0.0f, 40.0f, 0.0f, 1.0f, 40.0f, 0.0015f, 0.0f, 1.0f);
    canvas->setMatrix(matrix);
    auto shaderA = Shader::MakeImageShader(imageA);
    auto shaderB = Shader::MakeImageShader(imageB, TileMode::Repeat, TileMode::Repeat);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, shaderA, shaderB));
    canvas->drawRect(Rect::MakeWH(120, 120), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("perspective-chain-leaf", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, NestedTwoChildXferFold) {
  if (std::string(TGFX_BACKEND_NAME) != "metal") {
    // Software backends (SwiftShader) serve this shape with LSB-level precision differences;
    // Metal is the byte-exact AOT verification backend.
    GTEST_SKIP();
  }
  auto imageA = MakeImage("resources/apitest/imageReplacement.png");
  auto imageB = MakeImage("resources/apitest/test_timestretch.png");
  auto imageC = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr && imageC != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // Nested two-child blends: the inner blend's children receive the opaque-alpha input, and
    // its epilogue multiplies alpha 1.0 (idempotent override), while the outermost multiplies
    // the paint alpha.
    auto shaderA = Shader::MakeImageShader(imageA);
    auto shaderB = Shader::MakeImageShader(imageB);
    auto shaderC = Shader::MakeImageShader(imageC);
    auto inner = Shader::MakeBlend(BlendMode::Screen, shaderA, shaderB);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, inner, shaderC));
    paint.setAlpha(0.6f);
    canvas->drawRect(Rect::MakeXYWH(30, 30, 140, 140), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("nested-two-child-xfer-fold", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, TwoChildXferBlendFold) {
  if (std::string(TGFX_BACKEND_NAME) != "metal") {
    // Software backends (SwiftShader) serve this shape with LSB-level precision differences;
    // Metal is the byte-exact AOT verification backend.
    GTEST_SKIP();
  }
  auto imageA = MakeImage("resources/apitest/imageReplacement.png");
  auto imageB = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // Two blended image shaders produce a two-child xfer; the translucent paint alpha exercises
    // the runtime's output *= inputColor.a epilogue, and the shader mask adds a tiled coverage.
    auto shaderA = Shader::MakeImageShader(imageA);
    auto shaderB = Shader::MakeImageShader(imageB, TileMode::Repeat, TileMode::Repeat);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, shaderA, shaderB));
    paint.setAlpha(0.7f);
    auto maskShader = Shader::MakeLinearGradient(Point{0, 0}, Point{200, 0},
                                                 {Color::White(), Color::Transparent()}, {});
    paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
    canvas->drawRect(Rect::MakeXYWH(40, 40, 120, 120), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("two-child-xfer-blend-fold", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, ProgramKeyColorCoverageBoundary) {
  // The program cache key does not encode the color/coverage boundary (numColorProcessors is
  // absent from buildProgramKey), so two draws with identical processor sequences but different
  // boundaries share one cache entry. Verification result (2026-09-14, JIT and AOT both): the
  // Xfermode uniform layout makes the reuse self-consistent on this shape — its onSetData
  // writes the same uniform slots in either position — so no visible error is produced. This
  // test stays as a regression fence: any future layout asymmetry between the color and
  // coverage positions of a shared FP would turn this pair into a real misrender, and the key
  // fix (encoding the boundary) is deferred until such a case is reproduced rather than being
  // applied without a failing case.
  // A shared alpha-gradient image: the left half is opaque, the right half is half-transparent,
  // so the blue blend draw and the red mask draw produce visibly different pixels when mixed up.
  Bitmap gradient = {};
  ASSERT_TRUE(gradient.allocPixels(64, 64));
  {
    auto* pixels = static_cast<uint32_t*>(gradient.lockPixels());
    ASSERT_TRUE(pixels != nullptr);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        auto alpha = x < 32 ? 255u : 128u;
        pixels[y * 64 + x] = (alpha << 24) | 0x00FFFFFFu;
      }
    }
    gradient.unlockPixels();
  }
  auto image = Image::MakeFrom(gradient);
  ASSERT_TRUE(image != nullptr);

  auto renderScene = [&](bool withBlendDraw, bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 128, 128);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // Draw A: a SrcIn blend shader whose processor sequence is
    // [TextureEffect(gradient), Xfermode(SrcIn, DstChild)] in the color chain, with a solid
    // blue source operand.
    if (withBlendDraw) {
      Paint paintA = {};
      paintA.setShader(Shader::MakeBlend(
          BlendMode::SrcIn, Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp),
          Shader::MakeColorShader(Color::Blue())));
      canvas->drawRect(Rect::MakeXYWH(8, 8, 112, 52), paintA);
    }
    // Draw B: a solid red paint with a shader mask filter over the same gradient. Its
    // processor sequence is structurally identical — [TextureEffect(gradient),
    // Xfermode(SrcIn, DstChild)] — but the pair sits in the coverage chain. Reusing draw A's
    // program for draw B would read the paint color through the blend's uniform layout
    // instead of the geometry color, so the program key must distinguish the two.
    Paint paintB = {};
    paintB.setColor(Color::Red());
    paintB.setMaskFilter(
        MaskFilter::MakeShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp)));
    canvas->drawRect(Rect::MakeXYWH(8, 68, 112, 52), paintB);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(128, 128));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };

  Bitmap reference = {};
  renderScene(false, false, &reference);
  Bitmap mixed = {};
  renderScene(true, false, &mixed);
  Bitmap referenceAOT = {};
  renderScene(false, true, &referenceAOT);
  Bitmap mixedAOT = {};
  renderScene(true, true, &mixedAOT);
  // Compare only draw B's band (rows 60..124): the reference scene never draws A, so the upper
  // band differs by construction while the lower band must stay identical.
  auto compareBand = [&](const char* label, const Bitmap& ref, const Bitmap& mix) {
    auto* refPixels = static_cast<const uint32_t*>(const_cast<Bitmap&>(ref).lockPixels());
    auto* mixPixels = static_cast<uint32_t*>(const_cast<Bitmap&>(mix).lockPixels());
    ASSERT_TRUE(refPixels != nullptr && mixPixels != nullptr);
    int mismatches = 0;
    uint32_t firstRef = 0;
    uint32_t firstMix = 0;
    for (int y = 60; y < 124; ++y) {
      for (int x = 0; x < 128; ++x) {
        auto refValue = refPixels[y * 128 + x];
        auto mixValue = mixPixels[y * 128 + x];
        if (refValue != mixValue) {
          if (mismatches == 0) {
            firstRef = refValue;
            firstMix = mixValue;
          }
          ++mismatches;
        }
      }
    }
    const_cast<Bitmap&>(ref).unlockPixels();
    const_cast<Bitmap&>(mix).unlockPixels();
    EXPECT_EQ(mismatches, 0) << label << ": mask draw changed after the blend draw shared its "
                             << "program key (first ref=" << firstRef << " mix=" << firstMix
                             << ")";
  };
  compareBand("jit", reference, mixed);
  compareBand("aot", referenceAOT, mixedAOT);
}

TGFX_TEST(AOTRenderConsistencyTest, TextureFillTriangulatedShapeAA) {
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 240, 240);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // A convex polygon larger than the triangulator's minimum size (162px) triangulates, so
    // this anti-aliased shape draw reaches DefaultGeometryProcessor with AAType::Coverage and
    // exactly one TextureEffect color processor — the shape the TextureFill matcher accepts.
    // The precompiled vertex template does not consume the GP's coverage attribute, so the
    // match must not be served from the bundle: the runtime codegen carries the edge coverage
    // through a varying.
    Path path = {};
    for (int i = 0; i < 5; ++i) {
      float angle = static_cast<float>(i) * 72.0f - 90.0f;
      float x = 120.0f + 95.0f * cosf(angle * kStarPi / 180.0f);
      float y = 120.0f + 95.0f * sinf(angle * kStarPi / 180.0f);
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    path.close();
    auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/mandrill_128.png"));
    ASSERT_TRUE(image != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp));
    canvas->drawShape(Shape::MakeFrom(std::move(path)), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(240, 240));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("texture-fill-triangulated-shape-aa", aotBitmap, runtimeBitmap, 240, 240);
}

TGFX_TEST(AOTRenderConsistencyTest, LUTGradientMaskFold) {
  Color red = {1.f, 0.f, 0.f, 1.f};
  Color green = {0.f, 1.f, 0.f, 1.f};
  Color blue = {0.f, 0.f, 1.f, 1.f};
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // A rotated round-rect path rasterizes through a coverage mask texture; the 17-stop
    // gradient bakes into a LUT colorizer.
    Path path = {};
    path.addRoundRect(Rect::MakeWH(100, 100), 20, 20);
    Paint paint = {};
    paint.setShader(Shader::MakeLinearGradient(Point{0.f, 0.f}, Point{25.f, 150.f},
                                               {red, green, blue, green, red, blue, red, green, red,
                                                green, blue, green, red, blue, red, green, blue},
                                               {}));
    auto matrix = Matrix::MakeRotate(15, 50, 50);
    matrix.postScale(1.5f, 0.9f, 50, 50);
    matrix.postTranslate(40, 30);
    canvas->setMatrix(matrix);
    canvas->drawPath(path, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("lut-gradient-mask-fold", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, AtlasTextConstColorFold) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto font = Font(typeface, 50);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(textBlob != nullptr);
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 100);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    Paint paint;
    paint.setColorFilter(ColorFilter::Blend(Color::Red(), BlendMode::Multiply));
    canvas->drawTextBlob(textBlob, 25, 60, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 100));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("atlas-text-const-fold", aotBitmap, runtimeBitmap, 200, 100);
}

TGFX_TEST(AOTRenderConsistencyTest, AtlasTextGradientFold) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto font = Font(typeface, 50);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(textBlob != nullptr);
  auto textBounds = textBlob->getTightBounds();
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 100);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto gradientShader = Shader::MakeLinearGradient(Point{0, 0}, Point{textBounds.width(), 0},
                                                     {Color::Red(), Color::Blue()}, {});
    Paint paint;
    paint.setShader(gradientShader);
    canvas->drawTextBlob(textBlob, 25, 60, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 100));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("atlas-text-gradient-fold", aotBitmap, runtimeBitmap, 200, 100);
}

TGFX_TEST(AOTRenderConsistencyTest, MeshTextureAndColorsXferSrcFold) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 200, 200);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    Point positions[] = {{50, 50}, {150, 50}, {150, 150}, {50, 150}};
    Point texCoords[] = {{0, 0}, {imageWidth, 0}, {imageWidth, imageHeight}, {0, imageHeight}};
    Color colors[] = {Color::FromRGBA(255, 0, 0, 128), Color::FromRGBA(0, 255, 0, 128),
                      Color::FromRGBA(0, 0, 255, 128), Color::FromRGBA(255, 255, 0, 128)};
    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    auto mesh =
        Mesh::MakeCopy(MeshTopology::Triangles, 4, positions, texCoords, colors, 6, indices);
    ASSERT_TRUE(mesh != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image));
    canvas->drawMesh(mesh, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(200, 200));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  renderOnce(true, &aotBitmap);
  renderOnce(false, &runtimeBitmap);
  ExpectBitmapsIdentical("mesh-texture-colors-xfer-src", aotBitmap, runtimeBitmap, 200, 200);
}

TGFX_TEST(AOTRenderConsistencyTest, GaussianBlurTileModes) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width() + 40;
  int height = image->height() + 40;
  ExpectImageFilterConsistent("blur-clamp", image, ImageFilter::Blur(6, 6, TileMode::Clamp), width,
                              height);
  ExpectImageFilterConsistent("blur-repeat", image, ImageFilter::Blur(6, 6, TileMode::Repeat),
                              width, height);
  ExpectImageFilterConsistent("blur-mirror", image, ImageFilter::Blur(6, 6, TileMode::Mirror),
                              width, height);
  ExpectImageFilterConsistent("blur-decal", image, ImageFilter::Blur(6, 6, TileMode::Decal), width,
                              height);
}

// Color-space conversions into a Display-P3 surface: exercises the color-space operator of
// PointwiseDirectShader and TexturedEffectShader, whose seven pipeline steps are selected by the
// CSFlags runtime uniform. A precompiled variant that reads a flag differently from the runtime
// codegen would show up as a byte mismatch here.
TGFX_TEST(AOTRenderConsistencyTest, ColorSpaceXformModes) {
  int width = 200;
  int height = 200;
  ExpectShaderConsistentP3("csx-srgb-color", Shader::MakeColorShader(Color::Green()), width,
                           height);
  ExpectShaderConsistentP3("csx-srgb-linear",
                           Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(200, 0),
                                                      {Color::Green(), Color::Red()}, {}),
                           width, height);
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  ExpectShaderConsistentP3("csx-srgb-image",
                           Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp), width,
                           height);
}

// Gradient layouts (linear/radial/conic/diamond) are now selected by the LayoutType runtime uniform
// instead of a compile-time dimension. Each layout must still compute t identically to the runtime
// layout FP, so a divergent LayoutType branch or a missing Bias/Scale would show up as a byte
// mismatch here. Two-stop uses SingleInterval, multi-stop uses DualInterval/Texture colorizers.
TGFX_TEST(AOTRenderConsistencyTest, GradientLayoutModes) {
  int width = 200;
  int height = 200;
  Point center = Point::Make(100, 100);
  std::vector<Color> twoStops = {Color::Green(), Color::Red()};
  std::vector<Color> multiStops = {Color::Green(), Color::Blue(), Color::Red()};
  std::vector<float> multiPositions = {0.0f, 0.4f, 1.0f};

  ExpectShaderConsistent(
      "grad-linear",
      Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(200, 0), twoStops, {}), width,
      height);
  ExpectShaderConsistent("grad-radial", Shader::MakeRadialGradient(center, 100, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-conic", Shader::MakeConicGradient(center, 0, 360, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-diamond", Shader::MakeDiamondGradient(center, 100, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-radial-multi",
                         Shader::MakeRadialGradient(center, 100, multiStops, multiPositions), width,
                         height);
  ExpectShaderConsistent("grad-conic-multi",
                         Shader::MakeConicGradient(center, 0, 360, multiStops, multiPositions),
                         width, height);
  // The unrolled colorizer kicks in at 3+ intervals: a 6-stop gradient exercises the mid-depth
  // threshold pack, and a 9-stop gradient maxes out all 8 intervals.
  std::vector<Color> sixStops = {Color::Red(),   Color::Green(), Color::Blue(),
                                 Color::White(), Color::Black(), Color::FromRGBA(255, 255, 0)};
  std::vector<float> sixPositions = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
  std::vector<Color> nineStops = {Color::Red(),
                                  Color::Green(),
                                  Color::Blue(),
                                  Color::White(),
                                  Color::Black(),
                                  Color::FromRGBA(255, 255, 0),
                                  Color::FromRGBA(0, 255, 255),
                                  Color::FromRGBA(255, 0, 255),
                                  Color::FromRGBA(128, 128, 128)};
  std::vector<float> ninePositions = {0.0f,   0.125f, 0.25f,  0.375f, 0.5f,
                                      0.625f, 0.75f,  0.875f, 1.0f};
  ExpectShaderConsistent(
      "grad-linear-6stop",
      Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(200, 0), sixStops, sixPositions),
      width, height);
  ExpectShaderConsistent("grad-radial-9stop",
                         Shader::MakeRadialGradient(center, 100, nineStops, ninePositions), width,
                         height);
}

// Renders an antialiased circle (EllipseGeometryProcessor) under an optional clip into outBitmap.
// clipMode: 0 = no clip, 1 = antialiased clipRect (a device-space RectEffect coverage), 2 = antialiased
// non-rect clipPath (device-space mask texture coverage). This exercises the EllipseFillShader
// HAS_COVERAGE dimension whose mask is sampled in device space via DeviceCoordMatrix * gl_FragCoord;
// a divergent coordinate transform would show up as a byte mismatch against the runtime path.
static void RenderClippedCircleOnce(int clipMode, int width, int height, bool useBundle,
                                    Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->save();
  if (clipMode == 1) {
    canvas->clipRect(Rect::MakeLTRB(20, 20, 180, 180));
  } else if (clipMode == 2) {
    Path clipPath = {};
    clipPath.addOval(Rect::MakeLTRB(20, 20, 180, 180));
    canvas->clipPath(clipPath);
  }
  Paint paint = {};
  paint.setColor(Color::Red());
  paint.setAntiAlias(true);
  canvas->drawCircle(100, 100, 80, paint);
  canvas->restore();
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectClippedCircleConsistent(const char* label, int clipMode, int width, int height) {
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderClippedCircleOnce(clipMode, width, height, true, &aotBitmap);
  RenderClippedCircleOnce(clipMode, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// EllipseFillShader HAS_COVERAGE dimension: an antialiased circle drawn under a clip pulls in a
// coverage FP (RectEffect for a rect clip, a device-space mask texture for a non-rect clip). The
// AOT path samples that coverage in device space; this verifies it is byte-identical to the runtime
// ProgramBuilder path, catching any DeviceCoordMatrix / gl_FragCoord misalignment.
TGFX_TEST(AOTRenderConsistencyTest, EllipseFillCoverageModes) {
  int width = 200;
  int height = 200;
  ExpectClippedCircleConsistent("ellipse-no-clip", 0, width, height);
  ExpectClippedCircleConsistent("ellipse-aarect-clip", 1, width, height);
  ExpectClippedCircleConsistent("ellipse-device-mask-clip", 2, width, height);
}

struct ColorFilterRenderStats {
  uint32_t hits = 0;
  uint32_t pipelines = 0;
  uint32_t noMatchingRule = 0;
  AOTDrawStats draws = {};
  ProgramCacheStats programs = {};
};

static void RenderImageWithColorFilterOnce(const std::shared_ptr<Image>& image,
                                           const std::shared_ptr<ColorFilter>& colorFilter,
                                           int width, int height, bool useBundle,
                                           bool decompositionEnabled, bool useAnalyticClip,
                                           bool forceTexture2D, Bitmap* outBitmap,
                                           ColorFilterRenderStats* outStats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleSize, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleSize));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  cache->setDiagnosticRecordingEnabled(false);
  context->globalCache()->clearPrograms();
  auto sourceImage = image;
  std::shared_ptr<Surface> sourceSurface = nullptr;
  if (forceTexture2D) {
    sourceSurface = Surface::Make(context, width, height, false, 1, true);
    ASSERT_TRUE(sourceSurface != nullptr);
    sourceSurface->getCanvas()->drawImage(image, 0, 0);
    sourceImage = sourceSurface->makeImageSnapshot();
    ASSERT_TRUE(sourceImage != nullptr);
    context->flushAndSubmit(true);
  }
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setColorFilter(colorFilter);
  auto canvas = surface->getCanvas();
  if (useAnalyticClip) {
    canvas->clipRect(Rect::MakeLTRB(8, 8, width - 8, height - 8), true);
  }
  canvas->drawImage(sourceImage, 0, 0, &paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  outStats->hits = cache->hitCount();
  outStats->pipelines = cache->aotStageCount(PrecompiledAOTStage::PipelineCreated);
  outStats->noMatchingRule = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  outStats->draws = cache->drawStats();
  outStats->programs = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->setDecompositionEnabled(true);
  cache->unload();
  context->globalCache()->clearPrograms();
}

TGFX_TEST(AOTRenderConsistencyTest, TexturedEffect2D) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto colorFilter = ColorFilter::Matrix(swapRedBlue);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, colorFilter, width, height, false, false, false, true,
                                 &reference, &referenceStats);
  RenderImageWithColorFilterOnce(image, colorFilter, width, height, true, false, false, true,
                                 &candidate, &candidateStats);
  EXPECT_GE(candidateStats.hits, 1u);
  EXPECT_GE(candidateStats.pipelines, 1u);
  EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 1u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 0u);
  ExpectBitmapsIdentical("textured-effect-2d", candidate, reference, width, height);
}

// The decomposition switch must gate every entry point, including the direct-draw rewrite in
// StandardDrawOp::prepareDecomposedProgram, which previously only checked bundle loading. The
// trigger scene is the triangulated AA polygon (its GP coverage makes the plain matcher pass,
// so the chain rewrite serves it): with the switch on the draw records a direct chain rewrite;
// with the switch off the rewrite must not run and the draw falls back to the runtime builder.
// Note a separate boundary this test documents: the switch does not gate the matcher's own
// chain rule — a draw the plain matcher serves directly (e.g. a simple filter chain) still uses
// the chain kernel with the switch off. That rule-level gating is a semantic decision recorded
// in the audit report, not a defect fixed here.
TGFX_TEST(AOTRenderConsistencyTest, DecompositionSwitchControlsDirectDrawEntry) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  context->globalCache()->resetProgramStats();
  cache->resetStats();
  auto renderOnce = [&](bool decompositionEnabled, AOTDrawStats* outDraws,
                        ProgramCacheStats* outPrograms, Bitmap* outBitmap) {
    ScopedAOTDeliberateMiss deliberate(context, !decompositionEnabled);
    auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleSize, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleSize));
    context->globalCache()->clearPrograms();
    cache->setDecompositionEnabled(decompositionEnabled);
    cache->setDiagnosticRecordingEnabled(true);
    auto surface = Surface::Make(context, 240, 240);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    Path path = {};
    for (int i = 0; i < 5; ++i) {
      float angle = static_cast<float>(i) * 72.0f - 90.0f;
      float x = 120.0f + 95.0f * cosf(angle * kStarPi / 180.0f);
      float y = 120.0f + 95.0f * sinf(angle * kStarPi / 180.0f);
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    path.close();
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp));
    canvas->drawShape(Shape::MakeFrom(std::move(path)), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(240, 240));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    *outDraws = cache->drawStats();
    *outPrograms = context->globalCache()->programStats();
    cache->setDiagnosticRecordingEnabled(false);
    cache->setDecompositionEnabled(true);
    cache->unload();
  };
  AOTDrawStats onDraws = {};
  AOTDrawStats offDraws = {};
  ProgramCacheStats onPrograms = {};
  ProgramCacheStats offPrograms = {};
  Bitmap onBitmap = {};
  Bitmap offBitmap = {};
  renderOnce(true, &onDraws, &onPrograms, &onBitmap);
  renderOnce(false, &offDraws, &offPrograms, &offBitmap);
  EXPECT_GE(onDraws.directChainDraws, 1u);
  EXPECT_EQ(onPrograms.programBuilderCreations, 0u);
  EXPECT_EQ(onPrograms.excludedProgramBuilderCreations, 0u);
  EXPECT_EQ(offDraws.directChainDraws, onDraws.directChainDraws);
  EXPECT_GE(offPrograms.programBuilderCreations, 1u);
  EXPECT_EQ(offPrograms.excludedProgramBuilderCreations, offPrograms.programBuilderCreations);
  EXPECT_FALSE(cache->deliberateMissMarking());
  ExpectBitmapsIdentical("decomposition-switch-direct-entry", onBitmap, offBitmap, 240, 240);
}

// Bundle identity and program-cache generations (audit F04/F06). Sequence A: draw without a
// bundle first (a JIT program occupies the cache key), then load — the load must invalidate
// that program so the redraw creates and caches the AOT program (previously the JIT program
// kept occupying the key). Sequence B: unload — the cached AOT program must not be served
// after its bundle is gone; the draw falls back to the runtime builder. Sequence C/D: a
// tampered content byte and a tampered profile tag must both be rejected at load time, and
// failed loads must not bump the generation.
TGFX_TEST(AOTRenderConsistencyTest, BundleIdentityAndGenerationLifecycle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleSize, 0u);

  auto drawOnce = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, 128, 128);
    ASSERT_TRUE(surface != nullptr);
    auto image = MakeImage("resources/apitest/mandrill_128.png");
    ASSERT_TRUE(image != nullptr);
    Paint paint = {};
    std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
    surface->getCanvas()->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(128, 128));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };

  auto generation0 = cache->bundleGeneration();
  cache->unload();  // start from the no-bundle state (the constructor auto-loads)
  EXPECT_EQ(cache->bundleGeneration(), generation0 + 1u);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->resetProgramStats();

  // Sequence A: JIT first, then load.
  Bitmap jitBitmap = {};
  {
    ScopedAOTDeliberateMiss deliberate(context);
    drawOnce(&jitBitmap);
  }
  auto jitCreations = context->globalCache()->programStats().programBuilderCreations;
  EXPECT_GE(jitCreations, 1u);
  EXPECT_EQ(context->globalCache()->programStats().excludedProgramBuilderCreations, jitCreations);
  EXPECT_FALSE(cache->deliberateMissMarking());
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleSize));
  EXPECT_EQ(cache->bundleGeneration(), generation0 + 2u);  // unload + successful load
  Bitmap aotBitmap = {};
  drawOnce(&aotBitmap);
  const auto& aotPhaseStats = context->globalCache()->programStats();
  EXPECT_GE(aotPhaseStats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(aotPhaseStats.programBuilderCreations, jitCreations);
  EXPECT_EQ(aotPhaseStats.excludedProgramBuilderCreations, jitCreations);
  ExpectBitmapsIdentical("bundle-lifecycle-jit-then-aot", aotBitmap, jitBitmap, 128, 128);

  // Sequence B: unload invalidates the cached AOT program.
  auto aotCreations = aotPhaseStats.precompiledArtifactCreations;
  cache->unload();
  EXPECT_EQ(cache->bundleGeneration(), generation0 + 3u);
  Bitmap staleBitmap = {};
  {
    ScopedAOTDeliberateMiss deliberate(context);
    drawOnce(&staleBitmap);
  }
  EXPECT_EQ(context->globalCache()->programStats().precompiledArtifactCreations, aotCreations);
  EXPECT_GE(context->globalCache()->programStats().programBuilderCreations, jitCreations + 1u);
  EXPECT_EQ(context->globalCache()->programStats().excludedProgramBuilderCreations,
            context->globalCache()->programStats().programBuilderCreations);
  EXPECT_FALSE(cache->deliberateMissMarking());
  ExpectBitmapsIdentical("bundle-lifecycle-unload", staleBitmap, aotBitmap, 128, 128);
  cache->setDiagnosticRecordingEnabled(false);

  // Sequence C: tamper with one byte inside the reflection pool. That region is stored
  // uncompressed and its layout checks stay structurally valid, so only the identity hash can
  // reject the modified content.
  std::vector<uint8_t> tampered(bundleData, bundleData + bundleSize);
  uint32_t reflectionOffset = static_cast<uint32_t>(tampered[44]) |
                              (static_cast<uint32_t>(tampered[45]) << 8) |
                              (static_cast<uint32_t>(tampered[46]) << 16) |
                              (static_cast<uint32_t>(tampered[47]) << 24);
  ASSERT_GT(tampered.size(), reflectionOffset + 8u);
  tampered[reflectionOffset + (tampered.size() - reflectionOffset) / 2] ^= 0xFF;
  EXPECT_FALSE(cache->loadBundle(tampered.data(), tampered.size()));

  // Sequence D: tamper with the profile tag (backend identity).
  std::vector<uint8_t> wrongTag(bundleData, bundleData + bundleSize);
  wrongTag[48] = static_cast<uint8_t>('x');
  EXPECT_FALSE(cache->loadBundle(wrongTag.data(), wrongTag.size()));

  // Failed loads must not change the generation or the loaded state.
  EXPECT_EQ(cache->bundleGeneration(), generation0 + 3u);
  EXPECT_FALSE(cache->isLoaded());
}

// Three pointwise operators on a plain-sampled texture are served by the fused chain kernel in
// a single pass: the DAG planner serves the whole graph without materialization (the tail
// planner previously split the same graph into two fixed-slot passes with one RGBA8
// intermediate). Multi-pass planning stays covered by the device-space unit test.
TGFX_TEST(AOTRenderConsistencyTest, LinearChainSinglePass) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  std::array<float, 20> swapRedGreen = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  auto matrixThenLuma = ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::Luma());
  auto chain = ColorFilter::Compose(matrixThenLuma, ColorFilter::Matrix(swapRedGreen));
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true, &reference,
                                 &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                 &candidateStats);
  EXPECT_GE(candidateStats.hits, 1u);
  EXPECT_GE(candidateStats.pipelines, 1u);
  EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 1u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 0u);
  EXPECT_EQ(candidateStats.draws.renderTargetSwitches, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateReadBytes, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateWriteBytes, 0u);
  EXPECT_EQ(candidateStats.draws.peakTemporaryBytes, 0u);
  ExpectBitmapsIdentical("linear-chain-matrix-luma-matrix", candidate, reference, width, height);
}

TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailPassesPreserveCoordinateDomains) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  auto sourceSurface = Surface::Make(context, image->width(), image->height(), false, 1, true);
  ASSERT_NE(sourceSurface, nullptr);
  {
    ScopedAOTStatsPause pause(context, true);
    sourceSurface->getCanvas()->drawImage(image, 0, 0);
    context->flushAndSubmit(true);
  }
  auto source = context->proxyProvider()->wrapExternalTexture(sourceSurface->getBackendTexture());
  ASSERT_NE(source, nullptr);
  const std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto render = [&](bool deviceSource, bool useBundle, const Point& offset, Bitmap* bitmap,
                    AOTDrawStats* draws, ProgramCacheStats* programs) {
    if (useBundle) {
      auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_TRUE(cache->loadBundle(bundle.first, bundle.second));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause pause(context, !useBundle);
    cache->setDecompositionEnabled(useBundle);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->clearPrograms();
    context->globalCache()->resetProgramStats();
    auto target = RenderTargetProxy::Make(context, 64, 64, false);
    ASSERT_NE(target, nullptr);
    auto allocator = context->drawingAllocator();
    PlacementPtr<FragmentProcessor> processor = nullptr;
    if (deviceSource) {
      processor = DeviceSpaceTextureEffect::Make(allocator, source, Matrix::MakeTrans(3, 5));
    } else {
      processor = TextureEffect::Make(allocator, source);
    }
    int opCount = deviceSource ? 3 : 17;
    for (int index = 0; index < opCount; ++index) {
      processor = FragmentProcessor::Compose(
          allocator, std::move(processor), ColorMatrixFragmentProcessor::Make(allocator, swapRedBlue));
    }
    ASSERT_TRUE(context->drawingManager()->fillRTWithFP(target, std::move(processor), 0, offset));
    context->flushAndSubmit(true);
    auto rt = target->getRenderTarget();
    ASSERT_NE(rt, nullptr);
    auto surface = Surface::MakeFrom(context, rt->getBackendRenderTarget(), rt->origin());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(bitmap->allocPixels(64, 64));
    auto pixels = bitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    EXPECT_TRUE(surface->readPixels(bitmap->info(), pixels));
    bitmap->unlockPixels();
    *draws = cache->drawStats();
    *programs = context->globalCache()->programStats();
    cache->setDiagnosticRecordingEnabled(false);
    cache->setDecompositionEnabled(true);
    cache->unload();
  };
  for (bool deviceSource : {false, true}) {
    for (Point offset : {Point::Zero(), Point::Make(11, 7)}) {
      SCOPED_TRACE(deviceSource);
      SCOPED_TRACE(offset.x);
      Bitmap reference;
      Bitmap candidate;
      AOTDrawStats referenceDraws;
      AOTDrawStats candidateDraws;
      ProgramCacheStats referencePrograms;
      ProgramCacheStats candidatePrograms;
      render(deviceSource, false, offset, &reference, &referenceDraws, &referencePrograms);
      render(deviceSource, true, offset, &candidate, &candidateDraws, &candidatePrograms);
      uint64_t passCount = deviceSource ? 2 : 9;
      EXPECT_EQ(candidatePrograms.programBuilderCreations, 0u);
      EXPECT_GE(candidatePrograms.precompiledArtifactCreations, 1u);
      EXPECT_EQ(candidateDraws.completeAOTDraws, 1u);
      EXPECT_EQ(candidateDraws.atomicFallbacks, 0u);
      EXPECT_EQ(candidateDraws.kernelInvocations, passCount);
      EXPECT_EQ(candidateDraws.planMaterializedEdges, passCount - 1);
      EXPECT_EQ(candidateDraws.fpFlattenEdges, 0u);
      ExpectBitmapsIdentical("offscreen-tail-coordinates", candidate, reference, 64, 64);
    }
  }
}

TGFX_TEST(AOTRenderConsistencyTest, LongLinearChainExecutesMaterializedTailPasses) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  const std::array<float, 20> rotateRGB = {0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                           1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  for (size_t opCount : {size_t{15}, size_t{16}, size_t{17}}) {
    SCOPED_TRACE(opCount);
    std::shared_ptr<ColorFilter> chain = nullptr;
    for (size_t index = 0; index < opCount; ++index) {
      chain = ColorFilter::Compose(chain, ColorFilter::Matrix(rotateRGB));
    }
    Bitmap reference;
    Bitmap candidate;
    ColorFilterRenderStats referenceStats;
    ColorFilterRenderStats candidateStats;
    RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true,
                                   &reference, &referenceStats);
    RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true,
                                   &candidate, &candidateStats);
    uint64_t passCount = opCount == 15 ? 1 : (opCount + 1) / 2;
    const auto& draws = candidateStats.draws;
    EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
    EXPECT_EQ(candidateStats.noMatchingRule, 0u);
    EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 1u);
    EXPECT_EQ(draws.draws, 1u);
    EXPECT_EQ(draws.completeAOTDraws, 1u);
    EXPECT_EQ(draws.atomicFallbacks, 0u);
    EXPECT_EQ(draws.kernelInvocations, passCount);
    EXPECT_EQ(draws.offscreenTargets, passCount - 1);
    EXPECT_EQ(draws.materializedEdges, passCount - 1);
    EXPECT_EQ(draws.planMaterializedEdges, passCount - 1);
    EXPECT_EQ(draws.fpFlattenEdges, 0u);
    if (passCount > 1) {
      EXPECT_EQ(draws.offscreenPlanDraws, 1u);
      EXPECT_EQ(draws.planPassHistogram.back(), 1u);
    }
    auto bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 * (passCount - 1);
    EXPECT_EQ(draws.intermediateReadBytes, bytes);
    EXPECT_EQ(draws.intermediateWriteBytes, bytes);
    EXPECT_EQ(draws.peakTemporaryBytes, bytes);
    ExpectBitmapsIdentical("long-linear-chain-tail", candidate, reference, width, height);
  }
}

// P4 retry semantics, risk one: color-filter retention. addDrawOp merges an
// affectsTransparentBlack color filter into the shader (shader->makeWithColorFilter) before
// building the leading processor, so the filter travels inside the ColorFilterShader's SrcIn
// tree rather than as a trailing processor. When the chain route refuses that tree and the
// in-plan retry rebuilds the color chain, the rebuild must start from the merged shader: using
// the raw brush.shader would drop the ColorFilterShader wrapper entirely — the filter AND the
// alpha mask — and replace the whole color semantics with the bare shader. The scene pairs a
// two-gradient blend (guaranteed chain refusal, the retry is non-vacuous) with an offset-red
// matrix (affectsTransparentBlack, so the merge branch is taken). A dropped filter shows up as a
// systematic ~64-level red shift on the opaque region, far beyond the <=1-LSB-per-edge
// materialization budget; a correct retry differs from the untouched reference only by the
// documented quantization.
TGFX_TEST(AOTRenderConsistencyTest, RetryRebuildKeepsTransparentBlackColorFilter) {
  if (std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr) {
    GTEST_SKIP() << "The legacy switch restores construction-time materialization; this test "
                   "asserts the planned path";
  }
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  // A DstOver mode filter is affectsTransparentBlack (transparent input falls back to the filter
  // color), which is the exact condition addDrawOp uses to merge the filter into the shader
  // (ColorFilterShader) instead of appending it as a trailing processor. Note a matrix filter
  // with an alpha-row bias would also qualify, but ValidateForFusion deliberately refuses to
  // fuse such matrices (the bias breaks the source-alpha constraint), so its materialized
  // offscreen fill has no precompiled service today — an existing coverage gap this test
  // intentionally stays away from (documented in the audit report).
  const Color filterColor = Color(0.5f, 0.0f, 0.0f, 0.5f);
  constexpr int size = 96;
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    // Alpha-0.6 stops keep the blend output partially transparent, so the filter's unpremul ->
    // matrix -> premul round trip is not an identity (an opaque input would clamp the alpha
    // offset away and make the whole wrapper a no-op).
    auto gradientA = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size),
                                                 {Color(1, 0, 0, 0.6f), Color(0, 0, 1, 0.6f)});
    auto gradientB = Shader::MakeLinearGradient(Point::Make(size, 0), Point::Make(0, size),
                                                 {Color(0, 1, 0, 0.6f), Color(1, 1, 0, 0.6f)});
    ASSERT_TRUE(gradientA != nullptr && gradientB != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, gradientA, gradientB));
    paint.setColorFilter(ColorFilter::Blend(filterColor, BlendMode::DstOver));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  // Control: the same scene without the color filter. If the filter is applied anywhere in the
  // reference path, these pixels must differ from the reference above.
  Bitmap noFilter = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto gradientA = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size),
                                                 {Color(1, 0, 0, 1), Color(0, 0, 1, 1)});
    auto gradientB = Shader::MakeLinearGradient(Point::Make(size, 0), Point::Make(0, size),
                                                 {Color(0, 1, 0, 1), Color(1, 1, 0, 1)});
    Paint paint = {};
    paint.setAlpha(51.0f / 255.0f);
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, gradientA, gradientB));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(noFilter.allocPixels(size, size));
    auto* pixels = noFilter.lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(noFilter.info(), pixels));
    noFilter.unlockPixels();
    auto* nf = static_cast<const uint32_t*>(const_cast<Bitmap&>(noFilter).lockPixels());
    auto* rf = static_cast<const uint32_t*>(const_cast<Bitmap&>(reference).lockPixels());
    printf("[RetryColorFilter] control pixel(48,48) noFilter=%08x reference=%08x\n",
           nf[48 * size + 48], rf[48 * size + 48]);
    const_cast<Bitmap&>(noFilter).unlockPixels();
    const_cast<Bitmap&>(reference).unlockPixels();
  }
  Bitmap candidate = {};
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    cache->resetStats();
    cache->setDiagnosticRecordingEnabled(true);
    renderScene(&candidate);
    auto stats = cache->drawStats();
    cache->setDiagnosticRecordingEnabled(false);
    printf("[RetryColorFilter] completeAOT=%u fpFlattenEdges=%u\n",
           static_cast<unsigned>(stats.completeAOTDraws),
           static_cast<unsigned>(stats.fpFlattenEdges));
    fflush(stdout);
    // Non-vacuous: the retry must actually have materialized and served the draw.
    EXPECT_GE(stats.completeAOTDraws, 1u);
    EXPECT_GE(stats.fpFlattenEdges, 1u);
  }
  auto* refPixels = static_cast<const uint32_t*>(const_cast<Bitmap&>(reference).lockPixels());
  auto* candPixels = static_cast<uint32_t*>(candidate.lockPixels());
  ASSERT_TRUE(refPixels != nullptr && candPixels != nullptr);
  int maxDiff = 0;
  long long diffCount = 0;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      auto refValue = refPixels[y * size + x];
      auto candValue = candPixels[y * size + x];
      int pixelDiff = 0;
      for (int channel = 0; channel < 4; ++channel) {
        int shift = channel * 8;
        int value = std::abs(static_cast<int>((refValue >> shift) & 0xFF) -
                             static_cast<int>((candValue >> shift) & 0xFF));
        pixelDiff = std::max(pixelDiff, value);
      }
      if (pixelDiff > 0) {
        ++diffCount;
      }
      maxDiff = std::max(maxDiff, pixelDiff);
    }
  }
  const_cast<Bitmap&>(reference).unlockPixels();
  candidate.unlockPixels();
  {
    // Attribution probe: the offset-red filter shifts r by 64/255 on the opaque region. Print a
    // few sample pixels from both renders to see which side carries the filter.
    auto* refProbe = static_cast<const uint32_t*>(const_cast<Bitmap&>(reference).lockPixels());
    auto* candProbe = static_cast<uint32_t*>(candidate.lockPixels());
    for (auto& [px, py] : {std::pair<int, int>{48, 48}, {24, 72}, {72, 24}}) {
      printf("[RetryColorFilter] pixel(%d,%d) ref=%08x cand=%08x\n", px, py,
             refProbe[py * size + px], candProbe[py * size + px]);
    }
    const_cast<Bitmap&>(reference).unlockPixels();
    candidate.unlockPixels();
  }
  printf("[RetryColorFilter] maxChannelDiff=%d differing=%lld/%d\n", maxDiff, diffCount,
         size * size);
  fflush(stdout);
  // The materialization path costs at most 1 LSB per materialized edge; a dropped filter is a
  // systematic ~64-level red shift (0.25 * 255). The bound separates the two unambiguously.
  EXPECT_LE(maxDiff, 4);
}

// P4 retry semantics, risk two: coverage retention. A draw carrying a shader mask filter keeps
// the mask as a coverage processor; when the chain route refuses the color tree and the in-plan
// retry swaps the materialized tree into the op, the swap must only replace the color processors
// — the mask coverage stays on the op and the plain route composites it. The scene pairs a
// two-gradient blend (guaranteed chain refusal, so the retry and its swap are non-vacuous) with
// a decal image-shader mask filter. A dropped mask would paint the full rect with the blended
// color; a correct retry paints only where the mask has alpha, within the materialization
// quantization budget.
TGFX_TEST(AOTRenderConsistencyTest, RetryRebuildKeepsMaskCoverage) {
  // Under the legacy switch this test still renders the bundle segment and prints the candidate
  // fingerprint, so a planned-vs-legacy double run can verify the fallback equivalence (the
  // swapped materialized tree must match the construction-time rewrite byte for byte); only the
  // planned-path assertions are skipped.
  bool legacy = std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr;
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  auto maskImage = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_NE(maskImage, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto gradientA = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size),
                                                 {Color(1, 0, 0, 1), Color(0, 0, 1, 1)});
    auto gradientB = Shader::MakeLinearGradient(Point::Make(size, 0), Point::Make(0, size),
                                                 {Color(0, 1, 0, 1), Color(1, 1, 0, 1)});
    ASSERT_TRUE(gradientA != nullptr && gradientB != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, gradientA, gradientB));
    auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Decal, TileMode::Decal);
    ASSERT_NE(maskShader, nullptr);
    paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  Bitmap candidate = {};
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    cache->resetStats();
    cache->setDiagnosticRecordingEnabled(true);
    renderScene(&candidate);
    auto stats = cache->drawStats();
    cache->setDiagnosticRecordingEnabled(false);
    printf("[RetryMaskCoverage] completeAOT=%u fpFlattenEdges=%u\n",
           static_cast<unsigned>(stats.completeAOTDraws),
           static_cast<unsigned>(stats.fpFlattenEdges));
    fflush(stdout);
    // Non-vacuous: the retry materialized and the draw was served.
    if (!legacy) {
      EXPECT_GE(stats.fpFlattenEdges, 1u);
    }
  }
  auto* refPixels = static_cast<const uint32_t*>(const_cast<Bitmap&>(reference).lockPixels());
  auto* candPixels = static_cast<uint32_t*>(candidate.lockPixels());
  ASSERT_TRUE(refPixels != nullptr && candPixels != nullptr);
  int maxDiff = 0;
  long long diffCount = 0;
  long long fullPaintCount = 0;
  const uint32_t white = 0xFFFFFFFFu;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      auto refValue = refPixels[y * size + x];
      auto candValue = candPixels[y * size + x];
      if (candValue == white && refValue != white) {
        ++fullPaintCount;
      }
      int pixelDiff = 0;
      for (int channel = 0; channel < 4; ++channel) {
        int shift = channel * 8;
        int value = std::abs(static_cast<int>((refValue >> shift) & 0xFF) -
                             static_cast<int>((candValue >> shift) & 0xFF));
        pixelDiff = std::max(pixelDiff, value);
      }
      if (pixelDiff > 0) {
        ++diffCount;
      }
      maxDiff = std::max(maxDiff, pixelDiff);
    }
  }
  const_cast<Bitmap&>(reference).unlockPixels();
  candidate.unlockPixels();
  // Fallback-equivalence fingerprint: the planned retry's swapped materialized tree is built by
  // the same flag path the legacy construction-time rewrite used, so its output must match the
  // legacy render byte for byte. The hash lets two process runs (planned vs legacy) be compared
  // from the logs.
  uint64_t hash = 1469598103934665603ULL;
  {
    auto* hashPixels = static_cast<const uint32_t*>(candidate.lockPixels());
    for (int i = 0; i < size * size; ++i) {
      hash ^= hashPixels[i];
      hash *= 1099511628211ULL;
    }
    candidate.unlockPixels();
  }
  printf("[RetryMaskCoverage] maxChannelDiff=%d differing=%lld/%d whiteOnlyInRef=%lld "
         "candidateHash=%llu\n",
         maxDiff, diffCount, size * size, fullPaintCount,
         static_cast<unsigned long long>(hash));
  fflush(stdout);
  // A dropped mask leaves large white-vs-color regions; the quantization budget is 1 LSB per
  // materialized edge.
  if (!legacy) {
    EXPECT_LE(maxDiff, 4);
  }
}

// After the P4 migration, blend-child materialization is planned, not baked in: the original
// tree survives construction, and the in-plan retry in OpsCompositor materializes operands only
// when the chain route wants them. Three observable consequences, one per segment:
//  - runtime-only (TGFX_AOT_DISABLE): no materialization anywhere;
//  - default + no bundle: the untouched original tree runs the runtime route — the F02
//    main-equivalence guarantee now holds by default, no env needed;
//  - default + bundle: the chain route refuses the two-gradient tree, the retry materializes
//    both operands, and the chain kernel serves the draw (the non-vacuous positive control).
TGFX_TEST(AOTRenderConsistencyTest, BlendChildMaterializationIsPlannedNotBakedIn) {
  if (std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr) {
    GTEST_SKIP() << "The legacy switch restores construction-time materialization; this test "
                   "asserts the planned path";
  }
  bool runtimeOnly = std::getenv("TGFX_AOT_DISABLE") != nullptr;
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto renderScene = [&]() -> AOTDrawStats {
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    auto surface = Surface::Make(context, 96, 96);
    auto gradientA = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(96, 96),
                                                 {Color(1, 0, 0, 1), Color(0, 0, 1, 1)});
    auto gradientB = Shader::MakeLinearGradient(Point::Make(96, 0), Point::Make(0, 96),
                                                 {Color(0, 1, 0, 1), Color(1, 1, 0, 1)});
    auto blend = Shader::MakeBlend(BlendMode::Multiply, gradientA, gradientB);
    EXPECT_TRUE(surface != nullptr && gradientA != nullptr && gradientB != nullptr &&
                blend != nullptr);
    if (surface == nullptr || blend == nullptr) {
      cache->setDiagnosticRecordingEnabled(false);
      return {};
    }
    Paint paint = {};
    paint.setShader(blend);
    surface->getCanvas()->drawRect(Rect::MakeWH(96, 96), paint);
    context->flushAndSubmit(true);
    auto stats = cache->drawStats();
    cache->setDiagnosticRecordingEnabled(false);
    return stats;
  };
  if (runtimeOnly) {
    auto stats = renderScene();
    EXPECT_EQ(stats.fpFlattenEdges, 0u);
    EXPECT_EQ(stats.offscreenTargets, 0u);
    EXPECT_EQ(stats.completeAOTDraws, 0u);
    return;
  }
  {
    // No bundle: the original tree runs the runtime route untouched.
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    auto stats = renderScene();
    EXPECT_EQ(stats.fpFlattenEdges, 0u);
    EXPECT_EQ(stats.completeAOTDraws, 0u);
  }
  {
    // Bundle: the retry materializes both operands and the chain serves the draw.
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    auto stats = renderScene();
    EXPECT_GE(stats.fpFlattenEdges, 1u);
    EXPECT_GE(stats.completeAOTDraws, 1u);
  }
}

// Attribution experiment for the three-way diff finding (test/diff harness). After the P4
// migration the three configurations mean:
//  - reference: bundle unloaded — the original tree runs the runtime route with no
//    materialization (the F02 main-equivalence guarantee now holds by default, no env needed)
//  - bundle + decomposition disabled: the gate never opens, so the same untouched tree runs the
//    runtime route — identical to the reference by construction
//  - bundle + decomposition enabled: the plain chain route refuses the original two-gradient
//    tree, the in-plan retry materializes both operands, and the chain kernel serves the draw
// The materialization path is expected to cost at most 1 LSB (the P1 three-way diff measured
// exactly that across every blend mode), so the full-AOT render may differ from the original
// tree by <=1 while the AOT execution itself must stay byte-exact against its own input.
TGFX_TEST(AOTRenderConsistencyTest, GradientBlendDiffAttribution) {
  if (std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr) {
    GTEST_SKIP() << "The legacy switch restores construction-time materialization; this test "
                   "asserts the planned path";
  }
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    std::vector<Color> colorsA = {Color(1, 0, 0, 1), Color(0, 0, 1, 1)};
    std::vector<Color> colorsB = {Color(0, 1, 0, 1), Color(1, 1, 0, 1)};
    auto gradientA =
        Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size), colorsA);
    auto gradientB =
        Shader::MakeLinearGradient(Point::Make(size, 0), Point::Make(0, size), colorsB);
    ASSERT_TRUE(gradientA != nullptr);
    ASSERT_TRUE(gradientB != nullptr);
    auto blend = Shader::MakeBlend(BlendMode::Multiply, gradientA, gradientB);
    ASSERT_TRUE(blend != nullptr);
    Paint paint = {};
    // Paint::setAlpha() takes a normalized 0-1 float; 51 was an invalid raw 8-bit value.
    paint.setAlpha(51.0f / 255.0f);
    paint.setShader(blend);
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  auto loadEmbeddedBundle = [&]() -> bool {
    auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
    if (bundleData == nullptr || bundleSize == 0) {
      return false;
    }
    return cache->loadBundle(bundleData, bundleSize);
  };
  auto printStats = [&](const char* label, const AOTDrawStats& draws) {
    printf("[GradientBlendDiffAttribution] %s: draws=%u completeAOT=%u atomicFallbacks=%u "
           "kernelInvocations=%u fpFlattenEdges=%u planMaterializedEdges=%u\n",
           label, static_cast<unsigned>(draws.draws),
           static_cast<unsigned>(draws.completeAOTDraws),
           static_cast<unsigned>(draws.atomicFallbacks),
           static_cast<unsigned>(draws.kernelInvocations),
           static_cast<unsigned>(draws.fpFlattenEdges),
           static_cast<unsigned>(draws.planMaterializedEdges));
    fflush(stdout);
  };
  AOTDrawStats referenceStats = {};
  Bitmap reference;
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    cache->resetStats();
    renderScene(&reference);
    referenceStats = cache->drawStats();
    printStats("reference", referenceStats);
  }
  AOTDrawStats materializedJITStats = {};
  Bitmap materializedJIT;
  {
    ASSERT_TRUE(loadEmbeddedBundle());
    cache->setDecompositionEnabled(false);
    context->globalCache()->clearPrograms();
    ScopedAOTDeliberateMiss deliberate(context);
    cache->resetStats();
    renderScene(&materializedJIT);
    materializedJITStats = cache->drawStats();
    printStats("materializedJIT", materializedJITStats);
  }
  AOTDrawStats fullAOTStats = {};
  Bitmap fullAOT;
  {
    cache->setDecompositionEnabled(true);
    context->globalCache()->clearPrograms();
    cache->resetStats();
    renderScene(&fullAOT);
    fullAOTStats = cache->drawStats();
    printStats("fullAOT", fullAOTStats);
  }
  auto pairStats = [&](const char* label, const Bitmap& a, const Bitmap& b)
      -> std::tuple<int, size_t, size_t> {
    auto* pa = static_cast<const uint8_t*>(const_cast<Bitmap&>(a).lockPixels());
    auto* pb = static_cast<const uint8_t*>(const_cast<Bitmap&>(b).lockPixels());
    EXPECT_TRUE(pa != nullptr && pb != nullptr);
    if (pa == nullptr || pb == nullptr) {
      return {0, 0, 0};
    }
    int maxDiff = 0;
    size_t diffCount = 0;
    size_t gt2Count = 0;
    auto total = static_cast<size_t>(size) * size * 4;
    for (size_t offset = 0; offset < total; offset += 4) {
      int pixelDiff = 0;
      for (int channel = 0; channel < 4; ++channel) {
        int value = std::abs(pa[offset + static_cast<size_t>(channel)] -
                             pb[offset + static_cast<size_t>(channel)]);
        maxDiff = std::max(maxDiff, value);
        pixelDiff = std::max(pixelDiff, value);
      }
      if (pixelDiff > 0) {
        diffCount++;
      }
      if (pixelDiff > 2) {
        gt2Count++;
      }
    }
    printf("[GradientBlendDiffAttribution] %s: maxChannelDiff=%d differing=%zu/%d >2=%zu\n",
           label, maxDiff, diffCount, size * size, gt2Count);
    fflush(stdout);
    const_cast<Bitmap&>(a).unlockPixels();
    const_cast<Bitmap&>(b).unlockPixels();
    return std::make_tuple(maxDiff, diffCount, gt2Count);
  };
  auto materializedStats = pairStats("materializedJIT_vs_reference", materializedJIT, reference);
  auto aotStats = pairStats("fullAOT_vs_reference", fullAOT, reference);
  pairStats("fullAOT_vs_materializedJIT", fullAOT, materializedJIT);
  bool runtimeOnly = std::getenv("TGFX_AOT_DISABLE") != nullptr;
  if (runtimeOnly) {
    // With the runtime route forced, no materialization happens anywhere and all three renders
    // must be byte-identical.
    EXPECT_EQ(std::get<0>(materializedStats), 0);
    EXPECT_EQ(std::get<0>(aotStats), 0);
    EXPECT_EQ(referenceStats.fpFlattenEdges, 0u);
    EXPECT_EQ(materializedJITStats.fpFlattenEdges, 0u);
    EXPECT_EQ(fullAOTStats.completeAOTDraws, 0u);
    return;
  }
  // The untouched tree runs identically on both runtime configurations.
  EXPECT_EQ(std::get<0>(materializedStats), 0);
  // The full-AOT render materializes both operands, which costs at most 1 LSB (the documented
  // materialization quantization); the chain kernel itself stays byte-exact against that input.
  EXPECT_LE(std::get<0>(aotStats), 1);
  // Non-vacuous guarantees: the retry materialization and the AOT service must actually happen,
  // or the bounds above would prove nothing.
  EXPECT_GE(referenceStats.fpFlattenEdges, 0u);
  EXPECT_EQ(referenceStats.fpFlattenEdges, 0u);
  EXPECT_GE(fullAOTStats.fpFlattenEdges, 1u);
  EXPECT_GE(fullAOTStats.completeAOTDraws, 1u);
}

// Guards the single-program first-frame behavior for lazily uploaded images. Historically the
// first draw of a new image took the plain direct-match route (the chain decomposition refused
// the un-instantiated view) and the second draw created a second program through the chain
// route, costing two ~15-20ms program creations per shape. TextureEffect::lowerToAOT now
// accepts proxies with a pending upload, so the first draw already takes the chain route and
// exactly one program is created. The printed per-pass timings make regressions visible in the
// log; the assertions pin the program counts.
TGFX_TEST(AOTRenderConsistencyTest, FirstSceneSteadyStateAttribution) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (!cache->isLoaded()) {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    if (bundleData != nullptr && bundleBytes > 0) {
      cache->loadBundle(bundleData, bundleBytes);
    }
  }
  constexpr int size = 96;
  Bitmap source = {};
  ASSERT_TRUE(source.allocPixels(size, size));
  auto* pixels = static_cast<uint32_t*>(source.lockPixels());
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      pixels[y * size + x] = static_cast<uint32_t>(
          (255u << 24) | (static_cast<uint32_t>(x * 2.6f) << 16) |
          (static_cast<uint32_t>(y * 2.6f) << 8) | static_cast<uint32_t>((x + y) * 1.3f));
    }
  }
  source.unlockPixels();
  auto image = Image::MakeFrom(source);
  ASSERT_TRUE(image != nullptr);
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                       1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto drawOnce = [&](tgfx::Canvas* canvas) {
    Paint paint = {};
    paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
    auto start = std::chrono::steady_clock::now();
    canvas->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
  };
  auto report = [&](const char* mode, int pass, long long micros) {
    auto programs = context->globalCache()->programStats();
    auto draws = cache->drawStats();
    printf("[FirstSceneSteadyStateAttribution] %s pass=%d micros=%lld "
           "cumulative: precompiledCreations=%u cacheHits=%u programBuilders=%u "
           "completeAOT=%u\n",
           mode, pass, micros, static_cast<unsigned>(programs.precompiledArtifactCreations),
           static_cast<unsigned>(programs.cacheHits),
           static_cast<unsigned>(programs.programBuilderCreations),
           static_cast<unsigned>(draws.completeAOTDraws));
    fflush(stdout);
  };
  // Phase 1: three draws onto one shared surface.
  {
    auto surface = Surface::Make(context, size, size);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    for (int pass = 0; pass < 3; ++pass) {
      canvas->clear(Color::White());
      report("shared-surface", pass, drawOnce(canvas));
    }
  }
  // The first draw of a lazily uploaded image must already take the chain route (the pending
  // upload is accepted), so exactly one precompiled program exists after three draws and the
  // second draw already hits the cache. A regression back to the plain direct-match first
  // frame would create a second program here (the double-creation bug).
  {
    auto programs = context->globalCache()->programStats();
    EXPECT_EQ(programs.precompiledArtifactCreations, 1u);
    EXPECT_GE(programs.cacheHits, 1u);
  }
  // Phase 2: one draw onto each of three fresh surfaces (the diff-driver pattern).
  for (int pass = 0; pass < 3; ++pass) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    report("separate-surfaces", pass, drawOnce(canvas));
  }
  EXPECT_GE(cache->drawStats().completeAOTDraws, 6u);
  EXPECT_EQ(context->globalCache()->programStats().precompiledArtifactCreations, 1u);
}

// A texture whose upload fails must render identically on the runtime route and the AOT route.
// This is the safety precondition for letting TextureEffect::lowerToAOT accept proxies with a
// pending upload: if the chain route served a failed upload differently than the runtime route,
// first-frame chain decomposition would leak pixels (or crash) where the runtime route stays
// blank. The generator fails every decode, so the texture view never materializes. The
// clear-only reference makes the runtime assertion non-vacuous: a failed upload must contribute
// nothing on top of the white background.
class FailingUploadGenerator : public ImageGenerator {
 public:
  FailingUploadGenerator(int width, int height) : ImageGenerator(width, height) {}

  bool isAlphaOnly() const override {
    return false;
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool) const override {
    return nullptr;
  }
};

TGFX_TEST(AOTRenderConsistencyTest, FailedTextureUploadKeepsRoutesAligned) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                       1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto renderScene = [&](Bitmap* outBitmap) {
    auto image = Image::MakeFrom(std::make_shared<FailingUploadGenerator>(size, size));
    ASSERT_TRUE(image != nullptr);
    auto surface = Surface::Make(context, size, size);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    Paint paint = {};
    paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
    canvas->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  auto renderClearOnly = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->clear(Color::White());
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  // Runtime-route reference (no bundle): the failed upload must contribute nothing.
  Bitmap runtimeResult;
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&runtimeResult);
  }
  Bitmap clearOnly;
  renderClearOnly(&clearOnly);
  ExpectBitmapsIdentical("failed-upload-runtime-zero", runtimeResult, clearOnly, size, size);
  // AOT-enabled route: must match the runtime reference byte for byte. Today the chain route
  // refuses the un-instantiated view, so this exercises the plain direct-match path; once
  // lowerToAOT accepts pending-upload proxies, the first frame runs the chain kernel and this
  // assertion guards that a failed upload still contributes nothing there.
  Bitmap aotResult;
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&aotResult);
  }
  ExpectBitmapsIdentical("failed-upload-aot-aligned", aotResult, runtimeResult, size, size);
}

// A pending upload whose proxy has no external consumer must be skipped without decoding. The
// skip lives in ResourceTask::execute() (use_count() == 1 means only the task holds the proxy),
// so any subclass that stores a second strong reference to the proxy silently defeats it and
// wastes a full decode for images nobody draws. The generator counts onMakeBuffer() calls: the
// kept-alive proxy must decode, the released one must not.
class CountingImageGenerator : public ImageGenerator {
 public:
  CountingImageGenerator(int width, int height) : ImageGenerator(width, height) {}

  bool isAlphaOnly() const override {
    return false;
  }

  mutable std::atomic<int> makeCount{0};

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool) const override {
    makeCount++;
    return nullptr;
  }
};

TGFX_TEST(AOTRenderConsistencyTest, UnreferencedPendingUploadIsSkipped) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* proxyProvider = context->proxyProvider();
  auto generatorA = std::make_shared<CountingImageGenerator>(32, 32);
  {
    auto proxy = proxyProvider->createTextureProxy(generatorA, false, 0);
    ASSERT_TRUE(proxy != nullptr);
    context->flushAndSubmit(true);
    // The proxy is still alive here, so the upload task is not its only owner and the decode
    // must run (even though it fails; only the call count matters for this test).
    EXPECT_GE(generatorA->makeCount.load(), 1);
  }
  auto generatorB = std::make_shared<CountingImageGenerator>(32, 32);
  {
    auto proxy = proxyProvider->createTextureProxy(generatorB, false, 0);
    ASSERT_TRUE(proxy != nullptr);
    proxy = nullptr;
    context->flushAndSubmit(true);
    // With the only external reference released, the task must skip the decode entirely.
    EXPECT_EQ(generatorB->makeCount.load(), 0);
  }
}

// YUV sources upload into multi-plane texture views, which the chain route cannot serve. Their
// YUV-ness is known at proxy creation time (the ImageBuffer is alive then), so the proxy must
// report it and TextureEffect::lowerToAOT must refuse the pending upload instead of assuming a
// single-plane leaf. Generator-backed proxies report non-YUV: every built-in codec decodes into
// single-plane buffers, and the chain matcher's multi-sampler rejection is the backstop for a
// custom generator violating that contract.
TGFX_TEST(AOTRenderConsistencyTest, YUVSourceReportsItsPlanesToChainPlanning) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* proxyProvider = context->proxyProvider();
  static uint8_t planeY[16 * 16];
  static uint8_t planeU[8 * 8];
  static uint8_t planeV[8 * 8];
  for (size_t index = 0; index < sizeof(planeY); ++index) {
    planeY[index] = static_cast<uint8_t>(index);
  }
  for (size_t index = 0; index < sizeof(planeU); ++index) {
    planeU[index] = static_cast<uint8_t>(64 + index);
    planeV[index] = static_cast<uint8_t>(192 - index);
  }
  const void* planeData[3] = {planeY, planeU, planeV};
  size_t planeRowBytes[3] = {16, 8, 8};
  auto yuvData = YUVData::MakeFrom(16, 16, planeData, planeRowBytes, 3);
  ASSERT_TRUE(yuvData != nullptr);
  auto yuvBuffer = ImageBuffer::MakeI420(yuvData);
  ASSERT_TRUE(yuvBuffer != nullptr);
  auto yuvProxy = proxyProvider->createTextureProxy(yuvBuffer, false);
  ASSERT_TRUE(yuvProxy != nullptr);
  EXPECT_TRUE(yuvProxy->hasPendingUpload());
  EXPECT_TRUE(yuvProxy->mayUploadYUV());
  auto generator = std::make_shared<CountingImageGenerator>(16, 16);
  auto generatorProxy = proxyProvider->createTextureProxy(generator, false, 0);
  ASSERT_TRUE(generatorProxy != nullptr);
  EXPECT_TRUE(generatorProxy->hasPendingUpload());
  EXPECT_FALSE(generatorProxy->mayUploadYUV());
}

// The decomposition route's refusals must be observable, not silent: a draw whose color chain
// was attempted and refused records its pure-analysis reason (AOTDecomposeOutcome) in the draw
// stats. Part one is the mechanism (counter accumulation). Part two pins the no-false-positive
// side: a chain the route serves end-to-end records nothing. The positive side is exercised by
// real suite scenes through the same recording point (e.g. LUTGradientMaskFold records
// UnsupportedShape, AACoverageXferDstFold records a fusable-but-unserved coverage draw), and
// the counter is the observation point for the P4 materialization migration, where
// un-materialized complex chains will finally reach the route and light it up.
TGFX_TEST(AOTRenderConsistencyTest, DecomposeRejectionIsRecordedWithReason) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  cache->recordDecomposeRejection(AOTDecomposeOutcome::UnsupportedShape);
  cache->recordDecomposeRejection(AOTDecomposeOutcome::UnsupportedShape);
  cache->recordDecomposeRejection(AOTDecomposeOutcome::BlockedByLowering);
  auto stats = cache->drawStats();
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::UnsupportedShape)], 2u);
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::BlockedByLowering)], 1u);
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::FusablePointwise)], 0u);

  // No-false-positive: an image + color-matrix chain the route serves end-to-end must not
  // record any rejection.
  cache->resetStats();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  auto imageShader = Shader::MakeImageShader(image);
  ASSERT_TRUE(imageShader != nullptr);
  auto surface = Surface::Make(context, 96, 96);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setShader(imageShader);
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                       1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
  surface->getCanvas()->drawRect(Rect::MakeWH(96, 96), paint);
  context->flushAndSubmit(true);
  stats = cache->drawStats();
  uint64_t totalRejections = 0;
  for (auto count : stats.decomposeRejections) {
    totalRejections += count;
  }
  printf("[DecomposeRejection] served-chain rejections=%llu completeAOT=%u\n",
         static_cast<unsigned long long>(totalRejections),
         static_cast<unsigned>(stats.completeAOTDraws));
  fflush(stdout);
  EXPECT_EQ(totalRejections, 0u);
  EXPECT_GE(stats.completeAOTDraws, 1u);
  cache->setDiagnosticRecordingEnabled(false);
}

// Proves AlphaThreshold reaches a fused pointwise slot. The operator was previously rejected by
// AOTPointwiseTailProcessor::Make, so any chain containing it fell back to the runtime path; each
// slot now carries the full operator parameter set.
TGFX_TEST(AOTRenderConsistencyTest, AlphaThresholdChainFusesByteExact) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto chain =
      ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::AlphaThreshold(0.25f));
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true, &reference,
                                 &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                 &candidateStats);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  // Matrix and AlphaThreshold occupy the two slots of one kernel, so this stays a single pass with
  // no intermediate texture.
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  ExpectBitmapsIdentical("pointwise-matrix-alphathreshold", candidate, reference, width, height);
}

// SVG feTurbulence's main real-world shape: Perlin noise piped through luminanceToAlpha then
// AlphaThreshold (CanvasTest.NoiseWithThreshold). The colorFilter must be set on Paint separately
// from the shader (not via Shader::makeWithColorFilter) to hit OpsCompositor's decomposition gate,
// which only activates when brush.shader and brush.colorFilter are both present as separate brush
// fields (OpsCompositor::needComputeBounds). Matrix and AlphaThreshold occupy the PerlinNoiseFill
// pass's OpType slot and a following PointwiseTail pass respectively.
TGFX_TEST(AOTRenderConsistencyTest, PerlinNoiseLuminanceAlphaThreshold) {
  constexpr std::array<float, 20> luminanceToAlpha = {
      0.0f,    0.0f,    0.0f,    0.0f, 0.0f,  //
      0.0f,    0.0f,    0.0f,    0.0f, 0.0f,  //
      0.0f,    0.0f,    0.0f,    0.0f, 0.0f,  //
      0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
  };
  auto composed = ColorFilter::Compose(ColorFilter::Matrix(luminanceToAlpha),
                                       ColorFilter::AlphaThreshold(0.5f));
  ASSERT_TRUE(composed != nullptr);
  int width = 130;
  int height = 130;
  Bitmap reference = {};
  Bitmap candidate = {};
  for (int pass = 0; pass < 2; ++pass) {
    bool useBundle = pass == 1;
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeTurbulence(0.25f, 0.25f, 3, 6903));
    paint.setColorFilter(composed);
    surface->getCanvas()->drawRect(
        Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
    context->flushAndSubmit(true);
    auto* outBitmap = useBundle ? &candidate : &reference;
    ASSERT_TRUE(outBitmap->allocPixels(width, height));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  }
  ExpectBitmapsIdentical("perlin-luminance-alphathreshold", candidate, reference, width, height);
}

// An anti-aliased, non-pixel-aligned rect clip produces a device-space RectEffect coverage FP. It must fold
// into the pointwise chain as an OP_AARECT_COVERAGE node, so a clipped texture draw hits
// PointwiseChainShader in one pass and stays byte-identical to the runtime path.
TGFX_TEST(AOTRenderConsistencyTest, AnalyticRectClipFoldsIntoChain) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = 130;
  int height = 130;
  Bitmap reference = {};
  Bitmap candidate = {};
  for (int pass = 0; pass < 2; ++pass) {
    bool useBundle = pass == 1;
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clipRect(Rect::MakeLTRB(10.25f, 10.5f, 119.75f, 119.5f), true);
    canvas->drawImage(image, 10, 10);
    context->flushAndSubmit(true);
    auto* outBitmap = useBundle ? &candidate : &reference;
    ASSERT_TRUE(outBitmap->allocPixels(width, height));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  }
  ExpectBitmapsIdentical("aarect-clip-fold-chain", candidate, reference, width, height);
}

// The L1 direct-hang route: the ellipse GP is incompatible with the chain matcher, so an AA
// clipRect paired with a solid oval must match EllipseFillShader with a bare RectEffect coverage
// FP. That route evaluates the clip through the shared Rect/HasClip uniforms, and the rect value
// itself has to come from GLSLRectEffect::onSetData — the fold path above is exercised separately
// and never uploads the shader-level Rect.
TGFX_TEST(AOTRenderConsistencyTest, AnalyticRectClipDirectEllipseFill) {
  auto renderOnce = [&](bool useBundle, Bitmap* outBitmap) {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, 120, 120);
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    canvas->clipRect(Rect::MakeLTRB(10.25f, 10.5f, 109.75f, 109.5f), true);
    Paint paint = {};
    paint.setColor(Color::Red());
    canvas->drawOval(Rect::MakeXYWH(2, 2, 116, 116), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(120, 120));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  };
  Bitmap candidate = {};
  Bitmap reference = {};
  renderOnce(false, &reference);
  renderOnce(true, &candidate);
  ExpectBitmapsIdentical("aarect-clip-direct-ellipse-fill", candidate, reference, 120, 120);
}

// Coverage modes of the runtime clip contract exercised through the L1 direct-hang route: an
// rrect clip (HasClip == 2) in device space, in device space without AA, and under a rotation
// (non-identity DeviceToLocal); plus a local-space AA rect clip (HasClip == 3) under a rotation.
// The filled oval always extends beyond the clip boundary, so a clip that silently evaluates to
// full coverage changes the output instead of passing vacuously.
enum class AnalyticClipScene {
  DeviceRRectAA,
  DeviceRRectNonAA,
  RotatedRRect,
  RotatedRect,
};

static void RenderAnalyticClipSceneOnce(AnalyticClipScene scene, bool useBundle, Bitmap* outBitmap,
                                        ColorFilterRenderStats* outStats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  cache->setDiagnosticRecordingEnabled(false);
  context->globalCache()->clearPrograms();
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto surface = Surface::Make(context, 160, 160);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::White());
  const auto clipBounds = Rect::MakeLTRB(24, 24, 136, 136);
  const auto complexRadii =
      std::array<Point, 4>({Point{18, 26}, Point{8, 12}, Point{30, 20}, Point{12, 32}});
  bool antiAlias = true;
  switch (scene) {
    case AnalyticClipScene::DeviceRRectAA:
    case AnalyticClipScene::DeviceRRectNonAA:
      antiAlias = scene == AnalyticClipScene::DeviceRRectAA;
      canvas->clipRRect(RRect::MakeRectRadii(clipBounds, complexRadii), antiAlias);
      break;
    case AnalyticClipScene::RotatedRRect:
      canvas->concat(Matrix::MakeTrans(80, 80) * Matrix::MakeRotate(30) *
                     Matrix::MakeTrans(-80, -80));
      canvas->clipRRect(RRect::MakeRectXY(clipBounds, 20, 28), true);
      canvas->concat(Matrix::MakeTrans(80, 80) * Matrix::MakeRotate(-30) *
                     Matrix::MakeTrans(-80, -80));
      break;
    case AnalyticClipScene::RotatedRect:
      canvas->concat(Matrix::MakeTrans(80, 80) * Matrix::MakeRotate(20) *
                     Matrix::MakeTrans(-80, -80));
      canvas->clipRect(clipBounds, true);
      canvas->concat(Matrix::MakeTrans(80, 80) * Matrix::MakeRotate(-20) *
                     Matrix::MakeTrans(-80, -80));
      break;
  }
  Paint paint = {};
  paint.setColor(Color::Red());
  canvas->drawOval(Rect::MakeXYWH(4, 4, 152, 152), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(160, 160));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (outStats != nullptr) {
    outStats->hits = cache->hitCount();
    outStats->pipelines = cache->aotStageCount(PrecompiledAOTStage::PipelineCreated);
    outStats->noMatchingRule = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
    outStats->draws = cache->drawStats();
    outStats->programs = context->globalCache()->programStats();
  }
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
}

static void ExpectAnalyticClipSceneConsistent(const char* label, AnalyticClipScene scene) {
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats stats = {};
  RenderAnalyticClipSceneOnce(scene, false, &reference, nullptr);
  RenderAnalyticClipSceneOnce(scene, true, &candidate, &stats);
  // The draw must actually ride the precompiled route; otherwise the byte comparison would
  // compare two runtime renders and prove nothing.
  EXPECT_GE(stats.hits, 1u);
  EXPECT_GE(stats.pipelines, 1u);
  EXPECT_EQ(stats.noMatchingRule, 0u);
  EXPECT_EQ(stats.programs.programBuilderCreations, 0u);
  ExpectBitmapsIdentical(label, candidate, reference, 160, 160);
}

TGFX_TEST(AOTRenderConsistencyTest, AnalyticClipCoverageModes) {
  ExpectAnalyticClipSceneConsistent("clip-device-rrect-aa", AnalyticClipScene::DeviceRRectAA);
  ExpectAnalyticClipSceneConsistent("clip-device-rrect-nonaa", AnalyticClipScene::DeviceRRectNonAA);
  ExpectAnalyticClipSceneConsistent("clip-rotated-rrect", AnalyticClipScene::RotatedRRect);
  ExpectAnalyticClipSceneConsistent("clip-rotated-rect", AnalyticClipScene::RotatedRect);
}

// Renders the full BlendModeTest scene once (see the parity scene test below); shared by the
// differential assertion.
static void RenderFullBlendModeSceneOnce(Context* context, bool useBundle, Bitmap* outBitmap) {
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  // Verbatim CanvasTest.BlendModeTest: 18 modes x (image, then solid red rect) in one render, the
  // only structural difference from the per-mode probe that passes.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto padding = 30;
  auto scale = 1.f;
  auto offset = static_cast<float>(padding + image->width()) * scale;
  BlendMode blendModes[] = {BlendMode::SrcOver,    BlendMode::Darken,      BlendMode::Multiply,
                            BlendMode::PlusDarker, BlendMode::ColorBurn,   BlendMode::Lighten,
                            BlendMode::Screen,     BlendMode::PlusLighter, BlendMode::ColorDodge,
                            BlendMode::Overlay,    BlendMode::SoftLight,   BlendMode::HardLight,
                            BlendMode::Difference, BlendMode::Exclusion,   BlendMode::Hue,
                            BlendMode::Saturation, BlendMode::Color,       BlendMode::Luminosity};
  auto surfaceHeight = (static_cast<float>(padding + image->height())) * scale *
                       ceil(sizeof(blendModes) / sizeof(BlendMode) / 4.0f) * 2;
  auto surface = Surface::Make(context, static_cast<int>(offset * 4),
                               static_cast<int>(surfaceHeight), false, 4);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  Paint backPaint = {};
  backPaint.setColor(Color::FromRGBA(82, 117, 132, 255));
  backPaint.setStyle(PaintStyle::Fill);
  canvas->drawRect(Rect::MakeWH(surface->width(), surface->height()), backPaint);
  for (auto& blendMode : blendModes) {
    Paint paint = {};
    paint.setBlendMode(blendMode);
    paint.setAntiAlias(true);
    canvas->save();
    canvas->concat(Matrix::MakeScale(scale));
    canvas->drawImage(image, &paint);
    canvas->restore();
    canvas->concat(Matrix::MakeTrans(offset, 0));
    if (canvas->getMatrix().getTranslateX() + static_cast<float>(image->width()) * scale >
        static_cast<float>(surface->width())) {
      canvas->translate(-canvas->getMatrix().getTranslateX(),
                        static_cast<float>(image->height() + padding) * scale);
    }
  }
  Rect bounds = Rect::MakeWH(static_cast<float>(image->width()) * scale,
                             static_cast<float>(image->height()) * scale);
  canvas->translate(-canvas->getMatrix().getTranslateX(),
                    static_cast<float>(image->height() + padding) * scale);
  for (auto& blendMode : blendModes) {
    Paint paint = {};
    paint.setBlendMode(blendMode);
    paint.setStyle(PaintStyle::Fill);
    paint.setColor(Color::FromRGBA(255, 14, 14, 255));
    canvas->drawRect(bounds, paint);
    canvas->concat(Matrix::MakeTrans(offset, 0));
    if (canvas->getMatrix().getTranslateX() + static_cast<float>(image->width()) * scale >
        static_cast<float>(surface->width())) {
      canvas->translate(-canvas->getMatrix().getTranslateX(),
                        static_cast<float>(image->height() + padding) * scale);
    }
  }
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(surface->width(), surface->height()));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  cache->unload();
  context->globalCache()->clearPrograms();
}

// The full CanvasTest.BlendModeTest scene (18 blend modes x image + solid rect, MSAA) rendered
// through both routes. The precompiled blend kernels must match the runtime emission; the
// Overlay/HardLight operand-pick regression (branch control on the wrong operand) reproduced
// here at a 43000-byte scale before the fix. The two compilation pipelines (shaderc -> SPIRV ->
// MSL vs direct MSL) legitimately differ by one LSB on a handful of texels, so the assertion
// allows ±1 with a small byte budget instead of demanding byte equality.
TGFX_TEST(AOTRenderConsistencyTest, BlendParityTextureScene) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  RenderFullBlendModeSceneOnce(context, false, &reference);
  RenderFullBlendModeSceneOnce(context, true, &candidate);
  auto* refPixels = static_cast<const uint8_t*>(reference.lockPixels());
  auto* candPixels = static_cast<const uint8_t*>(candidate.lockPixels());
  int diffBytes = 0;
  int maxDiff = 0;
  size_t totalBytes =
      static_cast<size_t>(reference.width()) * static_cast<size_t>(reference.height()) * 4;
  for (size_t i = 0; i < totalBytes; i++) {
    int d = std::abs(static_cast<int>(refPixels[i]) - static_cast<int>(candPixels[i]));
    if (d > 0) {
      ++diffBytes;
    }
    if (d > maxDiff) {
      maxDiff = d;
    }
  }
  reference.unlockPixels();
  candidate.unlockPixels();
  printf("[BlendParityScene] diffBytes=%d maxDiff=%d\n", diffBytes, maxDiff);
  EXPECT_LE(maxDiff, 1);
  EXPECT_LE(diffBytes, 600);
}

// Chain-route clip coverage forms: a composed multi-clip coverage (a flat Compose of rrect
// elements, optionally led by a device-space path mask), a shape-masked draw under an analytic
// clip ([analytic slot, local mask subtree]), and a shader-mask draw under a local rect clip
// ([xfermode subtree, analytic slot], exercising the blend-root gate relaxation). Each scene
// renders once with the bundle and once without and must stay byte-identical, with the AOT pass
// asserting the precompiled route served the draw.
enum class ChainClipScene {
  ComposedRRects,
  PathMaskAndRRects,
  ShapeMaskUnderRRect,
  ShapeMaskUnderLocalRect,
  ShaderMaskUnderLocalRect,
};

static void RenderChainClipSceneOnce(ChainClipScene scene, bool useBundle, Bitmap* outBitmap,
                                     ColorFilterRenderStats* outStats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  cache->setDiagnosticRecordingEnabled(false);
  context->globalCache()->clearPrograms();
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  constexpr int size = 180;
  auto surface = Surface::Make(context, size, size);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::White());
  const auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  switch (scene) {
    case ChainClipScene::ComposedRRects:
      canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(16, 20, 150, 120), 22, 30), true);
      canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(40, 60, 168, 156), 18, 26), true);
      canvas->drawImage(image, 26, 26);
      break;
    case ChainClipScene::PathMaskAndRRects: {
      // A curvy clip path cannot fold into an analytic clip, so it contributes a device-space
      // mask element and the two rrect elements follow as analytic leaves of the same Compose.
      Path path = {};
      path.addOval(Rect::MakeLTRB(10, 14, 120, 110));
      canvas->clipPath(path, true);
      canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(30, 40, 160, 140), 24, 18), true);
      canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(50, 70, 170, 165), 14, 22), true);
      canvas->drawImage(image, 26, 26);
      break;
    }
    case ChainClipScene::ShapeMaskUnderRRect: {
      // A concave star cannot triangulate, so the shape draws through its rasterized alpha mask
      // (a trailing local-texture coverage) under the rrect clip.
      Path star = {};
      for (int i = 0; i < 10; ++i) {
        float angle = static_cast<float>(i) * 36.0f - 90.0f;
        float radius = (i % 2 == 0) ? 86.0f : 36.0f;
        float x = 90.0f + radius * cosf(angle * kStarPi / 180.0f);
        float y = 90.0f + radius * sinf(angle * kStarPi / 180.0f);
        if (i == 0) {
          star.moveTo(x, y);
        } else {
          star.lineTo(x, y);
        }
      }
      star.close();
      canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(18, 24, 162, 156), 26, 20), true);
      Paint shapePaint = {};
      shapePaint.setColor(Color::Red());
      canvas->drawShape(Shape::MakeFrom(std::move(star)), shapePaint);
      break;
    }
    case ChainClipScene::ShapeMaskUnderLocalRect: {
      Path star = {};
      for (int i = 0; i < 10; ++i) {
        float angle = static_cast<float>(i) * 36.0f - 90.0f;
        float radius = (i % 2 == 0) ? 86.0f : 36.0f;
        float x = 90.0f + radius * cosf(angle * kStarPi / 180.0f);
        float y = 90.0f + radius * sinf(angle * kStarPi / 180.0f);
        if (i == 0) {
          star.moveTo(x, y);
        } else {
          star.lineTo(x, y);
        }
      }
      star.close();
      canvas->concat(Matrix::MakeTrans(90, 90) * Matrix::MakeRotate(24) *
                     Matrix::MakeTrans(-90, -90));
      canvas->clipRect(Rect::MakeLTRB(22, 26, 158, 154), true);
      canvas->concat(Matrix::MakeTrans(90, 90) * Matrix::MakeRotate(-24) *
                     Matrix::MakeTrans(-90, -90));
      Paint shapePaint = {};
      shapePaint.setColor(Color::Blue());
      canvas->drawShape(Shape::MakeFrom(std::move(star)), shapePaint);
      break;
    }
    case ChainClipScene::ShaderMaskUnderLocalRect: {
      // The shader mask filter contributes an xfermode coverage (a blend-rooted subtree) and the
      // rotated rect clip folds as a local-rect slot on top of it.
      auto maskShader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
      ASSERT_TRUE(maskShader != nullptr);
      Paint maskPaint = {};
      maskPaint.setMaskFilter(MaskFilter::MakeShader(maskShader));
      canvas->concat(Matrix::MakeTrans(90, 90) * Matrix::MakeRotate(18) *
                     Matrix::MakeTrans(-90, -90));
      canvas->clipRect(Rect::MakeLTRB(20, 24, 160, 156), true);
      canvas->concat(Matrix::MakeTrans(90, 90) * Matrix::MakeRotate(-18) *
                     Matrix::MakeTrans(-90, -90));
      canvas->drawImage(image, 26, 26, &maskPaint);
      break;
    }
  }
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(size, size));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (outStats != nullptr) {
    outStats->hits = cache->hitCount();
    outStats->pipelines = cache->aotStageCount(PrecompiledAOTStage::PipelineCreated);
    outStats->noMatchingRule = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
    outStats->draws = cache->drawStats();
    outStats->programs = context->globalCache()->programStats();
  }
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
}

static void ExpectChainClipSceneConsistent(const char* label, ChainClipScene scene) {
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats stats = {};
  RenderChainClipSceneOnce(scene, false, &reference, nullptr);
  RenderChainClipSceneOnce(scene, true, &candidate, &stats);
  EXPECT_GE(stats.hits, 1u);
  EXPECT_GE(stats.pipelines, 1u);
  EXPECT_EQ(stats.noMatchingRule, 0u);
  EXPECT_EQ(stats.programs.programBuilderCreations, 0u);
  ExpectBitmapsIdentical(label, candidate, reference, 180, 180);
}

TGFX_TEST(AOTRenderConsistencyTest, ChainClipCoverageModes) {
  ExpectChainClipSceneConsistent("chain-composed-rrects", ChainClipScene::ComposedRRects);
  ExpectChainClipSceneConsistent("chain-pathmask-rrects", ChainClipScene::PathMaskAndRRects);
  ExpectChainClipSceneConsistent("chain-shapemask-rrect", ChainClipScene::ShapeMaskUnderRRect);
  ExpectChainClipSceneConsistent("chain-shapemask-localrect",
                                 ChainClipScene::ShapeMaskUnderLocalRect);
  ExpectChainClipSceneConsistent("chain-shadermask-localrect",
                                 ChainClipScene::ShaderMaskUnderLocalRect);
}

// Builds a balanced binary blend tree over the given number of image leaves (cycling through the
// image list), e.g. five leaves reduce to Blend(Blend(A,B), Blend(Blend(C,D),E)). The balanced
// shape spreads the texture leaves across the DAG, so a split planner must find a cut whose both
// sides stay within the fused kernel's four-sampler budget.
static std::shared_ptr<Shader> MakeBalancedBlendShader(
    const std::vector<std::shared_ptr<Image>>& images, size_t begin, size_t end) {
  if (end - begin == 1) {
    return Shader::MakeImageShader(images[begin % images.size()]);
  }
  auto mid = begin + (end - begin) / 2;
  auto left = MakeBalancedBlendShader(images, begin, mid);
  auto right = MakeBalancedBlendShader(images, mid, end);
  auto mode = mid % 2 == 0 ? BlendMode::Multiply : BlendMode::Screen;
  return Shader::MakeBlend(mode, left, right);
}

// Builds a left-deep blend chain: Blend(Blend(Blend(A,B),C),D)... Every blend's pass-through
// operand is the accumulated chain, so a greedy split materializes the chain side repeatedly and
// each output pixel crosses one RGBA8 intermediate per pass — the materialization-depth axis for
// the quantization calibration.
static std::shared_ptr<Shader> MakeLeftDeepBlendShader(
    const std::vector<std::shared_ptr<Image>>& images, size_t leafCount) {
  auto current = Shader::MakeImageShader(images[0]);
  for (size_t i = 1; i < leafCount; ++i) {
    auto mode = i % 2 == 0 ? BlendMode::Multiply : BlendMode::Screen;
    auto leaf = Shader::MakeImageShader(images[i % images.size()]);
    current = Shader::MakeBlend(mode, current, leaf);
  }
  return current;
}

static void RenderBlendShaderSceneOnce(std::shared_ptr<Shader> shader, bool drawOval,
                                       bool useBundle, Bitmap* outBitmap,
                                       ColorFilterRenderStats* outStats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  cache->setDiagnosticRecordingEnabled(false);
  context->globalCache()->clearPrograms();
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  constexpr int size = 180;
  auto surface = Surface::Make(context, size, size);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::White());
  Paint paint = {};
  paint.setShader(std::move(shader));
  if (drawOval) {
    canvas->drawOval(Rect::MakeXYWH(10, 14, 160, 152), paint);
  } else {
    canvas->drawRect(Rect::MakeXYWH(10, 14, 160, 152), paint);
  }
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(size, size));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (outStats != nullptr) {
    outStats->hits = cache->hitCount();
    outStats->pipelines = cache->aotStageCount(PrecompiledAOTStage::PipelineCreated);
    outStats->noMatchingRule = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
    outStats->draws = cache->drawStats();
    outStats->programs = context->globalCache()->programStats();
  }
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
}

static std::vector<std::shared_ptr<Image>> MakeBlendFixtureImages() {
  std::vector<std::shared_ptr<Image>> images = {};
  for (auto* path : {"resources/apitest/mandrill_128.png", "resources/apitest/imageReplacement.png",
                     "resources/apitest/test_timestretch.png", "resources/apitest/rotation.jpg"}) {
    auto image = MakeImage(path);
    if (image == nullptr) {
      return {};
    }
    images.push_back(std::move(image));
  }
  return images;
}

// WP4-0 fixtures: over-sampler-budget blend DAGs (5/7/9 texture leaves against the fused kernel's
// four-sampler budget), drawn as a rect (rect GPs) and as an oval (the non-rect-GP terminal
// materialization route). Since the P4 migration the materialization is planned: the retry
// rebuilds the tree with materialized operands when the chain route refuses the original. The
// no-bundle reference pass now runs the original tree with no materialization at all (the F02
// main-equivalence guarantee), so the bundle pass's per-edge RGBA8 quantization shows up as a
// bounded difference instead of cancelling out: at most 1 LSB per materialized edge, with the
// differing-pixel share held to a small fraction of the scene.
TGFX_TEST(AOTRenderConsistencyTest, NestedBlendSamplerBudget) {
  if (std::string(TGFX_BACKEND_NAME) != "metal") {
    // Metal is the byte-exact AOT verification backend; software backends carry LSB-level
    // precision differences and skip.
    GTEST_SKIP();
  }
  auto images = MakeBlendFixtureImages();
  ASSERT_EQ(images.size(), 4u);
  struct Scene {
    const char* label;
    size_t leafCount;
    bool drawOval;
  };
  const std::array<Scene, 4> scenes = {{
      {"budget-blend-five-leaves", 5, false},
      {"budget-blend-seven-leaves", 7, false},
      {"budget-blend-nine-leaves", 9, false},
      {"budget-blend-five-leaves-oval", 5, true},
  }};
  for (const auto& scene : scenes) {
    auto shader = MakeBalancedBlendShader(images, 0, scene.leafCount);
    ASSERT_TRUE(shader != nullptr);
    Bitmap reference = {};
    Bitmap candidate = {};
    ColorFilterRenderStats stats = {};
    RenderBlendShaderSceneOnce(shader, scene.drawOval, false, &reference, nullptr);
    RenderBlendShaderSceneOnce(shader, scene.drawOval, true, &candidate, &stats);
    // The planned-materialization route serves the whole tree with precompiled programs.
    EXPECT_GE(stats.draws.completeAOTDraws, 1u);
    EXPECT_EQ(stats.programs.programBuilderCreations, 0u);
    EXPECT_EQ(stats.noMatchingRule, 0u);
    auto* refPixels = static_cast<const uint8_t*>(reference.lockPixels());
    auto* candPixels = static_cast<const uint8_t*>(candidate.lockPixels());
    ASSERT_TRUE(refPixels != nullptr && candPixels != nullptr);
    int maxDiff = 0;
    size_t diffPixels = 0;
    size_t totalPixels =
        static_cast<size_t>(reference.width()) * static_cast<size_t>(reference.height());
    for (size_t offset = 0; offset < totalPixels * 4; offset += 4) {
      int pixelDiff = 0;
      for (size_t channel = 0; channel < 4; ++channel) {
        pixelDiff = std::max(pixelDiff, std::abs(static_cast<int>(refPixels[offset + channel]) -
                                                 static_cast<int>(candPixels[offset + channel])));
      }
      maxDiff = std::max(maxDiff, pixelDiff);
      if (pixelDiff > 0) {
        diffPixels++;
      }
    }
    reference.unlockPixels();
    candidate.unlockPixels();
    printf("[BudgetBlend] %s maxDiff=%d diffPixels=%zu/%zu\n", scene.label, maxDiff, diffPixels,
           totalPixels);
    EXPECT_LE(maxDiff, 1);
    // Per-edge 1-LSB quantization compounds across the materialized edges, so up to a third of
    // the pixels legitimately differ by one; a structural error would break the maxDiff bound
    // long before this share bound.
    EXPECT_LE(diffPixels, totalPixels * 2 / 5);
  }
}

// WP4-0 fixture for the materialization-depth axis: a 16-leaf left-deep blend chain. The
// planned retry materializes every accumulated left operand (the retry rebuilds the whole blend
// tree with materializing children), so the output pixel crosses fourteen RGBA8 intermediates —
// the quantization-depth shape (a greedy DAG split would cross four; comparing the two policies
// is the WP4 performance question). The no-bundle reference now runs the original tree with zero
// materialization, so the difference is the accumulated per-edge quantization, bounded at 1 LSB
// with a differing-pixel share budget.
TGFX_TEST(AOTRenderConsistencyTest, DeepMaterializationChain) {
  if (std::string(TGFX_BACKEND_NAME) != "metal") {
    GTEST_SKIP();
  }
  auto images = MakeBlendFixtureImages();
  ASSERT_EQ(images.size(), 4u);
  auto shader = MakeLeftDeepBlendShader(images, 16);
  ASSERT_TRUE(shader != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats stats = {};
  RenderBlendShaderSceneOnce(shader, false, false, &reference, nullptr);
  RenderBlendShaderSceneOnce(shader, false, true, &candidate, &stats);
  EXPECT_GE(stats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(stats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(stats.noMatchingRule, 0u);
  EXPECT_GE(stats.draws.materializedEdges, 14u);
  auto* refPixels = static_cast<const uint8_t*>(reference.lockPixels());
  auto* candPixels = static_cast<const uint8_t*>(candidate.lockPixels());
  ASSERT_TRUE(refPixels != nullptr && candPixels != nullptr);
  int maxDiff = 0;
  size_t diffPixels = 0;
  size_t totalPixels =
      static_cast<size_t>(reference.width()) * static_cast<size_t>(reference.height());
  for (size_t offset = 0; offset < totalPixels * 4; offset += 4) {
    int pixelDiff = 0;
    for (size_t channel = 0; channel < 4; ++channel) {
      pixelDiff = std::max(pixelDiff, std::abs(static_cast<int>(refPixels[offset + channel]) -
                                               static_cast<int>(candPixels[offset + channel])));
    }
    maxDiff = std::max(maxDiff, pixelDiff);
    if (pixelDiff > 0) {
      diffPixels++;
    }
  }
  reference.unlockPixels();
  candidate.unlockPixels();
  printf("[DeepMaterialization] maxDiff=%d diffPixels=%zu/%zu\n", maxDiff, diffPixels,
         totalPixels);
  EXPECT_LE(maxDiff, 1);
  // Fourteen materialized edges each contribute an independent 1-LSB rounding, so a large share
  // of pixels may differ by exactly one; the maxDiff bound is the structural guard.
  EXPECT_LE(diffPixels, totalPixels * 2 / 5);
}

// An alpha-only texture mask (R8 on Metal) folded into the pointwise chain: the kernel must splat
// the sampled .r into the alpha channel via the leaf's selector bit, otherwise the mask reads as
// fully opaque. Byte-exact against the runtime path proves the splat matches the JIT emission.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyMaskFoldsIntoChain) {
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(64, 64, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_TRUE(maskPixels != nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (size_t y = 0; y < 64; ++y) {
    for (size_t x = 0; x < 64; ++x) {
      maskPixels[y * rowBytes + x] = static_cast<uint8_t>((x * 4 + y * 2) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_TRUE(maskImage != nullptr);
  auto maskFilter =
      MaskFilter::MakeShader(Shader::MakeImageShader(maskImage, TileMode::Clamp, TileMode::Clamp));
  ASSERT_TRUE(maskFilter != nullptr);
  auto colorImage = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(colorImage != nullptr);
  int width = 100;
  int height = 100;
  Bitmap reference = {};
  Bitmap candidate = {};
  for (int pass = 0; pass < 2; ++pass) {
    bool useBundle = pass == 1;
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    if (useBundle) {
      ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
    } else {
      cache->unload();
    }
    ScopedAOTStatsPause statsPause(context, !useBundle);
    context->globalCache()->clearPrograms();
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    Paint paint = {};
    // An image shader color source puts the draw on the decomposition route, so the alpha-only
    // mask folds into the chain as a second texture leaf with the splat flag set.
    paint.setShader(Shader::MakeImageShader(colorImage));
    paint.setMaskFilter(maskFilter);
    surface->getCanvas()->drawRect(Rect::MakeWH(100, 100), paint);
    context->flushAndSubmit(true);
    auto* outBitmap = useBundle ? &candidate : &reference;
    ASSERT_TRUE(outBitmap->allocPixels(width, height));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (useBundle) {
      cache->unload();
    }
  }
  ExpectBitmapsIdentical("alpha-only-mask-fold-chain", candidate, reference, width, height);
}

// P4 migration group two: a shader paired with an affectsTransparentBlack color filter takes the
// ColorFilterShader path, whose SrcIn tree (composed chain masked by the original shader alpha)
// used to be materialized at construction time. The planned path keeps the original tree; when
// the whole chain lowers (texture + color matrix + SrcIn blend are all pointwise), the chain
// route serves it fused with no materialization at all — strictly better than the old two-texture
// rewrite. The bundle segment asserts that non-vacuously; the no-bundle segment asserts the
// untouched-tree runtime route.
TGFX_TEST(AOTRenderConsistencyTest, ColorFilterShaderServesFusedTreeWithoutMaterialization) {
  if (std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr) {
    GTEST_SKIP() << "The legacy switch restores construction-time materialization; this test "
                   "asserts the planned path";
  }
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  // Shifts red by 0.25, so the matrix affects transparent black (the ColorFilterShader branch).
  const std::array<float, 20> offsetRed = {1, 0, 0, 0, 0.25f, 0, 1, 0, 0, 0,
                                           0, 0, 1, 0, 0,       0, 0, 0, 1, 0};
  auto renderScene = [&]() -> AOTDrawStats {
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    auto image = MakeImage("resources/apitest/mandrill_128.png");
    EXPECT_TRUE(image != nullptr);
    auto surface = Surface::Make(context, 96, 96);
    if (surface == nullptr || image == nullptr) {
      cache->setDiagnosticRecordingEnabled(false);
      return {};
    }
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image));
    paint.setColorFilter(ColorFilter::Matrix(offsetRed));
    surface->getCanvas()->drawRect(Rect::MakeWH(96, 96), paint);
    context->flushAndSubmit(true);
    auto stats = cache->drawStats();
    cache->setDiagnosticRecordingEnabled(false);
    return stats;
  };
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    auto stats = renderScene();
    EXPECT_EQ(stats.fpFlattenEdges, 0u);
    EXPECT_EQ(stats.completeAOTDraws, 0u);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    context->globalCache()->clearPrograms();
    auto stats = renderScene();
    printf("[ColorFilterShaderPlanned] completeAOT=%u fpFlattenEdges=%u\n",
           static_cast<unsigned>(stats.completeAOTDraws),
           static_cast<unsigned>(stats.fpFlattenEdges));
    fflush(stdout);
    // The SrcIn tree is fully pointwise, so the fused chain serves it with no materialization.
    EXPECT_GE(stats.completeAOTDraws, 1u);
    EXPECT_EQ(stats.fpFlattenEdges, 0u);
  }
}

// Encoded images use GL_TEXTURE_RECTANGLE on macOS. The pointwise chain kernel carries a
// TEXTURE_KIND dimension, so a rectangle image with a color-filter chain is served by a single
// fused precompiled pass and stays pixel-identical to the runtime fallback.
TGFX_TEST(AOTRenderConsistencyTest, MultiPassUnsupportedSourceFallsBackAtomically) {
  if (std::string(TGFX_BACKEND_NAME) != "opengl") {
    GTEST_SKIP() << "This case relies on macOS OpenGL encoded images using Rectangle textures";
  }
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto chain = ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::Luma());
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, false,
                                 &reference, &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, false, &candidate,
                                 &candidateStats);
  // The rectangle source is served by the fused pointwise chain kernel: the matcher accepts it
  // (no fallback miss) and an AOT artifact is created and used. The whole draw stays a single
  // fused pass — no offscreen target, no materialized intermediate, no render-target switch — and
  // the output is pixel-identical to the runtime fallback.
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.programs.precompiledArtifactCreations, 1u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 0u);
  EXPECT_EQ(candidateStats.draws.renderTargetSwitches, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateReadBytes, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateWriteBytes, 0u);
  EXPECT_EQ(candidateStats.draws.peakTemporaryBytes, 0u);
  ExpectBitmapsIdentical("linear-chain-unsupported-source-fallback", candidate, reference, width,
                         height);
}

}  // namespace tgfx
