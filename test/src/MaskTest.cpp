/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "core/PathRasterizer.h"
#include "core/images/BufferImage.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(PathRasterizerTest, Rasterize) {
  Path path = {};
  path.addRect(100, 100, 400, 400);
  path.addRoundRect(Rect::MakeLTRB(150, 150, 350, 350), 30, 20, true);
  path.addOval(Rect::MakeLTRB(200, 200, 300, 300));
  auto matrix = Matrix::MakeTrans(50, 50);
  path.transform(matrix);
  // 501*501 is for GL_UNPACK_ALIGNMENT testing
  auto rasterizer = PathRasterizer::MakeFrom(501, 501, path, true);
  ASSERT_TRUE(rasterizer != nullptr);
  auto maskBuffer = std::static_pointer_cast<PixelBuffer>(rasterizer->makeBuffer());
  EXPECT_TRUE(Baseline::Compare(maskBuffer, "MaskTest/rasterize_path"));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = Image::MakeFrom(rasterizer->makeBuffer());
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, rasterizer->width(), rasterizer->height());
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  Bitmap bitmap(rasterizer->width(), rasterizer->height(), true, false, surface->gamutColorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  pixmap.clear();
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  EXPECT_TRUE(result);
  EXPECT_TRUE(
      Baseline::Compare(pixmap, "MaskTest/rasterize_path_texture", surface->gamutColorSpace()));

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
  auto glyphCodec = font.getImage(glyphID, nullptr, &matrix);
  auto glyphImage = Image::MakeFrom(glyphCodec);
  ASSERT_TRUE(glyphImage != nullptr);
  EXPECT_TRUE(fabsf(matrix.getScaleX() - 2.75229359f) < FLT_EPSILON);
  EXPECT_TRUE(fabsf(matrix.getSkewX() + 0.550458729f) < FLT_EPSILON);
  surface = Surface::Make(context, glyphImage->width(), glyphImage->height());
  canvas = surface->getCanvas();
  canvas->drawImage(glyphImage);
  EXPECT_TRUE(Baseline::Compare(surface, "MaskTest/rasterize_emoji"));
}
}  // namespace tgfx
