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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/images/SubsetImage.h"
#include "gpu/SamplingArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Image.h"
#include "utils/ContextScope.h"
#include "utils/TestUtils.h"

namespace tgfx {
/**
 * A subset draw whose rect starts inside the subset bounds and overflows them renders the
 * cropped area into a texture at the draw scale. The uv matrix therefore has to subtract the
 * crop origin before applying that scale: the crop origin must map to texel (0, 0) and the far
 * corner of the crop to the texture size, no matter what the draw scale is.
 */
TGFX_TEST(SubsetImageTest, CropUVMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  Bitmap bitmap(96, 64, false, false, ColorSpace::SRGB());
  ASSERT_FALSE(bitmap.isEmpty());
  auto source = Image::MakeFrom(bitmap);
  ASSERT_TRUE(source != nullptr);
  auto subsetBounds = Rect::MakeXYWH(16, 8, 64, 48);
  auto subset = source->makeSubset(subsetBounds);
  ASSERT_TRUE(subset != nullptr);
  // The draw rect starts inside the subset bounds and overflows them on both axes.
  auto drawRect = Rect::MakeXYWH(8, 4, 64, 48);
  constexpr float DRAW_SCALE = 0.5f;
  FPArgs args(context, 0, drawRect, DRAW_SCALE);
  auto processor = subset->asFragmentProcessor(args, SamplingArgs(), nullptr);
  ASSERT_NE(processor, nullptr);
  ASSERT_EQ(processor->numCoordTransforms(), 1u);
  auto coordTransform = processor->coordTransform(0);
  ASSERT_NE(coordTransform, nullptr);
  auto& matrix = coordTransform->matrix;
  auto textureOrigin = matrix.mapXY(drawRect.left, drawRect.top);
  EXPECT_NEAR(textureOrigin.x, 0.0f, 0.001f) << "the crop origin has to sample texel (0, 0)";
  EXPECT_NEAR(textureOrigin.y, 0.0f, 0.001f) << "the crop origin has to sample texel (0, 0)";
  auto textureCorner = matrix.mapXY(subsetBounds.width(), subsetBounds.height());
  EXPECT_NEAR(textureCorner.x, (subsetBounds.width() - drawRect.left) * DRAW_SCALE, 0.001f);
  EXPECT_NEAR(textureCorner.y, (subsetBounds.height() - drawRect.top) * DRAW_SCALE, 0.001f);
}

}  // namespace tgfx
