/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <vector>
#include "tgfx/core/Mask.h"
#include "tgfx/gpu/Surface.h"
#include "tgfx/opengl/GLDevice.h"
#include "utils/TestUtils.h"
#include "vectors/freetype/FTMask.h"

namespace tgfx {
TGFX_TEST(MaskTest, Rasterize) {
  Path path = {};
  path.addRect(100, 100, 400, 400);
  path.addRoundRect(Rect::MakeLTRB(150, 150, 350, 350), 30, 20, true);
  path.addOval(Rect::MakeLTRB(200, 200, 300, 300));
  // 501*501 is for GL_UNPACK_ALIGNMENT testing
  auto mask = Mask::Make(501, 501);
  ASSERT_TRUE(mask != nullptr);
  auto matrix = Matrix::MakeTrans(50, 50);
  mask->setMatrix(matrix);
  mask->fillPath(path);
  auto maskBuffer = std::static_pointer_cast<PixelBuffer>(mask->makeBuffer());
  EXPECT_TRUE(Baseline::Compare(maskBuffer, "MaskTest/rasterize_path"));

  auto device = GLDevice::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto image = Image::MakeFrom(mask->makeBuffer());
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, mask->width(), mask->height());
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  Bitmap bitmap(mask->width(), mask->height(), true, false);
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  pixmap.clear();
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  ASSERT_TRUE(result);
  device->unlock();
  EXPECT_TRUE(Baseline::Compare(pixmap, "MaskTest/rasterize_path_texture"));

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasColor());
  auto glyphID = typeface->getGlyphID("👻");
  ASSERT_TRUE(glyphID != 0);
  Font font = {};
  font.setSize(300);
  font.setTypeface(typeface);
  font.setFauxItalic(true);
  font.setFauxBold(true);
  auto buffer = font.getGlyphImage(glyphID, &matrix);
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_TRUE(fabsf(matrix.getScaleX() - 2.75229359f) < FLT_EPSILON);
  EXPECT_TRUE(fabsf(matrix.getSkewX() + 0.550458729f) < FLT_EPSILON);
  EXPECT_TRUE(
      Baseline::Compare(std::static_pointer_cast<PixelBuffer>(buffer), "MaskTest/rasterize_emoji"));
}
}  // namespace tgfx
