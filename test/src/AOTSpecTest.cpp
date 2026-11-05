/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include <memory>
#include "gpu/GlobalCache.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "utils/AOTToleranceCompare.h"
#include "utils/ContextScope.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

std::shared_ptr<Image> MakeGradientImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(48, 48)) {
    return nullptr;
  }
  auto* pixels = static_cast<uint8_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  auto rowBytes = bitmap.rowBytes();
  for (int y = 0; y < 48; ++y) {
    auto* row = pixels + static_cast<size_t>(y) * rowBytes;
    for (int x = 0; x < 48; ++x) {
      row[x * 4 + 0] = static_cast<uint8_t>(x * 5);
      row[x * 4 + 1] = static_cast<uint8_t>(y * 5);
      row[x * 4 + 2] = static_cast<uint8_t>((x + y) * 2);
      row[x * 4 + 3] = 255;
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

// A chain-1-op shape: a drawn image whose paint carries one color matrix. The decomposition route
// fuses it into the PointwiseChain kernel, so this draw is the specialization path's unit scene.
void DrawColorMatrixScene(Canvas* canvas, const std::shared_ptr<Image>& image) {
  canvas->clear(Color::Transparent());
  Paint paint = {};
  std::array<float, 20> matrix = {0.8f, 0.1f, 0.1f, 0.0f, 0.05f,  //
                                  0.1f, 0.7f, 0.2f, 0.0f, 0.03f,  //
                                  0.1f, 0.2f, 0.6f, 0.0f, 0.02f,  //
                                  0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  paint.setColorFilter(ColorFilter::Matrix(matrix));
  canvas->drawImage(image, 8, 8, &paint);
}

// A second chain shape of the SAME variant as the matrix scene (quad GP, one texture leaf,
// default XP): its ops and registers differ, so the specialization bytes differ while every
// identity the un-specialized key carries stays the same. This is the shape pair the
// spec-less key used to collapse onto one program.
void DrawLumaScene(Canvas* canvas, const std::shared_ptr<Image>& image) {
  canvas->clear(Color::Transparent());
  Paint paint = {};
  paint.setColorFilter(ColorFilter::Luma());
  paint.setAlpha(0.85f);
  canvas->drawImage(image, 8, 8, &paint);
}

Bitmap RenderScene(Context* context, const std::shared_ptr<Image>& image, bool matrixScene) {
  Bitmap bitmap = {};
  auto surface = Surface::Make(context, 64, 64);
  if (surface == nullptr) {
    return bitmap;
  }
  auto* canvas = surface->getCanvas();
  if (matrixScene) {
    DrawColorMatrixScene(canvas, image);
  } else {
    DrawLumaScene(canvas, image);
  }
  context->flushAndSubmit(true);
  if (!bitmap.allocPixels(64, 64)) {
    return bitmap;
  }
  auto pixels = bitmap.lockPixels();
  if (pixels == nullptr || !surface->readPixels(bitmap.info(), pixels)) {
    bitmap.unlockPixels();
    return Bitmap();
  }
  bitmap.unlockPixels();
  return bitmap;
}

// Same-pipeline-source agreement: like the L2 audit, allow one LSB of compilation rounding, zero
// structural difference. Anything larger means the specialized fold changed the math.
const AOTToleranceSpec OneLsbTolerance() {
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  return spec;
}

}  // namespace

// The specialization's ground contract: with the values injected and the executor's guards
// folded, the specialized program must produce the pixels the generic interpreter produces for
// the same draw. The dedicated kernels stay off so the chain family serves the scene (a small
// canvas keeps the decomposition router on the plain route, where the kernel table's dedicated
// families would otherwise accept the draw first).
TGFX_TEST(AOTSpecTest, SpecializationMatchesGenericPixels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeGradientImage();
  ASSERT_NE(image, nullptr);
  auto* cache = context->precompiledShaderCache();
  cache->setDedicatedKernelsEnabled(false);

  cache->setSpecializationEnabled(false);
  auto generic = RenderScene(context, image, true);

  cache->setSpecializationEnabled(true);
  // The generic program of the same identity is already cached; only a cleared cache routes
  // this second render through the specialized creation.
  context->globalCache()->clearPrograms();
  auto specialized = RenderScene(context, image, true);
  cache->setSpecializationEnabled(false);

  ASSERT_FALSE(generic.isEmpty());
  ASSERT_FALSE(specialized.isEmpty());
  Pixmap genericPixmap(generic);
  Pixmap specializedPixmap(specialized);
  auto result = AOTToleranceCompare::Compare(genericPixmap, specializedPixmap, OneLsbTolerance());
  EXPECT_TRUE(result.passed) << "specialized pixels differ from the generic form: maxChannelDiff="
                             << result.maxChannelDiff << " diffPixels=" << result.diffPixelCount
                             << "/" << result.totalPixelCount;
  cache->setDedicatedKernelsEnabled(true);
}

// Two chain shapes of one variant: the specialization bytes must reach the program key, or the
// second shape silently reuses the first shape's specialized program — the collision this test
// pins. Each scene is compared against its own generic render.
TGFX_TEST(AOTSpecTest, SpecializationSeparatesChainShapes) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeGradientImage();
  ASSERT_NE(image, nullptr);
  auto* cache = context->precompiledShaderCache();
  cache->setDedicatedKernelsEnabled(false);

  cache->setSpecializationEnabled(false);
  auto genericMatrix = RenderScene(context, image, true);
  auto genericLuma = RenderScene(context, image, false);

  cache->setSpecializationEnabled(true);
  context->globalCache()->clearPrograms();
  auto specializedMatrix = RenderScene(context, image, true);
  context->globalCache()->clearPrograms();
  auto specializedLuma = RenderScene(context, image, false);
  cache->setSpecializationEnabled(false);

  ASSERT_FALSE(genericMatrix.isEmpty());
  ASSERT_FALSE(genericLuma.isEmpty());
  ASSERT_FALSE(specializedMatrix.isEmpty());
  ASSERT_FALSE(specializedLuma.isEmpty());

  Pixmap genericMatrixPixmap(genericMatrix);
  Pixmap specializedMatrixPixmap(specializedMatrix);
  auto matrixResult =
      AOTToleranceCompare::Compare(genericMatrixPixmap, specializedMatrixPixmap, OneLsbTolerance());
  EXPECT_TRUE(matrixResult.passed)
      << "the matrix shape's specialized pixels differ: maxChannelDiff="
      << matrixResult.maxChannelDiff << " diffPixels=" << matrixResult.diffPixelCount << "/"
      << matrixResult.totalPixelCount;

  Pixmap genericLumaPixmap(genericLuma);
  Pixmap specializedLumaPixmap(specializedLuma);
  auto lumaResult =
      AOTToleranceCompare::Compare(genericLumaPixmap, specializedLumaPixmap, OneLsbTolerance());
  EXPECT_TRUE(lumaResult.passed)
      << "the luma shape's specialized pixels differ (or reuse the matrix shape's program): "
         "maxChannelDiff="
      << lumaResult.maxChannelDiff << " diffPixels=" << lumaResult.diffPixelCount << "/"
      << lumaResult.totalPixelCount;
  cache->setDedicatedKernelsEnabled(true);
}

}  // namespace tgfx
