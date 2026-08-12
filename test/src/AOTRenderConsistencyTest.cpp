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

#include <cstring>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "utils/TestUtils.h"

namespace tgfx {

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

TGFX_TEST(AOTRenderConsistencyTest, TwoChildXferBlendFold) {
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
}

// Renders an antialiased circle (EllipseGeometryProcessor) under an optional clip into outBitmap.
// clipMode: 0 = no clip, 1 = antialiased clipRect (AARectEffect coverage), 2 = antialiased
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
// coverage FP (AARectEffect for a rect clip, a device-space mask texture for a non-rect clip). The
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

// Three pointwise operators are packed into two fixed-slot passes: the first pass applies Matrix +
// Luma, and the terminal device-space pass applies the final Matrix. This exercises both source
// coordinate domains while requiring only one RGBA8 intermediate.
TGFX_TEST(AOTRenderConsistencyTest, LinearChainMultiPass) {
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
  EXPECT_GE(candidateStats.hits, 2u);
  EXPECT_GE(candidateStats.pipelines, 2u);
  EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 2u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  auto intermediateBytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 2u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 1u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 1u);
  EXPECT_EQ(candidateStats.draws.renderTargetSwitches, 1u);
  EXPECT_EQ(candidateStats.draws.intermediateReadBytes, intermediateBytes);
  EXPECT_EQ(candidateStats.draws.intermediateWriteBytes, intermediateBytes);
  EXPECT_EQ(candidateStats.draws.peakTemporaryBytes, intermediateBytes);
  ExpectBitmapsIdentical("linear-chain-matrix-luma-matrix", candidate, reference, width, height);
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

// An anti-aliased, non-pixel-aligned rect clip produces an AARectEffect coverage FP. It must fold
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

// Encoded images use GL_TEXTURE_RECTANGLE on macOS. The precompiled textured kernel accepts only
// TextureType::TwoD, so strict preparation must reject the plan before any pass executes and render
// the untouched original draw through the runtime fallback.
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
  EXPECT_GT(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.programs.precompiledArtifactCreations, 0u);
  EXPECT_GE(candidateStats.programs.programBuilderCreations, 1u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 0u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 1u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
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
