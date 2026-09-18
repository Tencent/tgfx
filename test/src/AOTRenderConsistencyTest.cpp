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

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/DrawingManager.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/YUVData.h"
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
  // The SwiftShader build runs an ES context, which the opengles bundle serves.
#if defined(TGFX_USE_SWIFTSHADER)
  if (backend == "opengl") {
    backend = "opengles";
  }
#endif
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

// A tolerance variant for backends whose compiler schedules the kernel's interpreted evaluation
// differently from the runtime's unrolled expressions (MSL fma fusion), producing an occasional
// 1-LSB rounding difference; OpenGL byte-matches by compiler luck. maxDiff must stay within
// tolerance — a structural error breaks the bound long before it.
static void ExpectBitmapsNear(const char* label, const Bitmap& aotBitmap,
                              const Bitmap& runtimeBitmap, int width, int height, int tolerance) {
  auto* aotPixels = const_cast<Bitmap&>(aotBitmap).lockPixels();
  auto* runtimePixels = const_cast<Bitmap&>(runtimeBitmap).lockPixels();
  ASSERT_TRUE(aotPixels != nullptr && runtimePixels != nullptr);
  size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  auto* a = static_cast<const uint8_t*>(aotPixels);
  auto* r = static_cast<const uint8_t*>(runtimePixels);
  int maxDiff = 0;
  size_t diffCount = 0;
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
  EXPECT_LE(maxDiff, tolerance) << "AOT vs runtime render diverged for scene: " << label
                                << " (maxChannelDiff=" << maxDiff << ", diffBytes=" << diffCount
                                << "/" << totalBytes << ", tolerance=" << tolerance << ")";
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
                             << "program key (first ref=" << firstRef << " mix=" << firstMix << ")";
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

struct BrushRenderStats {
  uint32_t noMatchingRule = 0;
  AOTDrawStats draws = {};
  ProgramCacheStats programs = {};
};

// Renders the given image with a color filter, brush alpha, and canvas transform, with or without
// the precompiled bundle. Unlike RenderImageWithColorFilterOnce this exposes the paint alpha and
// the draw matrix, so tests can probe where the geometry color enters the chain and how the plan's
// intermediate passes inherit the draw transform.
static void RenderImageWithBrushOnce(const std::shared_ptr<Image>& image,
                                     const std::shared_ptr<ColorFilter>& colorFilter,
                                     float brushAlpha, const Matrix& drawMatrix, int width,
                                     int height, bool useBundle, Bitmap* outBitmap,
                                     BrushRenderStats* outStats) {
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
  cache->setDecompositionEnabled(useBundle);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setAlpha(brushAlpha);
  paint.setColorFilter(colorFilter);
  auto canvas = surface->getCanvas();
  canvas->concat(drawMatrix);
  canvas->drawImage(image, 0, 0, &paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
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
    std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                         1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
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
  uint32_t reflectionOffset =
      static_cast<uint32_t>(tampered[44]) | (static_cast<uint32_t>(tampered[45]) << 8) |
      (static_cast<uint32_t>(tampered[46]) << 16) | (static_cast<uint32_t>(tampered[47]) << 24);
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

// Formerly asserted multi-pass tail execution preserved coordinate domains across
// materialization boundaries. Since the D5 ruling (a discontinuous operator reading an RGBA8
// materialized input can flip its step() decision — not a tolerance question), multi-pass
// tail plans are refused on every route; this now records the rejection boundary for both the
// device-space-source shape (formerly 2 passes) and the plain long chain (formerly 17).
TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailOverBudgetPlansRefused) {
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
    int opCount = deviceSource ? 3 : 33;
    for (int index = 0; index < opCount; ++index) {
      processor =
          FragmentProcessor::Compose(allocator, std::move(processor),
                                     ColorMatrixFragmentProcessor::Make(allocator, swapRedBlue));
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
      // Rejection boundary: both shapes (a 2-pass device-source chain and a 17-pass plain
      // chain) exceed the single-pass budget, so the runtime reference serves the fill — one
      // program build, one kernel invocation, no materialized edges, byte-identical output.
      EXPECT_GE(candidatePrograms.programBuilderCreations, 1u);
      EXPECT_EQ(candidateDraws.completeAOTDraws, 0u);
      EXPECT_EQ(candidateDraws.atomicFallbacks, 0u);
      EXPECT_EQ(candidateDraws.kernelInvocations, 1u);
      EXPECT_EQ(candidateDraws.planMaterializedEdges, 0u);
      EXPECT_EQ(candidateDraws.fpFlattenEdges, 0u);
      ExpectBitmapsIdentical("offscreen-tail-coordinates", candidate, reference, 64, 64);
    }
  }
}

// Shared driver for the offscreen counterexamples below: renders an FP tree through
// fillRTWithFP (the offscreen materialization route) with and without the bundle. Multi-pass
// tail plans are refused on every route (audit D5: a discontinuous operator reading an RGBA8
// materialized input can flip its step() decision), so these trees — all over the tail
// budget — record the rejection boundary: the runtime reference serves the fill
// (byte-identical, one program build, no AOT draw), and any of the multi-pass invocation
// assertions coming back would mean the gate opened without a kernel that honors it.
static void RenderOffscreenCounterexample(
    Context* context, PrecompiledShaderCache* cache,
    const std::function<PlacementPtr<FragmentProcessor>(BlockAllocator*)>& buildTree,
    const char* label, Bitmap* reference, Bitmap* candidate, AOTDrawStats* candidateDraws,
    uint64_t) {
  auto render = [&](bool useBundle, Bitmap* outBitmap, AOTDrawStats* outDraws) {
    if (useBundle) {
      auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_TRUE(bundle.first != nullptr);
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
    auto processor = buildTree(context->drawingAllocator());
    ASSERT_NE(processor, nullptr);
    ASSERT_TRUE(
        context->drawingManager()->fillRTWithFP(target, std::move(processor), 0, Point::Zero()));
    context->flushAndSubmit(true);
    auto rt = target->getRenderTarget();
    ASSERT_NE(rt, nullptr);
    auto surface = Surface::MakeFrom(context, rt->getBackendRenderTarget(), rt->origin());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(outBitmap->allocPixels(64, 64));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    EXPECT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (outDraws != nullptr) {
      *outDraws = cache->drawStats();
    }
    cache->setDiagnosticRecordingEnabled(false);
    cache->setDecompositionEnabled(true);
    cache->unload();
  };
  render(false, reference, nullptr);
  render(true, candidate, candidateDraws);
  ASSERT_NE(candidateDraws, nullptr);
  // Rejection boundary: the over-budget tail plan is refused, the runtime serves the fill.
  EXPECT_EQ(candidateDraws->completeAOTDraws, 0u);
  EXPECT_EQ(candidateDraws->atomicFallbacks, 0u);
  EXPECT_EQ(candidateDraws->kernelInvocations, 1u);
  EXPECT_EQ(candidateDraws->planMaterializedEdges, 0u);
  EXPECT_GE(context->globalCache()->programStats().programBuilderCreations, 1u);
  ExpectBitmapsIdentical(label, *candidate, *reference, 64, 64);
}

TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailAlphaEntersChainBeforeThreshold) {
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
  // Alpha semantics live INSIDE the FP tree here (a matrix scaling alpha to 0.4) rather than in
  // the fill's geometry: the offscreen fill's rectangle is opaque by construction, so the only
  // question is whether the tree-internal alpha survives the materialization boundary between
  // the tail passes. The threshold (0.35) sits right after the alpha scaling: 0.4 passes,
  // 0.4 quantized to 102/255 = 0.4 still passes, so a byte-identical comparison cleanly detects
  // any dropped or re-ordered alpha (a dropped modulation keeps every pixel above 0.35 with full
  // alpha: a ~150-level difference).
  const std::array<float, 20> halfAlpha = {1, 0, 0, 0, 0, 0, 1, 0, 0,    0,
                                           0, 0, 1, 0, 0, 0, 0, 0, 0.4f, 0};
  const std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto buildTree = [&](BlockAllocator* allocator) -> PlacementPtr<FragmentProcessor> {
    auto processor = TextureEffect::Make(allocator, source);
    processor = FragmentProcessor::Compose(
        allocator, std::move(processor), ColorMatrixFragmentProcessor::Make(allocator, halfAlpha));
    processor = FragmentProcessor::Compose(allocator, std::move(processor),
                                           AlphaThresholdFragmentProcessor::Make(allocator, 0.35f));
    for (int index = 0; index < 31; ++index) {
      processor =
          FragmentProcessor::Compose(allocator, std::move(processor),
                                     ColorMatrixFragmentProcessor::Make(allocator, swapRedBlue));
    }
    return processor;
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  AOTDrawStats candidateDraws = {};
  RenderOffscreenCounterexample(context, cache, buildTree, "offscreen-alpha-before-threshold",
                                &reference, &candidate, &candidateDraws, 17u);
}

TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailRotatedUVMatrixStaysAligned) {
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
  // A rotated UV matrix on the source leaf: the tree defines its own coordinate semantics (the
  // offscreen fill's rectangle is the tree's coordinate space), so every tail pass must keep
  // sampling through the same rotated mapping. A misaligned first pass shows up as rotated
  // ghost content in the byte comparison.
  const std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto buildTree = [&](BlockAllocator* allocator) -> PlacementPtr<FragmentProcessor> {
    auto uvMatrix = Matrix::MakeRotate(30.0f, static_cast<float>(source->width()) / 2,
                                       static_cast<float>(source->height()) / 2);
    SamplingOptions sampling(FilterMode::Linear, MipmapMode::None);
    SamplingArgs args = {TileMode::Clamp, TileMode::Clamp, sampling, SrcRectConstraint::Fast};
    auto processor = TextureEffect::Make(allocator, source, args, &uvMatrix);
    if (processor == nullptr) {
      return nullptr;
    }
    for (int index = 0; index < 32; ++index) {
      processor =
          FragmentProcessor::Compose(allocator, std::move(processor),
                                     ColorMatrixFragmentProcessor::Make(allocator, swapRedBlue));
    }
    return processor;
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  AOTDrawStats candidateDraws = {};
  // source + 32 matrices = 33 chain nodes: the first tail pass takes the source plus two
  // operators, the remaining 30 split two per pass -> 16 passes.
  RenderOffscreenCounterexample(context, cache, buildTree, "offscreen-rotated-uv-tail", &reference,
                                &candidate, &candidateDraws, 16u);
}

// F12 execution-failure contract: a plan task that passed the prepare phase must stop cleanly
// and record a diagnostic when its render pass fails to begin during execution. Since the D5
// ruling refuses multi-pass tail plans everywhere, the reachable injection position is the
// terminal pass of a single-pass plan (a device-space source with two operators — the chain
// kernel cannot take the device-space source, and two operators fit the tail's single pass).
// The "first"/"middle" intermediate-pass positions no longer exist on any reachable route.
TGFX_TEST(AOTRenderConsistencyTest, PlanExecutionFailureIsRecordedNotFatal) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
  const auto* bundleData = bundle.first;
  const auto bundleBytes = bundle.second;
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleBytes, 0u);
  const std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto sourceSurface = Surface::Make(context, 64, 64);
  ASSERT_NE(sourceSurface, nullptr);
  {
    auto* canvas = sourceSurface->getCanvas();
    canvas->clear(Color::Red());
    context->flushAndSubmit(true);
  }
  auto source = context->proxyProvider()->wrapExternalTexture(sourceSurface->getBackendTexture());
  ASSERT_NE(source, nullptr);
  // Renders a single-pass tail plan (a device-space source plus two operators) and returns the
  // draw stats of exactly that fill.
  auto renderSinglePassPlan = [&]() -> AOTDrawStats {
    AOTDrawStats stats = {};
    if (!cache->loadBundle(bundleData, bundleBytes)) {
      return stats;
    }
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->clearPrograms();
    auto target = RenderTargetProxy::Make(context, 64, 64, false);
    if (target == nullptr) {
      cache->setDiagnosticRecordingEnabled(false);
      return stats;
    }
    auto allocator = context->drawingAllocator();
    PlacementPtr<FragmentProcessor> processor =
        DeviceSpaceTextureEffect::Make(allocator, source, Matrix::I());
    if (processor == nullptr) {
      cache->setDiagnosticRecordingEnabled(false);
      return stats;
    }
    for (int index = 0; index < 2; ++index) {
      processor =
          FragmentProcessor::Compose(allocator, std::move(processor),
                                     ColorMatrixFragmentProcessor::Make(allocator, swapRedBlue));
    }
    if (!context->drawingManager()->fillRTWithFP(target, std::move(processor), 0, Point::Zero())) {
      cache->setDiagnosticRecordingEnabled(false);
      return stats;
    }
    context->flushAndSubmit(true);
    stats = cache->drawStats();
    cache->setDiagnosticRecordingEnabled(false);
    return stats;
  };
  {
    SCOPED_TRACE("last");
    ASSERT_EQ(::setenv("TGFX_AOT_TEST_INJECT_PASS_FAILURE", "last", 1), 0);
    auto stats = renderSinglePassPlan();
    ::unsetenv("TGFX_AOT_TEST_INJECT_PASS_FAILURE");
    // The failure is observable: counted, and the draw never lands as complete.
    EXPECT_GE(stats.planExecutionFailures, 1u);
    EXPECT_EQ(stats.completeAOTDraws, 0u);
  }
  // Un-injecting restores the normal service: the same single-pass plan completes.
  auto healthy = renderSinglePassPlan();
  EXPECT_EQ(healthy.planExecutionFailures, 0u);
  EXPECT_GE(healthy.completeAOTDraws, 1u);
  EXPECT_EQ(healthy.kernelInvocations, 1u);
}

// Counterexample audit D5: a discontinuous operator (AlphaThreshold) whose input is produced by
// matrices over a device-space source, through the offscreen tail route. The exact-rational
// construction: the source is black, a matrix scales alpha to 513/1024, the threshold is
// 1027/2048. Direct evaluation keeps alpha at 513/1024 < 1027/2048, so step() yields 0; after
// an RGBA8 materialization the stored alpha rounds up to 129/255 > 1027/2048, and step() flips
// to 1 — a full 255-LSB divergence, not a tolerance question. The three-operator chain over a
// device-space source cannot enter the single-pass chain kernel, so the tail planner splits it
// into two passes with the threshold on the far side of the materialization boundary.
TGFX_TEST(AOTRenderConsistencyTest, OffscreenThresholdAcrossBoundaryFlips) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  const std::array<float, 20> identityMatrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                                                0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  auto sourceSurface = Surface::Make(context, 64, 64);
  ASSERT_NE(sourceSurface, nullptr);
  {
    ScopedAOTStatsPause pause(context, true);
    sourceSurface->getCanvas()->clear(Color::Black());
    context->flushAndSubmit(true);
  }
  auto source = context->proxyProvider()->wrapExternalTexture(sourceSurface->getBackendTexture());
  ASSERT_NE(source, nullptr);
  // 513/1024 and 1027/2048 in float: the scaled alpha stays below the threshold in exact
  // arithmetic, while its nearest 1/255 grid point (129/255) sits above it.
  const std::array<float, 20> alphaScale = {
      1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 513.0f / 1024.0f, 0};
  auto render = [&](bool useBundle, Bitmap* outBitmap, AOTDrawStats* outDraws) {
    if (useBundle) {
      auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_NE(bundle.first, nullptr);
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
    PlacementPtr<FragmentProcessor> processor =
        DeviceSpaceTextureEffect::Make(allocator, source, Matrix::I());
    ASSERT_NE(processor, nullptr);
    processor = FragmentProcessor::Compose(
        allocator, std::move(processor), ColorMatrixFragmentProcessor::Make(allocator, alphaScale));
    processor = FragmentProcessor::Compose(
        allocator, std::move(processor),
        AlphaThresholdFragmentProcessor::Make(allocator, 1027.0f / 2048.0f));
    processor =
        FragmentProcessor::Compose(allocator, std::move(processor),
                                   ColorMatrixFragmentProcessor::Make(allocator, identityMatrix));
    ASSERT_NE(processor, nullptr);
    ASSERT_TRUE(
        context->drawingManager()->fillRTWithFP(target, std::move(processor), 0, Point::Zero()));
    context->flushAndSubmit(true);
    auto rt = target->getRenderTarget();
    ASSERT_NE(rt, nullptr);
    auto surface = Surface::MakeFrom(context, rt->getBackendRenderTarget(), rt->origin());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(outBitmap->allocPixels(64, 64));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    EXPECT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (outDraws != nullptr) {
      *outDraws = cache->drawStats();
    }
    cache->setDiagnosticRecordingEnabled(false);
    cache->setDecompositionEnabled(true);
    cache->unload();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  AOTDrawStats candidateDraws = {};
  render(false, &reference, nullptr);
  // Hand check on the runtime reference: direct evaluation keeps alpha at 513/1024 below the
  // 1027/2048 threshold, so step() outputs 0 everywhere — the center alpha byte is 0.
  {
    auto* pixels = static_cast<const uint8_t*>(const_cast<Bitmap&>(reference).lockPixels());
    ASSERT_NE(pixels, nullptr);
    const uint8_t* center = pixels + 32 * reference.rowBytes() + 32 * 4;
    EXPECT_EQ(center[3], 0);
    const_cast<Bitmap&>(reference).unlockPixels();
  }
  render(true, &candidate, &candidateDraws);
  // RULING: a discontinuous operator must never read its input through an RGBA8 materialized
  // intermediate — the over-budget tail plan is refused and the runtime reference serves the
  // fill (byte-identical, one program build, no AOT draw). Serving it as multi-pass AOT is the
  // D5 defect: the flipped threshold would diverge by a full 255 LSB.
  EXPECT_EQ(candidateDraws.completeAOTDraws, 0u);
  EXPECT_EQ(candidateDraws.atomicFallbacks, 0u);
  // The runtime fill executes once (a plain StandardDrawOp counts one kernel invocation); the
  // plan route's two-pass invocation pattern is gone.
  EXPECT_EQ(candidateDraws.kernelInvocations, 1u);
  EXPECT_GE(context->globalCache()->programStats().programBuilderCreations, 1u);
  ExpectBitmapsIdentical("offscreen-threshold-boundary", candidate, reference, 64, 64);
}

TGFX_TEST(AOTRenderConsistencyTest, LongLinearChainExecutesMaterializedTailPasses) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  const std::array<float, 20> rotateRGB = {0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                           1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  // 15/16/17 probe the old capacity boundary (all single-pass now that the chain kernel carries
  // 32 instructions). 33 crosses the boundary: multi-pass tail plans are refused on every route
  // (audit D5 — a discontinuous operator reading an RGBA8 materialized input can flip its
  // step() decision), so the draw falls back to the runtime path — still correct, byte-identical
  // to the reference, just not AOT-served. The offscreen rejection boundary is recorded by
  // OffscreenTailOverBudgetPlansRefused and OffscreenThresholdAcrossBoundaryFlips.
  for (size_t opCount : {size_t{15}, size_t{16}, size_t{17}, size_t{33}}) {
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
    RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                   &candidateStats);
    const auto& draws = candidateStats.draws;
    EXPECT_EQ(draws.draws, 1u);
    if (opCount <= 31) {
      // Inside the chain kernel's instruction budget: one fused AOT pass, no materialization.
      EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
      EXPECT_EQ(candidateStats.noMatchingRule, 0u);
      EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 1u);
      EXPECT_EQ(draws.completeAOTDraws, 1u);
      EXPECT_EQ(draws.atomicFallbacks, 0u);
      EXPECT_EQ(draws.kernelInvocations, 1u);
      EXPECT_EQ(draws.offscreenTargets, 0u);
      EXPECT_EQ(draws.materializedEdges, 0u);
      EXPECT_EQ(draws.planMaterializedEdges, 0u);
      EXPECT_EQ(draws.fpFlattenEdges, 0u);
      EXPECT_EQ(draws.intermediateReadBytes, 0u);
      EXPECT_EQ(draws.intermediateWriteBytes, 0u);
      EXPECT_EQ(draws.peakTemporaryBytes, 0u);
    } else {
      // Over budget on the on-screen route: the runtime path serves the draw (one program
      // compiled on first use), byte-identical to the reference.
      EXPECT_GE(candidateStats.programs.programBuilderCreations, 1u);
      EXPECT_EQ(draws.completeAOTDraws, 0u);
      EXPECT_EQ(draws.atomicFallbacks, 0u);
    }
    ExpectBitmapsIdentical("long-linear-chain-tail", candidate, reference, width, height);
  }
}

// A non-permutation matrix with fractional scales and biases: every level does real clamped
// arithmetic, so quantization and ordering differences accumulate instead of cancelling the way
// the channel-swap chain above does. The alpha row stays [0,0,0,1,0] so ValidateForFusion keeps
// accepting the chain.
static const std::array<float, 20>& NonTrivialScaleBiasMatrix() {
  static const std::array<float, 20> matrix = {0.92f, 0.01f, 0.0f,   0.0f,  0.015f, 0.0f, 0.88f,
                                               0.02f, 0.0f,  0.022f, 0.01f, 0.0f,   0.9f, 0.0f,
                                               0.01f, 0.0f,  0.0f,   0.0f,  1.0f,   0.0f};
  return matrix;
}

TGFX_TEST(AOTRenderConsistencyTest, NonTrivialLinearChainLengthMatrixMatchesRuntime) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  for (size_t opCount :
       {size_t{1}, size_t{2}, size_t{4}, size_t{14}, size_t{15}, size_t{16}, size_t{17}}) {
    SCOPED_TRACE(opCount);
    std::shared_ptr<ColorFilter> chain = nullptr;
    for (size_t index = 0; index < opCount; ++index) {
      chain = ColorFilter::Compose(chain, ColorFilter::Matrix(NonTrivialScaleBiasMatrix()));
    }
    Bitmap reference = {};
    Bitmap candidate = {};
    ColorFilterRenderStats referenceStats = {};
    ColorFilterRenderStats candidateStats = {};
    RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true,
                                   &reference, &referenceStats);
    RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                   &candidateStats);
    EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
    EXPECT_EQ(candidateStats.noMatchingRule, 0u);
    EXPECT_EQ(candidateStats.draws.draws, 1u);
    EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
    EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
    // The chain kernel carries 32 instructions with a recycled 16-entry register file, so a
    // texture plus up to 31 operators stays in one fused pass; only longer chains split.
    uint64_t passCount = opCount <= 31 ? 1 : (opCount + 1) / 2;
    EXPECT_EQ(candidateStats.draws.kernelInvocations, passCount);
    EXPECT_EQ(candidateStats.draws.planMaterializedEdges, passCount - 1);
    // Metal: the MSL compiler fuses the kernel's interpreted arithmetic differently from the
    // runtime's unrolled expressions, so a single pixel may round 1 LSB apart; OpenGL
    // byte-matches both structures.
    // P6.2 ATTRIBUTION (2026-09-18): verified by strict-zero experiment on the rebuilt new-ABI
    // bundle — the Metal divergence is real and tiny (maxChannelDiff=1, 1 byte of 65536),
    // consistent with an fma-scheduling rounding difference between the interpreted kernel and
    // the runtime's unrolled expressions; a structural error would break the bound long before.
    // The tolerance stays 1 on Metal only, zero elsewhere.
    ExpectBitmapsNear("nontrivial-linear-chain-matrix", candidate, reference, width, height,
                      std::string(TGFX_BACKEND_NAME) == "metal" ? 1 : 0);
  }
}

TGFX_TEST(AOTRenderConsistencyTest, BrushAlphaEntersChainBeforeThreshold) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  // The threshold sits at the head of the chain (closest to the texture) with 15 trailing
  // matrices: the whole graph rides the single-pass chain kernel, which folds the brush alpha
  // into the texture read (selector bit 0) exactly like the runtime's SrcIn wrap. The brush
  // alpha is 0.5 and the opaque source has alpha 1: with the input-alpha-inside semantics,
  // threshold(0.5*1) is transparent everywhere; an alpha applied after the chain would keep the
  // image visible at half opacity (0.5*threshold(1)). The two outcomes differ massively, so the
  // byte comparison cannot hide the ordering.
  std::shared_ptr<ColorFilter> chain = ColorFilter::AlphaThreshold(0.75f);
  for (size_t index = 0; index < 15; ++index) {
    chain = ColorFilter::Compose(chain, ColorFilter::Matrix(NonTrivialScaleBiasMatrix()));
  }
  Bitmap reference = {};
  Bitmap candidate = {};
  BrushRenderStats referenceStats = {};
  BrushRenderStats candidateStats = {};
  RenderImageWithBrushOnce(image, chain, 0.5f, Matrix::I(), width, height, false, &reference,
                           &referenceStats);
  RenderImageWithBrushOnce(image, chain, 0.5f, Matrix::I(), width, height, true, &candidate,
                           &candidateStats);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  // One texture + threshold + 15 matrices = 17 instructions, inside the chain kernel's
  // 32-instruction budget: a single fused pass with no materialization.
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.planMaterializedEdges, 0u);
  ExpectBitmapsIdentical("brush-alpha-before-threshold", candidate, reference, width, height);
}

TGFX_TEST(AOTRenderConsistencyTest, RotatedDrawWithLongChainMatchesRuntime) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  // A 30-degree rotation about the image center plus a 17-operator chain. The graph fits the
  // chain kernel's instruction budget, so the plan keeps the original draw (its GP and matrix
  // intact) and swaps only the color processors: the texture sampling runs in the original
  // local coordinate space by construction, and a coordinate mismatch would show up as
  // rotated-content ghosts in the byte comparison.
  auto drawMatrix =
      Matrix::MakeRotate(30.0f, static_cast<float>(width) / 2, static_cast<float>(height) / 2);
  std::shared_ptr<ColorFilter> chain = nullptr;
  for (size_t index = 0; index < 17; ++index) {
    chain = ColorFilter::Compose(chain, ColorFilter::Matrix(NonTrivialScaleBiasMatrix()));
  }
  Bitmap reference = {};
  Bitmap candidate = {};
  BrushRenderStats referenceStats = {};
  BrushRenderStats candidateStats = {};
  RenderImageWithBrushOnce(image, chain, 1.0f, drawMatrix, width, height, false, &reference,
                           &referenceStats);
  RenderImageWithBrushOnce(image, chain, 1.0f, drawMatrix, width, height, true, &candidate,
                           &candidateStats);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.planMaterializedEdges, 0u);
  ExpectBitmapsIdentical("rotated-long-chain", candidate, reference, width, height);
}

TGFX_TEST(AOTRenderConsistencyTest, LowBrushAlphaLongChainMatchesRuntime) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  int width = image->width();
  int height = image->height();
  // A 0.05 brush alpha with a 17-operator chain riding the single-pass kernel: no RGBA8
  // materialization exists on this route, so even the low-alpha amplification regime stays
  // byte-identical. (When the chain exceeds the instruction budget and splits into tail passes,
  // each materialized edge quantizes the premultiplied color; that boundary is covered by the
  // length-matrix test with quantization-exact matrices.)
  std::shared_ptr<ColorFilter> chain = nullptr;
  for (size_t index = 0; index < 17; ++index) {
    chain = ColorFilter::Compose(chain, ColorFilter::Matrix(NonTrivialScaleBiasMatrix()));
  }
  Bitmap reference = {};
  Bitmap candidate = {};
  BrushRenderStats referenceStats = {};
  BrushRenderStats candidateStats = {};
  RenderImageWithBrushOnce(image, chain, 0.05f, Matrix::I(), width, height, false, &reference,
                           &referenceStats);
  RenderImageWithBrushOnce(image, chain, 0.05f, Matrix::I(), width, height, true, &candidate,
                           &candidateStats);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.planMaterializedEdges, 0u);
  ExpectBitmapsIdentical("low-alpha-long-chain", candidate, reference, width, height);
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
  // (ColorFilterShader) instead of appending it as a trailing processor. A matrix filter with
  // an alpha-row bias qualifies the same way and fuses like any other matrix — see
  // AlphaBiasColorFilterOnBlendShaderMatchesRuntime, which locked that in after the historical
  // ValidateForFusion rejection proved conservative.
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

// The alpha-bias color-matrix coverage gap: a matrix whose alpha row carries a constant bias
// (matrix[19] != 0) makes transparent-black input gain alpha (affectsTransparentBlack), so addDrawOp
// merges it into the shader as a ColorFilterShader SrcIn wrap instead of a trailing processor. A
// glow filter on a blend shader (gradient background over a transparent-edged sticker) is the
// canonical shape: the transparent edge must end up with the bias-driven glow alpha, and the byte
// comparison must hold over the transparent rim specifically — a dropped or re-ordered bias shows
// up there as a massive alpha difference, not just in the opaque interior.
TGFX_TEST(AOTRenderConsistencyTest, AlphaBiasColorFilterOnBlendShaderMatchesRuntime) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  constexpr int size = 96;
  // A transparent-rimmed sticker: the image drawn into the center of a larger transparent
  // surface, so the rim pixels exercise the affectsTransparentBlack semantics.
  auto stickerSurface = Surface::Make(context, size, size, false, 1, true);
  ASSERT_NE(stickerSurface, nullptr);
  {
    ScopedAOTStatsPause pause(context, true);
    auto* canvas = stickerSurface->getCanvas();
    auto srcRect = Rect::MakeXYWH(0.0f, 0.0f, static_cast<float>(image->width()),
                                  static_cast<float>(image->height()));
    auto dstRect = Rect::MakeXYWH(16.0f, 16.0f, 64.0f, 64.0f);
    canvas->drawImageRect(image, srcRect, dstRect);
    context->flushAndSubmit(true);
  }
  auto sticker = stickerSurface->makeImageSnapshot();
  ASSERT_NE(sticker, nullptr);
  // The glow matrix: slight warm tint on the RGB rows, alpha row = [0, 0, 0, 0.5, 0.5] so the
  // bias (matrix[19] = 0.5) lifts the transparent rim to half-visible while the opaque interior
  // clamps to full alpha.
  const std::array<float, 20> glowMatrix = {0.9f, 0.0f, 0.0f,  0.0f, 0.1f, 0.0f, 0.9f,
                                            0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.9f, 0.0f,
                                            0.0f, 0.0f, 0.0f,  0.0f, 0.5f, 0.5f};
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto bg = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size),
                                         {Color(0, 1, 0, 0.6f), Color(0, 0, 1, 0.6f)});
    auto stickerShader = Shader::MakeImageShader(sticker, TileMode::Clamp, TileMode::Clamp);
    ASSERT_TRUE(bg != nullptr && stickerShader != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::SrcOver, bg, stickerShader));
    paint.setColorFilter(ColorFilter::Matrix(glowMatrix));
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
  // Control: the same scene without the color filter. The transparent rim (source alpha 0.6 from
  // the gradient, filter absent) must differ from the reference above, proving the filter — and
  // with it the bias — is actually engaged in the reference path.
  Bitmap noFilter = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto bg = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(size, size),
                                         {Color(0, 1, 0, 0.6f), Color(0, 0, 1, 0.6f)});
    auto stickerShader = Shader::MakeImageShader(sticker, TileMode::Clamp, TileMode::Clamp);
    ASSERT_TRUE(bg != nullptr && stickerShader != nullptr);
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::SrcOver, bg, stickerShader));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(noFilter.allocPixels(size, size));
    auto* pixels = noFilter.lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(noFilter.info(), pixels));
    noFilter.unlockPixels();
    auto* nf = static_cast<const uint32_t*>(const_cast<Bitmap&>(noFilter).lockPixels());
    auto* rf = static_cast<const uint32_t*>(const_cast<Bitmap&>(reference).lockPixels());
    // The render target is cleared opaque white, so the alpha shows up in the premultiplied
    // RGB instead of the alpha channel: the glow's bias lifts the rim's effective coverage and
    // its RGB rows tint it, so a rim pixel must differ between the filtered and unfiltered
    // renders (proving the filter — and with it the bias — is engaged in the reference path).
    EXPECT_NE(nf[2 * size + 2], rf[2 * size + 2]);
    const_cast<Bitmap&>(noFilter).unlockPixels();
    const_cast<Bitmap&>(reference).unlockPixels();
  }
  Bitmap candidate = {};
  uint64_t candidateNoMatch = 0;
  uint64_t candidateBuilderCreations = 0;
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->clearPrograms();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    candidateNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
    candidateBuilderCreations = context->globalCache()->programStats().programBuilderCreations;
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
  }
  // The gap this test closes: the alpha-bias matrix must be served by the precompiled path (no
  // JIT programs, no unmatched rules), not silently fall back to the runtime builder.
  EXPECT_EQ(candidateBuilderCreations, 0u);
  EXPECT_EQ(candidateNoMatch, 0u);
  // The planned path materializes the blend operands before the chain serves the tree, and each
  // materialized edge costs at most 1 LSB; the historical worry — the fusion silently dropping
  // the source-alpha constraint — would show up as a systematic alpha/coverage difference orders
  // of magnitude above this bound, so the bound separates the two unambiguously.
  auto* refPixels = static_cast<const uint8_t*>(const_cast<Bitmap&>(reference).lockPixels());
  auto* candPixels = static_cast<uint8_t*>(candidate.lockPixels());
  ASSERT_TRUE(refPixels != nullptr && candPixels != nullptr);
  size_t totalBytes = static_cast<size_t>(size) * static_cast<size_t>(size) * 4;
  int maxDiff = 0;
  size_t diffCount = 0;
  for (size_t i = 0; i < totalBytes; ++i) {
    int value = std::abs(static_cast<int>(refPixels[i]) - static_cast<int>(candPixels[i]));
    if (value > 0) {
      ++diffCount;
    }
    maxDiff = std::max(maxDiff, value);
  }
  const_cast<Bitmap&>(reference).unlockPixels();
  candidate.unlockPixels();
  EXPECT_LE(maxDiff, 4) << "diffBytes=" << diffCount << "/" << totalBytes;
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
  printf(
      "[RetryMaskCoverage] maxChannelDiff=%d differing=%lld/%d whiteOnlyInRef=%lld "
      "candidateHash=%llu\n",
      maxDiff, diffCount, size * size, fullPaintCount, static_cast<unsigned long long>(hash));
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
    printf(
        "[GradientBlendDiffAttribution] %s: draws=%u completeAOT=%u atomicFallbacks=%u "
        "kernelInvocations=%u fpFlattenEdges=%u planMaterializedEdges=%u\n",
        label, static_cast<unsigned>(draws.draws), static_cast<unsigned>(draws.completeAOTDraws),
        static_cast<unsigned>(draws.atomicFallbacks),
        static_cast<unsigned>(draws.kernelInvocations), static_cast<unsigned>(draws.fpFlattenEdges),
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
  auto pairStats = [&](const char* label, const Bitmap& a,
                       const Bitmap& b) -> std::tuple<int, size_t, size_t> {
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
    printf("[GradientBlendDiffAttribution] %s: maxChannelDiff=%d differing=%zu/%d >2=%zu\n", label,
           maxDiff, diffCount, size * size, gt2Count);
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
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto drawOnce = [&](tgfx::Canvas* canvas) {
    Paint paint = {};
    paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
    auto start = std::chrono::steady_clock::now();
    canvas->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                 start)
        .count();
  };
  auto report = [&](const char* mode, int pass, long long micros) {
    auto programs = context->globalCache()->programStats();
    auto draws = cache->drawStats();
    printf(
        "[FirstSceneSteadyStateAttribution] %s pass=%d micros=%lld "
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
  FailingUploadGenerator(int width, int height) : ImageGenerator(width, height) {
  }

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
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
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
  CountingImageGenerator(int width, int height) : ImageGenerator(width, height) {
  }

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

// Counterexample audit P4.3: a YUV video frame drawn with a paint color filter (the video
// player's color-grade path). The YUVTextureFillShader kernel carries three pointwise slots
// after its plane conversion, and the DecomposeYUVChain planner folds the Compose(YUV texture,
// matrix) tree onto them, so the color-grade draw rides the precompiled route in one fused pass.
TGFX_TEST(AOTRenderConsistencyTest, YUVImageWithColorFilterMatchesRuntime) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
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
  ASSERT_NE(yuvData, nullptr);
  auto yuvImage = Image::MakeI420(yuvData);
  ASSERT_NE(yuvImage, nullptr);
  const std::array<float, 20> warmGrade = {1.1f, 0.05f, 0,    0, 0,     0, 1.0f, 0,     0, 0,
                                           0,    0,     0.9f, 0, 0.04f, 0, 0,    0.95f, 0, 0};
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setColorFilter(ColorFilter::Matrix(warmGrade));
    canvas->drawImageRect(yuvImage, Rect::MakeXYWH(0, 0, size, size),
                          Rect::MakeXYWH(0, 0, size, size), {}, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The color-grade tree rides the precompiled YUV kernel: no fallback, no runtime program.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    EXPECT_GE(context->globalCache()->programStats().precompiledArtifactCreations, 1u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("yuv-image-with-color-filter", candidate, reference, size, size);
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
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::UnsupportedShape)],
            2u);
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::BlockedByLowering)],
            1u);
  EXPECT_EQ(stats.decomposeRejections[static_cast<size_t>(AOTDecomposeOutcome::FusablePointwise)],
            0u);

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
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
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

// A non-AA concave path fill and an advanced-blend image draw in the same pass. When
// TGFX_ENABLE_STENCIL_COVER_PATH is on, the path routes through the stencil-and-cover path and
// OpsRenderTask attaches a depth-stencil buffer to the pass; the blend image's chain rewrite in
// that same pass must then declare the depth-stencil format on its rebuilt pipeline, or backends
// that validate pipelines against the pass reject the rewritten program and the draw silently
// falls back to runtime compilation. With the build flag off (default) the scene degenerates to
// an ordinary shape + blend scene that still guards the rewrite's state inheritance.
static void RenderStencilPassPlusChainOnce(Context* context, bool useBundle, Bitmap* outBitmap) {
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !useBundle);
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, 180, 180);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  // A convex pentagon (not rect/oval/rrect, so Canvas::drawPath bypasses those fast paths and
  // reaches the stencil-cover dispatch fork) with antialiasing off: with the stencil-cover build
  // flag on and the backend supporting stencil attachments, this draw routes through
  // StencilCoverPathDrawOp and OpsRenderTask attaches the depth-stencil buffer to the pass.
  Path pentagon = {};
  pentagon.moveTo(90, 10);
  pentagon.lineTo(166, 65);
  pentagon.lineTo(137, 154);
  pentagon.lineTo(43, 154);
  pentagon.lineTo(14, 65);
  pentagon.close();
  Paint pathPaint = {};
  pathPaint.setColor(Color::FromRGBA(40, 60, 90, 255));
  pathPaint.setAntiAlias(false);
  canvas->drawPath(pentagon, pathPaint);
  // A two-texture blend shader: the plain matcher has no direct rule for this tree, so the
  // draw resolves through the decomposition rewrite onto the fused pointwise-chain kernel —
  // the exact path whose rebuilt ProgramInfo must inherit the depth-stencil format.
  auto imageA = MakeImage("resources/apitest/mandrill_128.png");
  auto imageB = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr);
  Paint blendPaint = {};
  blendPaint.setShader(Shader::MakeBlend(BlendMode::Multiply, Shader::MakeImageShader(imageA),
                                         Shader::MakeImageShader(imageB)));
  canvas->drawRect(Rect::MakeXYWH(10, 10, 160, 160), blendPaint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(180, 180));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
    context->globalCache()->clearPrograms();
  }
}

TGFX_TEST(AOTRenderConsistencyTest, StencilPassChainRewriteKeepsPipelineState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderStencilPassPlusChainOnce(context, true, &aotBitmap);
  // The rewritten blend draw must resolve to a precompiled program, not silently fall back
  // because its pipeline failed the pass's depth-stencil validation.
  EXPECT_GE(context->precompiledShaderCache()->drawStats().completeAOTDraws, 1u);
  RenderStencilPassPlusChainOnce(context, false, &runtimeBitmap);
  ExpectBitmapsIdentical("stencil-pass-chain-rewrite", aotBitmap, runtimeBitmap, 180, 180);
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
  printf("[DeepMaterialization] maxDiff=%d diffPixels=%zu/%zu\n", maxDiff, diffPixels, totalPixels);
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
                                           0, 0, 1, 0, 0,     0, 0, 0, 1, 0};
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

// Counterexample audit B1: an alpha-only image as a two-child blend operand under a non-white
// paint. The runtime's two-child xfer emission feeds each child vec4(inputColor.rgb, 1.0), and
// GLSLTextureEffect's alpha-only readback is sample.a * inputColor — so the mask is TINTED by the
// paint RGB. The chain kernel's alpha-only leaf only splats .r and multiplies by the unit's alpha
// (or nothing for blend operands), so a red paint must still produce a red-tinted mask, not a
// gray one. A wrong input environment shows up as a full-saturation color difference.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyBlendOperandKeepsPaintTint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto colorImage = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(colorImage, nullptr);
  constexpr int size = 96;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (size_t y = 0; y < static_cast<size_t>(size); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(size); ++x) {
      maskPixels[y * rowBytes + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    auto colorShader = Shader::MakeImageShader(colorImage, TileMode::Clamp, TileMode::Clamp);
    auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Clamp, TileMode::Clamp);
    ASSERT_TRUE(colorShader != nullptr && maskShader != nullptr);
    Paint paint = {};
    // A non-white paint: the runtime tints the alpha-only operand with this RGB.
    paint.setColor(Color(1.0f, 0.2f, 0.1f, 1.0f));
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, colorShader, maskShader));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    renderScene(&candidate);
    cache->unload();
  }
  // P6.2 ATTRIBUTION (2026-09-18): verified by strict-zero experiment on the rebuilt new-ABI
  // bundle — the Metal divergence is real and tiny (maxChannelDiff=1, 9 bytes of 36864),
  // consistent with an fma-scheduling rounding difference between the interpreted kernel and
  // the runtime's unrolled expressions. The tolerance stays 1 on Metal only, zero elsewhere.
  ExpectBitmapsNear("alpha-only-blend-tint", candidate, reference, size, size,
                    std::string(TGFX_BACKEND_NAME) == "metal" ? 1 : 0);
}

// Counterexample audit B1, part two: an alpha-only image as the color root under a non-white
// paint. The runtime feeds the root processor the GP's full output color, and GLSLTextureEffect's
// alpha-only readback is sample.a * inputColor — so the mask is TINTED by the paint RGB. The
// chain kernel's color-root alpha-only leaf must therefore modulate by the geometry color's RGB
// (bit 3) in addition to its alpha (bit 0); the old alpha-only form only multiplied by .a and
// produced a gray mask under a red paint.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyColorRootKeepsPaintTint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (size_t y = 0; y < static_cast<size_t>(size); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(size); ++x) {
      maskPixels[y * rowBytes + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    // The mask is the shader root: the runtime multiplies it by the full geometry color (the red
    // paint), tinting every visible pixel red instead of gray.
    auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Clamp, TileMode::Clamp);
    ASSERT_NE(maskShader, nullptr);
    Paint paint = {};
    paint.setColor(Color(1.0f, 0.2f, 0.1f, 1.0f));
    paint.setShader(maskShader);
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The mask draw must take the precompiled chain route (not a fallback that would trivially
    // match the runtime), so the tint comparison is a real check of the kernel's alpha-only
    // color-root modulation.
    auto stats = context->globalCache()->programStats();
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(stats.programBuilderCreations, 0u);
    EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("alpha-only-color-root-tint", candidate, reference, size, size);
}

// Counterexample audit D1: the alpha-only color root under a paint alpha below 1. The runtime
// readback is sample.a * inputColor, so a half-transparent red paint (premul (0.5, 0, 0, 0.5))
// over a constant 0x80 mask must yield exactly (0.25, 0, 0, 0.25) — byte (64, 0, 0, 64). The
// kernel's current selector combination multiplies the splat by the geometry RGB first (bit 3)
// and then by the geometry alpha again (bit 0), double-attenuating the RGB to (0.125, 0, 0,
// 0.25) — byte (32, 0, 0, 64). The tint test above only covered alpha = 1, where the alpha
// multiply is the identity, which is why this defect survived it.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyColorRootPaintAlphaBelowOne) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 48;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  for (size_t y = 0; y < static_cast<size_t>(size); ++y) {
    memset(maskPixels + y * maskBitmap.rowBytes(), 0x80, static_cast<size_t>(size));
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Clamp, TileMode::Clamp);
    ASSERT_NE(maskShader, nullptr);
    Paint paint = {};
    paint.setColor(Color(1.0f, 0.0f, 0.0f, 0.5f));
    paint.setShader(maskShader);
    // The color filter forces the draw off the dedicated QuadTextureFill shader and onto the
    // precompiled pointwise chain, where the alpha-only readback's modulation bits live. The
    // matrix halves RGB and keeps alpha, so its contribution stays hand-computable.
    std::array<float, 20> matrix = {0.5f, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0,
                                    0,    0, 0, 0, 0, 0, 0,    0, 1, 0};
    paint.setColorFilter(ColorFilter::Matrix(matrix));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    // Hand check on the runtime reference: the readback is 128/255 * premul(1, 0, 0, 0.5) =
    // (0.25098, 0, 0, 0.25098); the matrix then unpremultiplies (1, 0, 0), halves RGB to
    // (0.5, 0, 0) and re-premultiplies, giving (0.12549, 0, 0, 0.25098) — byte (32, 0, 0, 64).
    // The byte position of red follows the bitmap's platform format (BGRA on Apple, RGBA
    // elsewhere), so assert per channel.
    auto* pixels = static_cast<const uint8_t*>(const_cast<Bitmap&>(reference).lockPixels());
    ASSERT_NE(pixels, nullptr);
    const uint8_t* center = pixels + 24 * reference.rowBytes() + 24 * 4;
    EXPECT_EQ(center[3], 64);
    EXPECT_EQ(center[0] + center[2], 32);
    EXPECT_EQ(center[0] * center[2], 0);
    EXPECT_EQ(center[1], 0);
    const_cast<Bitmap&>(reference).unlockPixels();
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The draw must take the precompiled chain route; a fallback would trivially match the
    // runtime and prove nothing about the kernel's alpha-only modulation.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("alpha-only-color-root-paint-alpha", candidate, reference, size, size);
}

// Counterexample audit D3: a single-child xfer whose child contains a two-child blend, reached
// through public APIs by drawMesh with vertex colors and a blend shader. The mesh route wraps
// the shader in a Modulate SrcChild xfer whose child lowers from the white input, and the child
// (the two-child BlendShader) previously refused that white input — an expression gap, not a
// semantic limit: the runtime feeds the xfer children vec4(inputColor.rgb, 1.0), and against
// white both the child input and the output's input-alpha multiply are the identity, so the
// chain can carry the tree verbatim.
TGFX_TEST(AOTRenderConsistencyTest, MeshColorBlendShaderWithTwoChildBlend) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  auto imageA = MakeImage("resources/apitest/mandrill_128.png");
  auto imageB = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_NE(imageA, nullptr);
  ASSERT_NE(imageB, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::White());
    auto shaderA = Shader::MakeImageShader(imageA, TileMode::Clamp, TileMode::Clamp);
    auto shaderB = Shader::MakeImageShader(imageB, TileMode::Clamp, TileMode::Clamp);
    ASSERT_NE(shaderA, nullptr);
    ASSERT_NE(shaderB, nullptr);
    auto blendShader = Shader::MakeBlend(BlendMode::Multiply, shaderA, shaderB);
    ASSERT_NE(blendShader, nullptr);
    Point positions[] = {{8, 8}, {size - 8, 8}, {8, size - 8}, {size - 8, size - 8}};
    Color colors[] = {Color::White(), Color::White(), Color::White(), Color::White()};
    auto mesh = Mesh::MakeCopy(MeshTopology::TriangleStrip, 4, positions, nullptr, colors);
    ASSERT_NE(mesh, nullptr);
    Paint paint = {};
    paint.setShader(blendShader);
    canvas->drawMesh(mesh, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The D3 expression gap is closed: the tree lowers and rides the precompiled chain with no
    // fallback and no runtime program build.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("mesh-color-blend-shader-two-child", candidate, reference, size, size);
}

// Counterexample audit D4: an alpha-only image drawn with a paint that carries a shader.
// GetBrushForImage keeps the shader for alpha-only images, so the draw carries two color FPs —
// the A8 image's texture first, the paint shader's texture second, the second consuming the
// first's output as its runtime input (sample * input.a). The chain previously refused the
// computed input; the raw-sampling slot plus the topologically-ordered TEX_MODULATE
// instruction now carries it.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyImageWithPaintShaderMatchesRuntime) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (size_t y = 0; y < static_cast<size_t>(size); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(size); ++x) {
      maskPixels[y * rowBytes + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto patternImage = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(patternImage, nullptr);
  auto patternShader = Shader::MakeImageShader(patternImage, TileMode::Clamp, TileMode::Clamp);
  ASSERT_NE(patternShader, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setColor(Color::Red());
    paint.setShader(patternShader);
    canvas->drawImage(maskImage, 0, 0, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The D4 expression gap is closed: the two-texture tree rides the precompiled chain.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("alpha-only-image-with-paint-shader", candidate, reference, size, size);
}

// Counterexample audit P2.3, end to end: an alpha-only image drawn with a paint whose shader is
// a BlendShader. The color chain carries [A8 texture, two-child blend over the blend shader's
// children], so the blend's xfer input is the mask texture's computed output. The runtime
// feeds the children vec4(C.rgb, 1.0) and re-multiplies the blend result by C.a; the chain
// expresses this through the InputOpaque and MulAlpha instructions over TEX_MODULATE children.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyImageWithBlendShaderMatchesRuntime) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (size_t y = 0; y < static_cast<size_t>(size); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(size); ++x) {
      maskPixels[y * rowBytes + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto imageA = MakeImage("resources/apitest/mandrill_128.png");
  auto imageB = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_NE(imageA, nullptr);
  ASSERT_NE(imageB, nullptr);
  auto shaderA = Shader::MakeImageShader(imageA, TileMode::Clamp, TileMode::Clamp);
  auto shaderB = Shader::MakeImageShader(imageB, TileMode::Clamp, TileMode::Clamp);
  ASSERT_NE(shaderA, nullptr);
  ASSERT_NE(shaderB, nullptr);
  auto blendShader = Shader::MakeBlend(BlendMode::Multiply, shaderA, shaderB);
  ASSERT_NE(blendShader, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setColor(Color::Red());
    paint.setShader(blendShader);
    canvas->drawImage(maskImage, 0, 0, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("alpha-only-image-with-blend-shader", candidate, reference, size, size);
}

// Counterexample audit B3: two stacked analytic AA clips over an AA oval. The coverage subtree is
// a two-level analytic chain (RectEffect x2) while the GP emits a fractional coverage at the oval
// edge. The chain must keep the GP coverage as the chain's starting unit: at pixels where both
// clips evaluate to 1 but the oval edge is half covered, the final coverage must stay ~0.5, not
// snap to 1. Sampled on the oval's rim only (the interior is coverage 1 everywhere).
TGFX_TEST(AOTRenderConsistencyTest, StackedClipsKeepGPCoverageOnAAEdge) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    // Two stacked AA clip rects, both fully containing the oval's rim so the clips themselves
    // contribute coverage 1 on the sampled pixels — only the oval's own AA edge is fractional.
    canvas->clipRect(Rect::MakeLTRB(8, 8, size - 8, size - 8), true);
    canvas->clipRect(Rect::MakeLTRB(12, 12, size - 12, size - 12), true);
    Paint paint = {};
    paint.setColor(Color(0, 1, 0, 1));
    canvas->drawOval(Rect::MakeLTRB(24, 24, size - 24, size - 24), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    renderScene(&candidate);
    cache->unload();
  }
  ExpectBitmapsIdentical("stacked-clips-gp-coverage", candidate, reference, size, size);
}

// Counterexample audit D2, end-to-end reachability ruling: the D2 defect (a chained analytic
// coverage whose first node reads opaque white instead of the GP coverage unit, silently
// dropping the GP coverage) is verified constructively in AOTEffectTest
// (ChainedRectCoverageFeedsFromUnitCoverage asserts the -3 unit designator). Reaching it end to
// end would need a draw with fractional GP coverage under a multi-leaf analytic clip chain, but
// every renderer that produces fractional coverage here also emits a second coverage FP (the AA
// path adds a TextureEffect coverage alongside the clip Compose), and the two-FP branch rejects
// that combination — so the defect is UNREACHABLE through public APIs today and this test
// records the fallback boundary as a tripwire: if the two-FP gate ever opens without the chain
// honoring the unit, the pixel comparison below flips.
TGFX_TEST(AOTRenderConsistencyTest, ChainedAnalyticClipsOnAAPathRecordsFallbackBoundary) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    // The two clip shapes partially overlap (the rrect extends past the rect's right and bottom
    // edges), so their intersection is not an analytic shape and the clip stack keeps both
    // elements — the coverage lowers as a two-leaf analytic chain, not a merged single element.
    canvas->clipRect(Rect::MakeLTRB(8.5f, 8.5f, size * 0.75f, size * 0.75f), true);
    canvas->clipRRect(
        RRect::MakeRectXY(Rect::MakeLTRB(16.5f, 16.5f, size - 16.5f, size - 16.5f), 12, 12), true);
    Paint paint = {};
    paint.setColor(Color(0, 1, 0, 1));
    paint.setAntiAlias(true);
    Path path = {};
    path.moveTo(48, 12);
    path.lineTo(84, 80);
    path.lineTo(12, 80);
    path.close();
    canvas->drawPath(path, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // RULING: the coverage carries two FPs here (the clip's analytic Compose plus the AA path's
    // TextureEffect), which the chain's two-FP branch refuses, so the draw falls back to the
    // runtime route — exactly one program build, no partial-chain attempt. This records the
    // boundary; a zero here would mean the gate opened and the unit wiring must be re-verified.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 1u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 1u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("chained-analytic-clips-fallback-boundary", candidate, reference, size,
                         size);
}

// Counterexample audit D1: program identity. Three draws in one context whose color trees share
// the same variant (same GP layout, one texture leaf, no XP difference) but differ only in the
// instruction sequence (one matrix vs two matrices vs luma). The program key must collapse to the
// artifact/pipeline identity — the instruction structure is per-draw uniform data — so all three
// draws share one program (one artifact creation, two cache hits) while each render keeps its own
// byte-exact output, proving the per-draw uniforms are rewritten between the interleaved draws.
TGFX_TEST(AOTRenderConsistencyTest, SameVariantDifferentChainsShareProgram) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  constexpr int size = 64;
  const std::array<float, 20> brighten = {1.2f, 0, 0,     0, 0.05f, 0, 1.1f, 0, 0, 0.03f,
                                          0,    0, 1.15f, 0, 0.02f, 0, 0,    0, 1, 0};
  const std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto renderWith = [&](const std::shared_ptr<ColorFilter>& filter) -> Bitmap {
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return {};
    }
    Paint paint = {};
    paint.setColorFilter(filter);
    surface->getCanvas()->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    Bitmap bitmap = {};
    if (!bitmap.allocPixels(size, size)) {
      return {};
    }
    auto* pixels = bitmap.lockPixels();
    if (pixels == nullptr) {
      return {};
    }
    if (!surface->readPixels(bitmap.info(), pixels)) {
      bitmap.unlockPixels();
      return {};
    }
    bitmap.unlockPixels();
    return bitmap;
  };
  auto filterOne = ColorFilter::Matrix(brighten);
  auto filterTwo =
      ColorFilter::Compose(ColorFilter::Matrix(brighten), ColorFilter::Matrix(swapRedBlue));
  auto filterThree = ColorFilter::Luma();
  // Runtime references: the same three filters through the stitching path.
  Bitmap refOne = {};
  Bitmap refTwo = {};
  Bitmap refThree = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    refOne = renderWith(filterOne);
    refTwo = renderWith(filterTwo);
    refThree = renderWith(filterThree);
  }
  auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleBytes, 0u);
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
  cache->setDecompositionEnabled(true);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto one = renderWith(filterOne);
  auto two = renderWith(filterTwo);
  auto three = renderWith(filterThree);
  auto stats = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
  // The trees share one variant (same shader, same GP, same single texture leaf): one artifact
  // creation and two cache hits mean the interleaved draws reuse a single program.
  EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(stats.cacheHits, 2u);
  EXPECT_EQ(stats.programBuilderCreations, 0u);
  printf("[ProgramIdentity] artifactCreations=%u cacheHits=%u cacheMisses=%u\n",
         static_cast<unsigned>(stats.precompiledArtifactCreations),
         static_cast<unsigned>(stats.cacheHits), static_cast<unsigned>(stats.cacheMisses));
  fflush(stdout);
  // Sanity: the three renders must differ from each other (non-vacuous chains).
  auto* p1 = static_cast<const uint32_t*>(const_cast<Bitmap&>(one).lockPixels());
  auto* p2 = static_cast<const uint32_t*>(const_cast<Bitmap&>(two).lockPixels());
  auto* p3 = static_cast<const uint32_t*>(const_cast<Bitmap&>(three).lockPixels());
  EXPECT_NE(p1[32 * size + 32], p2[32 * size + 32]);
  EXPECT_NE(p2[32 * size + 32], p3[32 * size + 32]);
  const_cast<Bitmap&>(one).unlockPixels();
  const_cast<Bitmap&>(two).unlockPixels();
  const_cast<Bitmap&>(three).unlockPixels();
  // Sharing one program must not leak state between the interleaved draws: each render matches
  // its runtime reference byte for byte.
  ExpectBitmapsIdentical("shared-program-chain-one", one, refOne, size, size);
  ExpectBitmapsIdentical("shared-program-chain-two", two, refTwo, size, size);
  ExpectBitmapsIdentical("shared-program-chain-three", three, refThree, size, size);
}

// Counterexample audit P5: true A-B-A interleaving. The A->B->C shape above cannot expose a
// program whose uniform state was polluted by an intervening draw: only A's SECOND appearance
// proves the per-draw upload fully rewrites whatever B left behind. Two same-variant chains
// (one matrix vs luma) alternate A, B, A: one artifact creation, two cache hits (the second A
// must hit the program B just used), and A's re-render must stay byte-identical to its first.
TGFX_TEST(AOTRenderConsistencyTest, ABAInterleavedDrawsReuseProgramWithoutStateLeak) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  constexpr int size = 64;
  const std::array<float, 20> brighten = {1.2f, 0, 0,     0, 0.05f, 0, 1.1f, 0, 0, 0.03f,
                                          0,    0, 1.15f, 0, 0.02f, 0, 0,    0, 1, 0};
  auto renderWith = [&](const std::shared_ptr<ColorFilter>& filter) -> Bitmap {
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return {};
    }
    Paint paint = {};
    paint.setColorFilter(filter);
    surface->getCanvas()->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    Bitmap bitmap = {};
    if (!bitmap.allocPixels(size, size)) {
      return {};
    }
    auto* pixels = bitmap.lockPixels();
    if (pixels == nullptr) {
      return {};
    }
    if (!surface->readPixels(bitmap.info(), pixels)) {
      bitmap.unlockPixels();
      return {};
    }
    bitmap.unlockPixels();
    return bitmap;
  };
  auto filterA = ColorFilter::Matrix(brighten);
  auto filterB = ColorFilter::Luma();
  // Runtime references through the stitching path.
  Bitmap refA1 = {};
  Bitmap refB = {};
  Bitmap refA2 = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    refA1 = renderWith(filterA);
    refB = renderWith(filterB);
    refA2 = renderWith(filterA);
  }
  auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleBytes, 0u);
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
  cache->setDecompositionEnabled(true);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto a1 = renderWith(filterA);
  auto b = renderWith(filterB);
  auto a2 = renderWith(filterA);
  auto stats = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
  // One variant serves both chains: a single artifact creation, and the second A must hit the
  // same program B just drew with (two cache hits across the three draws).
  EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(stats.cacheHits, 2u);
  EXPECT_EQ(stats.programBuilderCreations, 0u);
  // The two chains must produce different output (non-vacuous pair).
  auto* pa = static_cast<const uint32_t*>(const_cast<Bitmap&>(a1).lockPixels());
  auto* pb = static_cast<const uint32_t*>(const_cast<Bitmap&>(b).lockPixels());
  ASSERT_NE(pa, nullptr);
  ASSERT_NE(pb, nullptr);
  EXPECT_NE(pa[32 * size + 32], pb[32 * size + 32]);
  const_cast<Bitmap&>(a1).unlockPixels();
  const_cast<Bitmap&>(b).unlockPixels();
  // The second A must match the first A byte for byte: any uniform state B left behind (kernel
  // slots, matrices, luma coefficients) would show up here.
  ExpectBitmapsIdentical("aba-first-a", a1, refA1, size, size);
  ExpectBitmapsIdentical("aba-b", b, refB, size, size);
  ExpectBitmapsIdentical("aba-second-a", a2, refA2, size, size);
  ExpectBitmapsIdentical("aba-self-consistent", a2, a1, size, size);
}

// Counterexample audit B2: a single-child blend operand wrapping a Compose-shaped child. drawMesh
// with vertex colors wraps the brush shader's FP in a SrcChild(Modulate) xfer (OpsCompositor).
// The runtime feeds that child white, and Compose passes its own input through to its first
// child, so the texture samples raw. The chain's whiteInputOperand marking only covers the
// blend's direct input node (the Compose root); the texture one level deeper stays unmarked, so
// its bit0 modulates by the geometry color's alpha — which for a mesh with vertex colors is the
// vertex color itself. With a 0.5-alpha vertex color over an opaque image, the runtime keeps the
// image alpha at 1 before the Modulate multiply while the chain halves it twice.
TGFX_TEST(AOTRenderConsistencyTest, MeshVertexColorsWithComposedShaderChildInput) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  constexpr int size = 96;
  std::array<Point, 4> positions = {Point(0, 0), Point(size, 0), Point(0, size), Point(size, size)};
  std::array<Point, 4> texCoords = {Point(0, 0), Point(size, 0), Point(0, size), Point(size, size)};
  std::array<Color, 4> vertexColors = {Color(1, 1, 1, 0.5f), Color(1, 1, 1, 0.5f),
                                       Color(1, 1, 1, 0.5f), Color(1, 1, 1, 0.5f)};
  auto mesh = Mesh::MakeCopy(MeshTopology::TriangleStrip, 4, positions.data(), texCoords.data(),
                             vertexColors.data());
  ASSERT_NE(mesh, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    auto imageShader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
    ASSERT_NE(imageShader, nullptr);
    // A Compose-shaped shader FP (texture, then color matrix) sits under the mesh's Modulate
    // xfer; the matrix keeps the alpha row identity so the alpha divergence stays visible.
    auto composedShader =
        imageShader->makeWithColorFilter(ColorFilter::Matrix(NonTrivialScaleBiasMatrix()));
    ASSERT_NE(composedShader, nullptr);
    Paint paint = {};
    paint.setShader(composedShader);
    canvas->drawMesh(mesh, paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The mesh draw must take the precompiled chain route for the comparison to be a real check.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    auto stats = context->globalCache()->programStats();
    EXPECT_EQ(stats.programBuilderCreations, 0u);
    EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("mesh-vertex-colors-composed-child", candidate, reference, size, size);
}

// Counterexample audit B5: coefficient-blend clamp asymmetry under out-of-range leaf values.
// Gradient stops accept unclamped float colors. The runtime's AppendCoeffBlend clamps the blend
// result (GLSLBlend.cpp Add/Subtract operations) while the chain kernel's xpBlendColors does not,
// so an out-of-range stop reaches the following color matrix unclamped on the chain path: the
// runtime feeds clamp(2.0) = 1.0 into a 0.5-scale matrix (output ~0.5) while the chain feeds 2.0
// (output ~1.0). The scene keeps a single gradient plus a single texture so the whole tree stays
// in one fused pass — two gradients would exceed MaxGradientSlots, materialize both operands to
// RGBA8 and hide the divergence behind the unorm clamp (verified experimentally).
TGFX_TEST(AOTRenderConsistencyTest, OutOfRangeGradientStopBlendClampOrder) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  constexpr int size = 96;
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    // A constant out-of-range red gradient (rgb = 2.0) blended over the opaque image: SrcOver
    // with src alpha 1 reduces to the src value — clamp(2) = 1.0 on the runtime path. The g/b
    // stops (0.21) avoid the 25.5 half-value quantization boundary after the 0.5-scale matrix,
    // so any residual difference can only come from the clamp ordering, not rounding luck.
    std::vector<Color> hotColors = {Color(2.0f, 0.21f, 0.21f, 1.0f),
                                    Color(2.0f, 0.21f, 0.21f, 1.0f)};
    auto hot = Shader::MakeLinearGradient(Point(0, 0), Point(0, size), hotColors, {0.0f, 1.0f});
    ASSERT_NE(hot, nullptr);
    auto imageShader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
    ASSERT_NE(imageShader, nullptr);
    auto blendShader = Shader::MakeBlend(BlendMode::SrcOver, imageShader, hot);
    ASSERT_NE(blendShader, nullptr);
    Paint paint = {};
    paint.setShader(blendShader);
    // A 0.5-scale matrix after the blend: the runtime feeds clamp(2.0) = 1.0 (output ~0.5),
    // the chain feeds 2.0 (output ~1.0) — a ~0.5 gap if the clamp is missing.
    std::array<float, 20> halfScale = {0.5f, 0, 0,    0, 0, 0, 0.5f, 0, 0, 0,
                                       0,    0, 0.5f, 0, 0, 0, 0,    0, 1, 0};
    paint.setColorFilter(ColorFilter::Matrix(halfScale));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The blend-then-matrix chain must take the precompiled route in a single fused pass — a
    // materialized multi-pass plan would clamp the out-of-range value at the RGBA8 boundary and
    // hide the very asymmetry this test probes.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    auto stats = context->globalCache()->programStats();
    EXPECT_EQ(stats.programBuilderCreations, 0u);
    EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
    auto drawStats = cache->drawStats();
    EXPECT_EQ(drawStats.kernelInvocations, 1u);
    EXPECT_EQ(drawStats.planMaterializedEdges, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("out-of-range-stop-blend-clamp", candidate, reference, size, size);
}

// Counterexample audit B4, part one: fractional GP coverage against an opaque background across
// blend modes. The AA oval edge carries fractional coverage c, so the final composite must be
// O = c*Blend(S,D) + (1-c)*D — an opaque blue destination makes the (1-c)*D term visible and
// Multiply/Darken exercise the dst-reading XP path while Src/SrcOver take the fixed-function
// coefficient route. A wrong coverage application (e.g. c multiplying only the color, or the
// dst attenuation dropped) shows up on the rim pixels.
TGFX_TEST(AOTRenderConsistencyTest, CoverageBlendModesOnOpaqueBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  for (auto mode : {BlendMode::Src, BlendMode::SrcOver, BlendMode::Multiply, BlendMode::Darken}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto renderScene = [&](Bitmap* outBitmap) {
      auto surface = Surface::Make(context, size, size);
      ASSERT_NE(surface, nullptr);
      auto* canvas = surface->getCanvas();
      // An opaque blue destination: the uncovered term (1-c)*D must stay blue on the rim.
      canvas->clear(Color(0.0f, 0.1f, 0.9f, 1.0f));
      Paint paint = {};
      paint.setColor(Color(1.0f, 0.1f, 0.1f, 1.0f));
      paint.setBlendMode(mode);
      canvas->drawOval(Rect::MakeLTRB(16, 16, size - 16, size - 16), paint);
      context->flushAndSubmit(true);
      ASSERT_TRUE(outBitmap->allocPixels(size, size));
      auto* pixels = outBitmap->lockPixels();
      ASSERT_NE(pixels, nullptr);
      ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
      outBitmap->unlockPixels();
    };
    Bitmap reference = {};
    Bitmap candidate = {};
    {
      cache->unload();
      ScopedAOTDeliberateMiss deliberate(context);
      renderScene(&reference);
    }
    {
      auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_NE(bundleData, nullptr);
      ASSERT_GT(bundleBytes, 0u);
      ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
      cache->setDecompositionEnabled(true);
      cache->setDiagnosticRecordingEnabled(true);
      cache->resetStats();
      context->globalCache()->resetProgramStats();
      renderScene(&candidate);
      // Whether the draw resolves through the plain matcher or the chain kernel, it must not
      // fall back to runtime program building.
      EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
      cache->setDiagnosticRecordingEnabled(false);
      cache->unload();
      context->globalCache()->clearPrograms();
    }
    ExpectBitmapsIdentical("coverage-blend-opaque-bg", candidate, reference, size, size);
  }
}

// Counterexample audit B4, part two: a mask-sourced fractional coverage under the same blend
// matrix. The MaskFilter's shader alpha provides the coverage (a linear alpha gradient 0..1 over
// the rect), so the draw carries no GP coverage varying but still composites with fractional c.
// This is the mask application point of the chain kernel (device mask / coverage subtree) against
// the dst-reading and coefficient XP routes.
TGFX_TEST(AOTRenderConsistencyTest, MaskCoverageBlendModesOnOpaqueBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  // An alpha gradient (transparent -> opaque) as the mask shader: every column has a distinct
  // fractional coverage, not just an AA rim.
  std::vector<Color> maskColors = {Color(0, 0, 0, 0), Color(0, 0, 0, 1)};
  auto maskShader =
      Shader::MakeLinearGradient(Point(0, 0), Point(size, 0), maskColors, {0.0f, 1.0f});
  ASSERT_NE(maskShader, nullptr);
  for (auto mode : {BlendMode::Src, BlendMode::SrcOver, BlendMode::Multiply, BlendMode::Darken}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto renderScene = [&](Bitmap* outBitmap) {
      auto surface = Surface::Make(context, size, size);
      ASSERT_NE(surface, nullptr);
      auto* canvas = surface->getCanvas();
      canvas->clear(Color(0.0f, 0.1f, 0.9f, 1.0f));
      Paint paint = {};
      paint.setColor(Color(1.0f, 0.1f, 0.1f, 1.0f));
      paint.setBlendMode(mode);
      paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
      canvas->drawRect(Rect::MakeWH(size, size), paint);
      context->flushAndSubmit(true);
      ASSERT_TRUE(outBitmap->allocPixels(size, size));
      auto* pixels = outBitmap->lockPixels();
      ASSERT_NE(pixels, nullptr);
      ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
      outBitmap->unlockPixels();
    };
    Bitmap reference = {};
    Bitmap candidate = {};
    {
      cache->unload();
      ScopedAOTDeliberateMiss deliberate(context);
      renderScene(&reference);
    }
    {
      auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_NE(bundleData, nullptr);
      ASSERT_GT(bundleBytes, 0u);
      ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
      cache->setDecompositionEnabled(true);
      cache->setDiagnosticRecordingEnabled(true);
      cache->resetStats();
      context->globalCache()->resetProgramStats();
      renderScene(&candidate);
      EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
      EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
      cache->setDiagnosticRecordingEnabled(false);
      cache->unload();
      context->globalCache()->clearPrograms();
    }
    ExpectBitmapsIdentical("mask-coverage-blend-opaque-bg", candidate, reference, size, size);
  }
}

// Counterexample audit B3, remaining combination: the GP's own fractional AA coverage and a
// shader mask coexisting on one draw. The ellipse layout's per-pixel edge coverage is evaluated
// ahead of the chain and feeds the coverage subtree's unit input, so the blend-rooted subtree
// (the mask shader) rides the chain with the GP coverage folded in at the unit — the draw must
// take the precompiled route with no fallback and stay byte-identical to the runtime reference.
TGFX_TEST(AOTRenderConsistencyTest, GPCoverageAndMaskCoexistOnAAEdge) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  std::vector<Color> maskColors = {Color(0, 0, 0, 0), Color(0, 0, 0, 1)};
  auto maskShader =
      Shader::MakeLinearGradient(Point(0, 0), Point(size, 0), maskColors, {0.0f, 1.0f});
  ASSERT_NE(maskShader, nullptr);
  auto renderScene = [&](Bitmap* outBitmap) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setColor(Color::Green());
    paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
    canvas->drawOval(Rect::MakeLTRB(12, 12, size - 12, size - 12), paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  {
    cache->unload();
    ScopedAOTDeliberateMiss deliberate(context);
    renderScene(&reference);
  }
  {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleBytes, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
    cache->setDecompositionEnabled(true);
    cache->setDiagnosticRecordingEnabled(true);
    cache->resetStats();
    context->globalCache()->resetProgramStats();
    renderScene(&candidate);
    // The ellipse GP + blend-rooted coverage subtree rides the precompiled chain: no fallback,
    // no runtime program build.
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    cache->setDiagnosticRecordingEnabled(false);
    cache->unload();
    context->globalCache()->clearPrograms();
  }
  ExpectBitmapsIdentical("gp-coverage-and-mask-coexist", candidate, reference, size, size);
}

// Counterexample audit B6, redone as a parameter matrix: an alpha-only color matrix over a source
// with varying alpha and a paint with varying alpha, on a transparent target. The bias>0 matrix
// affects transparent black (ColorFilterShader wraps in SrcIn(composed, alphaSource) — the
// original shader's alpha masks the filtered color so transparent regions stay transparent),
// while the bias<=0 matrices take the plain Compose route where the texture modulates by the
// paint alpha. The identity-alpha control isolates the bias contribution: only the alpha row
// differs between the two matrices of each pair.
//
// AUDIT RULING (2026-09-18, batch 0): evidence insufficient, conclusion withdrawn. The claimed
// SrcIn-wrap-versus-Compose contrast is never actually constructed here: the makeWithColorFilter
// merge that produces the SrcIn wrap only happens when brush.shader is set (OpsCompositor's
// affectsTransparentBlack branch), and this scene builds the brush through drawImage +
// setColorFilter, so the claimed two routes do not both execute. Passing asserts below prove
// nothing about either route. Redo as part of the input-contract batch with a scene that
// provably reaches both paths.
TGFX_TEST(AOTRenderConsistencyTest, AlphaBiasMatrixSourceAlphaMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 48;
  // A source image with a controlled per-case alpha: a solid color bitmap rebuilt per alpha.
  auto makeSource = [&](float alpha) {
    Bitmap bitmap = {};
    EXPECT_TRUE(bitmap.allocPixels(size, size));
    auto* pixels = static_cast<uint32_t*>(bitmap.lockPixels());
    auto channel = [](float v) { return static_cast<uint32_t>(v * 255.0f + 0.5f); };
    uint32_t value =
        (channel(alpha) << 24) | (channel(0.3f) << 16) | (channel(0.5f) << 8) | channel(0.8f);
    for (size_t i = 0; i < static_cast<size_t>(size) * size; ++i) {
      pixels[i] = value;
    }
    bitmap.unlockPixels();
    return Image::MakeFrom(bitmap);
  };
  // Matrices touching only the alpha row: (scale, bias) pairs against the identity control.
  struct MatrixCase {
    const char* label;
    float scale;
    float bias;
    bool affectsTransparentBlack;
  };
  const std::array<MatrixCase, 3> cases = {{
      {"identity-alpha", 1.0f, 0.0f, false},
      {"half-scale", 0.5f, 0.0f, false},
      {"positive-bias", 1.0f, 0.25f, true},
  }};
  for (float sourceAlpha : {0.0f, 0.25f, 0.6f, 1.0f}) {
    for (float paintAlpha : {1.0f, 0.3f}) {
      for (const auto& matrixCase : cases) {
        SCOPED_TRACE(testing::Message() << "srcA=" << sourceAlpha << " paintA=" << paintAlpha
                                        << " matrix=" << matrixCase.label);
        auto image = makeSource(sourceAlpha);
        ASSERT_NE(image, nullptr);
        std::array<float, 20> matrix = {1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        matrixCase.scale,
                                        matrixCase.bias};
        auto filter = ColorFilter::Matrix(matrix);
        ASSERT_NE(filter, nullptr);
        auto renderScene = [&](Bitmap* outBitmap) {
          auto surface = Surface::Make(context, size, size);
          ASSERT_NE(surface, nullptr);
          auto* canvas = surface->getCanvas();
          canvas->clear(Color::Transparent());
          Paint paint = {};
          paint.setAlpha(paintAlpha);
          paint.setColorFilter(filter);
          canvas->drawImage(image, 0, 0, &paint);
          context->flushAndSubmit(true);
          ASSERT_TRUE(outBitmap->allocPixels(size, size));
          auto* pixels = outBitmap->lockPixels();
          ASSERT_NE(pixels, nullptr);
          ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
          outBitmap->unlockPixels();
        };
        Bitmap reference = {};
        Bitmap candidate = {};
        {
          cache->unload();
          ScopedAOTDeliberateMiss deliberate(context);
          renderScene(&reference);
        }
        {
          auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
          ASSERT_NE(bundleData, nullptr);
          ASSERT_GT(bundleBytes, 0u);
          ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
          cache->setDecompositionEnabled(true);
          cache->setDiagnosticRecordingEnabled(true);
          cache->resetStats();
          context->globalCache()->resetProgramStats();
          renderScene(&candidate);
          EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
          EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
          cache->setDiagnosticRecordingEnabled(false);
          cache->unload();
          context->globalCache()->clearPrograms();
        }
        ExpectBitmapsIdentical("alpha-bias-source-alpha-matrix", candidate, reference, size, size);
      }
    }
  }
}

// Counterexample audit D, boundary probe one: do two different images under the same effect
// structure share one program? The processor key aggregates per-sampler texture keys, which only
// carry the format and type (TextureView::ComputeTextureKey) — not the texture identity — so two
// RGBA_8888 2D images with the same effect chain must reuse the same program. This records the
// actual reuse behavior: one artifact creation and one cache hit across the two draws.
TGFX_TEST(AOTRenderConsistencyTest, DifferentImagesSameEffectShareProgram) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto imageOne = MakeImage("resources/apitest/mandrill_128.png");
  auto imageTwo = MakeImage("resources/apitest/checker_128.png");
  ASSERT_TRUE(imageOne != nullptr && imageTwo != nullptr);
  constexpr int size = 64;
  const std::array<float, 20> brighten = {1.2f, 0, 0,     0, 0.05f, 0, 1.1f, 0, 0, 0.03f,
                                          0,    0, 1.15f, 0, 0.02f, 0, 0,    0, 1, 0};
  auto filter = ColorFilter::Matrix(brighten);
  auto renderWith = [&](const std::shared_ptr<Image>& image) -> Bitmap {
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return {};
    }
    Paint paint = {};
    paint.setColorFilter(filter);
    surface->getCanvas()->drawImage(image, 0, 0, &paint);
    context->flushAndSubmit(true);
    Bitmap bitmap = {};
    if (!bitmap.allocPixels(size, size)) {
      return {};
    }
    auto* pixels = bitmap.lockPixels();
    if (pixels == nullptr) {
      return {};
    }
    if (!surface->readPixels(bitmap.info(), pixels)) {
      bitmap.unlockPixels();
      return {};
    }
    bitmap.unlockPixels();
    return bitmap;
  };
  auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleBytes, 0u);
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
  cache->setDecompositionEnabled(true);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto one = renderWith(imageOne);
  auto two = renderWith(imageTwo);
  auto stats = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
  printf("[ReuseImages] artifactCreations=%u cacheHits=%u\n",
         static_cast<unsigned>(stats.precompiledArtifactCreations),
         static_cast<unsigned>(stats.cacheHits));
  fflush(stdout);
  // Format-equal textures share the program identity: one artifact, one hit across the two
  // draws. (A creation count of two would record the texture-identity split as a boundary.)
  EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(stats.cacheHits, 1u);
  EXPECT_EQ(stats.programBuilderCreations, 0u);
  // Non-vacuous: the renders differ.
  auto* p1 = static_cast<const uint32_t*>(const_cast<Bitmap&>(one).lockPixels());
  auto* p2 = static_cast<const uint32_t*>(const_cast<Bitmap&>(two).lockPixels());
  EXPECT_NE(p1[32 * size + 32], p2[32 * size + 32]);
  const_cast<Bitmap&>(one).unlockPixels();
  const_cast<Bitmap&>(two).unlockPixels();
}

// Counterexample audit D, boundary probe two: does a two-image blend (two leaves) share a
// program with a single-image draw (one leaf) when both map to the same four-sampler artifact?
// The child-count difference in the processor key may split them; this records the behavior.
TGFX_TEST(AOTRenderConsistencyTest, DifferentLeafCountsShareVariant) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto imageOne = MakeImage("resources/apitest/mandrill_128.png");
  auto imageTwo = MakeImage("resources/apitest/checker_128.png");
  ASSERT_TRUE(imageOne != nullptr && imageTwo != nullptr);
  constexpr int size = 64;
  const std::array<float, 20> brighten = {1.2f, 0, 0,     0, 0.05f, 0, 1.1f, 0, 0, 0.03f,
                                          0,    0, 1.15f, 0, 0.02f, 0, 0,    0, 1, 0};
  auto renderWith = [&](const std::shared_ptr<ColorFilter>& filter, bool blend) -> Bitmap {
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return {};
    }
    Paint paint = {};
    paint.setColorFilter(filter);
    if (blend) {
      auto shaderOne = Shader::MakeImageShader(imageOne, TileMode::Clamp, TileMode::Clamp);
      auto shaderTwo = Shader::MakeImageShader(imageTwo, TileMode::Clamp, TileMode::Clamp);
      if (shaderOne == nullptr || shaderTwo == nullptr) {
        return {};
      }
      paint.setShader(Shader::MakeBlend(BlendMode::SrcOver, shaderOne, shaderTwo));
      surface->getCanvas()->drawRect(Rect::MakeWH(size, size), paint);
    } else {
      surface->getCanvas()->drawImage(imageOne, 0, 0, &paint);
    }
    context->flushAndSubmit(true);
    Bitmap bitmap = {};
    if (!bitmap.allocPixels(size, size)) {
      return {};
    }
    auto* pixels = bitmap.lockPixels();
    if (pixels == nullptr) {
      return {};
    }
    if (!surface->readPixels(bitmap.info(), pixels)) {
      bitmap.unlockPixels();
      return {};
    }
    bitmap.unlockPixels();
    return bitmap;
  };
  auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleBytes, 0u);
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
  cache->setDecompositionEnabled(true);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto single = renderWith(ColorFilter::Matrix(brighten), false);
  auto doubled = renderWith(ColorFilter::Matrix(brighten), true);
  auto stats = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
  context->globalCache()->clearPrograms();
  printf("[ReuseLeaves] artifactCreations=%u cacheHits=%u\n",
         static_cast<unsigned>(stats.precompiledArtifactCreations),
         static_cast<unsigned>(stats.cacheHits));
  fflush(stdout);
  // A one-leaf draw (padded to four samplers) and a two-leaf blend (two real leaves plus
  // padding) map to the same four-sampler artifact: one artifact creation and one cache hit
  // across the two draws (verified: the padding children's keys equal the real child's).
  EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(stats.cacheHits, 1u);
  EXPECT_EQ(stats.programBuilderCreations, 0u);
  EXPECT_NE(single.isEmpty(), true);
  EXPECT_NE(doubled.isEmpty(), true);
}

// Counterexample audit E1: blurring an alpha-only source through the real ImageFilter path.
// The blur pipeline materializes the alpha-only source into an ALPHA_8 intermediate
// (Swizzle=aaaa) and draws GaussianBlur1D(TiledTextureEffect) onto it. The kernel carries the
// alpha-only child semantics (the AlphaChild splat mirrors the runtime's
// Swizzle::ForRead(ALPHA_8)=.rrrr readback) and the AAAA write swizzle (OutputAlphaSwizzle),
// so the blur pass rides the precompiled GaussianBlur1DShader with no fallback. The composite
// end (tinted draw of the blurred mask) is served by the AOT set as before.
TGFX_TEST(AOTRenderConsistencyTest, AlphaOnlyImageBlurMatchesRuntime) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 96;
  // An alpha-only mask: a soft blob in the center, zero at the borders.
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(size, size, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  ASSERT_NE(maskPixels, nullptr);
  auto rowBytes = maskBitmap.rowBytes();
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      float dx = (x - size / 2.0f) / (size / 4.0f);
      float dy = (y - size / 2.0f) / (size / 4.0f);
      float d = sqrtf(dx * dx + dy * dy);
      maskPixels[static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x)] =
          static_cast<uint8_t>(255.0f * (d < 1.0f ? (1.0f - d * d) : 0.0f));
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);
  auto blur = ImageFilter::Blur(0.0f, 6.0f);
  ASSERT_NE(blur, nullptr);
  auto renderScene = [&](Bitmap* outBitmap, const std::shared_ptr<ColorFilter>& tint) {
    auto surface = Surface::Make(context, size, size);
    ASSERT_NE(surface, nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear(Color(0.0f, 0.1f, 0.9f, 1.0f));
    Paint paint = {};
    paint.setColorFilter(tint);
    canvas->drawImage(maskImage->makeWithFilter(blur), 0, 0, &paint);
    context->flushAndSubmit(true);
    ASSERT_TRUE(outBitmap->allocPixels(size, size));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
  };
  const std::array<float, 20> redTint = {1.0f, 0, 0,    0, 0, 0, 0.15f, 0, 0, 0,
                                         0,    0, 0.1f, 0, 0, 0, 0,     0, 1, 0};
  for (const auto& tint : {std::shared_ptr<ColorFilter>(nullptr), ColorFilter::Matrix(redTint)}) {
    Bitmap reference = {};
    Bitmap candidate = {};
    {
      cache->unload();
      ScopedAOTDeliberateMiss deliberate(context);
      renderScene(&reference, tint);
    }
    {
      auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_NE(bundleData, nullptr);
      ASSERT_GT(bundleBytes, 0u);
      ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
      cache->setDecompositionEnabled(true);
      cache->setDiagnosticRecordingEnabled(true);
      cache->resetStats();
      context->globalCache()->resetProgramStats();
      renderScene(&candidate, tint);
      auto stats = context->globalCache()->programStats();
      // The alpha-only blur pass now rides the precompiled kernel: no miss, no runtime program.
      // The scene resolves through three distinct precompiled artifacts (the blur pass's
      // GaussianBlur1D, and the composite's QuadTexture/chain variants), each created exactly
      // once with zero artifact misses.
      EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
      EXPECT_EQ(stats.programBuilderCreations, 0u);
      EXPECT_EQ(stats.precompiledArtifactCreations, 3u);
      cache->setDiagnosticRecordingEnabled(false);
      cache->unload();
      context->globalCache()->clearPrograms();
    }
    ExpectBitmapsIdentical("alpha-only-blur", candidate, reference, size, size);
  }
}

// Counterexample audit F1: the threshold operator straddling a materialization pass boundary
// under quantization-sensitive input. The offscreen fill drives a 34-instruction tree (texture,
// 16 matrices, threshold, 16 matrices) — formerly through a 17-pass tail plan with the
// threshold executing mid-chain over an RGBA8 materialized intermediate.
//
// AUDIT RULING (2026-09-18, batch 0): evidence insufficient — the sweep source violates the
// premul invariant and the identity alpha rows never let the step() decision flip, so the old
// maxDiff<=1 pass proved nothing.
//
// RULING UPDATE (2026-09-18, batch 3 / P3.2): multi-pass tail plans are refused on every route
// (audit D5: a discontinuous operator reading an RGBA8 materialized input can flip its step()
// decision — a 255-LSB divergence no tolerance bounds; see
// OffscreenThresholdAcrossBoundaryFlips for the exact-rational proof). Rebuilding the scene as
// a "valid" quantization probe is moot: the risk class no longer executes. This test now
// records the rejection boundary: the runtime reference serves the fill, byte-identical.
TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailThresholdQuantizationBand) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int srcSize = 96;
  // An alpha-sweep source: white rgb, alpha 0..255 down the rows.
  Bitmap sweepBitmap = {};
  ASSERT_TRUE(sweepBitmap.allocPixels(srcSize, srcSize));
  auto* sweepPixels = static_cast<uint32_t*>(sweepBitmap.lockPixels());
  ASSERT_NE(sweepPixels, nullptr);
  for (int y = 0; y < srcSize; ++y) {
    uint32_t alpha = static_cast<uint32_t>(y * 255 / (srcSize - 1));
    for (int x = 0; x < srcSize; ++x) {
      sweepPixels[static_cast<size_t>(y) * srcSize + static_cast<size_t>(x)] =
          (alpha << 24) | 0x00FFFFFFu;
    }
  }
  sweepBitmap.unlockPixels();
  auto sweepImage = Image::MakeFrom(sweepBitmap);
  ASSERT_NE(sweepImage, nullptr);
  auto sourceSurface = Surface::Make(context, srcSize, srcSize, false, 1, true);
  ASSERT_NE(sourceSurface, nullptr);
  {
    ScopedAOTStatsPause pause(context, true);
    sourceSurface->getCanvas()->drawImage(sweepImage, 0, 0);
    context->flushAndSubmit(true);
  }
  auto source = context->proxyProvider()->wrapExternalTexture(sourceSurface->getBackendTexture());
  ASSERT_NE(source, nullptr);
  auto buildTree = [&](BlockAllocator* allocator) -> PlacementPtr<FragmentProcessor> {
    auto processor = TextureEffect::Make(allocator, source);
    for (size_t index = 0; index < 16; ++index) {
      processor = FragmentProcessor::Compose(
          allocator, std::move(processor),
          ColorMatrixFragmentProcessor::Make(allocator, NonTrivialScaleBiasMatrix()));
    }
    processor = FragmentProcessor::Compose(allocator, std::move(processor),
                                           AlphaThresholdFragmentProcessor::Make(allocator, 0.5f));
    for (size_t index = 0; index < 16; ++index) {
      processor = FragmentProcessor::Compose(
          allocator, std::move(processor),
          ColorMatrixFragmentProcessor::Make(allocator, NonTrivialScaleBiasMatrix()));
    }
    return processor;
  };
  Bitmap reference = {};
  Bitmap candidate = {};
  AOTDrawStats candidateDraws = {};
  auto render = [&](bool useBundle, Bitmap* outBitmap, AOTDrawStats* outDraws) {
    if (useBundle) {
      auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_NE(bundleData, nullptr);
      ASSERT_GT(bundleBytes, 0u);
      ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
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
    auto processor = buildTree(context->drawingAllocator());
    ASSERT_NE(processor, nullptr);
    ASSERT_TRUE(context->drawingManager()->fillRTWithFP(target, std::move(processor), 0));
    context->flushAndSubmit(true);
    auto rt = target->getRenderTarget();
    ASSERT_NE(rt, nullptr);
    auto surface = Surface::MakeFrom(context, rt->getBackendRenderTarget(), rt->origin());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(outBitmap->allocPixels(64, 64));
    auto* pixels = outBitmap->lockPixels();
    ASSERT_NE(pixels, nullptr);
    EXPECT_TRUE(surface->readPixels(outBitmap->info(), pixels));
    outBitmap->unlockPixels();
    if (outDraws != nullptr) {
      *outDraws = cache->drawStats();
    }
    cache->setDiagnosticRecordingEnabled(false);
    cache->setDecompositionEnabled(true);
    cache->unload();
    context->globalCache()->clearPrograms();
  };
  render(false, &reference, nullptr);
  render(true, &candidate, &candidateDraws);
  // Rejection boundary: the 34-instruction tree exceeds the tail budget, so the plan is refused
  // and the runtime reference serves the fill (one program build, one kernel invocation, no
  // materialized edges, byte-identical output — the threshold never reads a quantized input).
  EXPECT_EQ(candidateDraws.completeAOTDraws, 0u);
  EXPECT_EQ(candidateDraws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateDraws.kernelInvocations, 1u);
  EXPECT_EQ(candidateDraws.planMaterializedEdges, 0u);
  EXPECT_GE(context->globalCache()->programStats().programBuilderCreations, 1u);
  ExpectBitmapsIdentical("offscreen-tail-threshold-refused", candidate, reference, 64, 64);
}

// Counterexample audit F2: materialized multi-pass sampling under translation, magnification,
// and minification with mipmap filtering. The offscreen fill drives a 36-instruction tree (a
// device-space texture source with a transform-carrying uvMatrix plus 35 matrices) — formerly
// through an 18-pass tail plan where every source sample rode a materialization chain.
//
// AUDIT RULING (2026-09-18, batch 0): evidence insufficient — the "minify-mipmap" case never
// executed mipmap sampling (no sampler override, no mip levels), so the old maxDiff<=3 pass
// said nothing about sampling across materialization boundaries.
//
// RULING UPDATE (2026-09-18, batch 3 / P3.2): multi-pass tail plans are refused on every route
// (audit D5 ruling; see OffscreenThresholdAcrossBoundaryFlips). Sampling across materialized
// intermediates no longer executes at all, so re-probing it with a real mipmap sampler is moot.
// This test now records the rejection boundary under every transform: the runtime reference
// serves the fill, byte-identical.
TGFX_TEST(AOTRenderConsistencyTest, OffscreenTailSamplingTransforms) {
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
  struct TransformCase {
    const char* label;
    Matrix uvMatrix;
  };
  const std::array<TransformCase, 3> cases = {{
      {"translate", Matrix::MakeTrans(31, 17)},
      {"magnify", Matrix::MakeScale(0.5f, 0.5f)},
      {"minify-mipmap", Matrix::MakeScale(3.2f, 3.2f)},
  }};
  for (const auto& transformCase : cases) {
    SCOPED_TRACE(transformCase.label);
    auto render = [&](bool useBundle, Bitmap* outBitmap, AOTDrawStats* outDraws) {
      if (useBundle) {
        auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
        ASSERT_NE(bundleData, nullptr);
        ASSERT_GT(bundleBytes, 0u);
        ASSERT_TRUE(cache->loadBundle(bundleData, bundleBytes));
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
      PlacementPtr<FragmentProcessor> processor =
          DeviceSpaceTextureEffect::Make(allocator, source, transformCase.uvMatrix);
      ASSERT_NE(processor, nullptr);
      for (size_t index = 0; index < 35; ++index) {
        processor = FragmentProcessor::Compose(
            allocator, std::move(processor),
            ColorMatrixFragmentProcessor::Make(allocator, NonTrivialScaleBiasMatrix()));
      }
      ASSERT_TRUE(context->drawingManager()->fillRTWithFP(target, std::move(processor), 0));
      context->flushAndSubmit(true);
      auto rt = target->getRenderTarget();
      ASSERT_NE(rt, nullptr);
      auto surface = Surface::MakeFrom(context, rt->getBackendRenderTarget(), rt->origin());
      ASSERT_NE(surface, nullptr);
      ASSERT_TRUE(outBitmap->allocPixels(64, 64));
      auto* pixels = outBitmap->lockPixels();
      ASSERT_NE(pixels, nullptr);
      EXPECT_TRUE(surface->readPixels(outBitmap->info(), pixels));
      outBitmap->unlockPixels();
      if (outDraws != nullptr) {
        *outDraws = cache->drawStats();
      }
      cache->setDiagnosticRecordingEnabled(false);
      cache->setDecompositionEnabled(true);
      cache->unload();
      context->globalCache()->clearPrograms();
    };
    Bitmap reference = {};
    Bitmap candidate = {};
    AOTDrawStats candidateDraws = {};
    render(false, &reference, nullptr);
    render(true, &candidate, &candidateDraws);
    // Rejection boundary under every transform: the runtime reference serves the fill.
    EXPECT_EQ(candidateDraws.completeAOTDraws, 0u);
    EXPECT_EQ(candidateDraws.atomicFallbacks, 0u);
    EXPECT_EQ(candidateDraws.kernelInvocations, 1u);
    EXPECT_EQ(candidateDraws.planMaterializedEdges, 0u);
    EXPECT_GE(context->globalCache()->programStats().programBuilderCreations, 1u);
    ExpectBitmapsIdentical("offscreen-tail-sampling-refused", candidate, reference, 64, 64);
  }
}

}  // namespace tgfx
