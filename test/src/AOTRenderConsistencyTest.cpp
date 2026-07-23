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
#include "base/TGFXTest.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
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

// Color-space conversions into a Display-P3 surface: exercises ColorSpaceXformShader and
// TexturedColorSpaceXformShader, whose seven pipeline steps are now selected by the CSFlags runtime
// uniform. A precompiled variant that reads a flag differently from the runtime codegen would show
// up as a byte mismatch here.
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

}  // namespace tgfx
