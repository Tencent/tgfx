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

#include <algorithm>
#include <cmath>
#include "core/PathRasterizer.h"
#include "core/utils/FauxBoldScale.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

#ifdef TGFX_USE_FREETYPE
#include "core/vectors/freetype/FTScalerContext.h"

namespace tgfx {

static std::shared_ptr<Typeface> MakeFauxBoldTestTypeface() {
  return Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
}

TGFX_TEST_PRIVATE(FauxBoldTest, OutlineMatchesUnhintedStroke) {
  auto typeface = MakeFauxBoldTestTypeface();
  ASSERT_NE(typeface, nullptr);
  ASSERT_FALSE(typeface->hasColor());
  for (float size : {8.0f, 9.0f, 12.0f, 28.0f, 36.0f, 72.0f, 160.0f}) {
    for (bool italic : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "size=" << size << " italic=" << italic);
      FTScalerContext reference(typeface, size);
      reference.loadGlyphFlags |= FT_LOAD_NO_HINTING;
      Font font(typeface, size);
      font.setFauxBold(true);
      font.setFauxItalic(italic);
      for (Unichar character : {'H', 'A', 'O', 'g'}) {
        SCOPED_TRACE(::testing::Message() << "glyph=" << character);
        auto glyphID = font.getGlyphID(character);
        ASSERT_NE(glyphID, 0);
        Path expected;
        ASSERT_TRUE(reference.generatePath(glyphID, false, italic, &expected));
        auto band = expected;
        Stroke stroke(size * FauxBoldScale(size), LineCap::Butt, LineJoin::Miter);
        ASSERT_TRUE(stroke.applyToPath(&band));
        ASSERT_TRUE(expected.addPath(band, PathOp::Union));
        Path actual;
        ASSERT_TRUE(font.getPath(glyphID, &actual));
        ASSERT_FALSE(actual.isEmpty());
        ASSERT_TRUE(actual.addPath(expected, PathOp::XOR));
        EXPECT_TRUE(actual.isEmpty());
      }
    }
  }
}

TGFX_TEST(FauxBoldTest, BoundsAndImageUseTheSameOutline) {
  auto typeface = MakeFauxBoldTestTypeface();
  ASSERT_NE(typeface, nullptr);
  for (float size : {12.0f, 36.0f, 72.0f, 160.0f}) {
    Font font(typeface, size);
    Font bold = font;
    bold.setFauxBold(true);
    for (Unichar character : {'A', 'O', 'g'}) {
      auto glyphID = font.getGlyphID(character);
      ASSERT_NE(glyphID, 0);
      Path path;
      ASSERT_TRUE(bold.getPath(glyphID, &path));
      auto bounds = path.getBounds();
      bounds.roundOut();
      EXPECT_EQ(bold.getBounds(glyphID), bounds);
      EXPECT_EQ(bold.getImage(glyphID, nullptr, nullptr), nullptr);
      EXPECT_NE(font.getImage(glyphID, nullptr, nullptr), nullptr);
      EXPECT_EQ(bold.getAdvance(glyphID), font.getAdvance(glyphID));
    }
    auto space = font.getGlyphID(' ');
    ASSERT_NE(space, 0);
    Path path;
    ASSERT_TRUE(bold.getPath(space, &path));
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(bold.getBounds(space).isEmpty());
  }
}

TGFX_TEST(FauxBoldTest, SwitchingBoldDoesNotChangeRegular) {
  auto typeface = MakeFauxBoldTestTypeface();
  ASSERT_NE(typeface, nullptr);
  Font font(typeface, 12.0f);
  auto glyphID = font.getGlyphID('A');
  ASSERT_NE(glyphID, 0);
  Path before;
  ASSERT_TRUE(font.getPath(glyphID, &before));
  auto beforeBounds = font.getBounds(glyphID);
  auto beforeAdvance = font.getAdvance(glyphID);
  font.setFauxBold(true);
  Path bold;
  ASSERT_TRUE(font.getPath(glyphID, &bold));
  EXPECT_FALSE(bold.isEmpty());
  font.setFauxBold(false);
  Path after;
  ASSERT_TRUE(font.getPath(glyphID, &after));
  EXPECT_EQ(before, after);
  EXPECT_EQ(beforeBounds, font.getBounds(glyphID));
  EXPECT_EQ(beforeAdvance, font.getAdvance(glyphID));
  EXPECT_NE(font.getImage(glyphID, nullptr, nullptr), nullptr);
}

TGFX_TEST(FauxBoldTest, UsesRawAntialiasedCoverage) {
  auto typeface = MakeFauxBoldTestTypeface();
  ASSERT_NE(typeface, nullptr);
  Font font(typeface, 72.0f);
  font.setFauxBold(true);
  auto glyphID = font.getGlyphID('A');
  ASSERT_NE(glyphID, 0);
  auto bounds = font.getBounds(glyphID);
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  ASSERT_GT(width, 0);
  ASSERT_GT(height, 0);
  auto shape = Shape::MakeFrom(font, glyphID);
  ASSERT_NE(shape, nullptr);
  shape = Shape::ApplyMatrix(shape, Matrix::MakeTrans(-bounds.left, -bounds.top));
  auto rawRasterizer = PathRasterizer::MakeFrom(width, height, shape, true, false);
  auto gammaRasterizer = PathRasterizer::MakeFrom(width, height, shape, true, true);
  ASSERT_NE(rawRasterizer, nullptr);
  ASSERT_NE(gammaRasterizer, nullptr);
  Bitmap raw(width, height, true, false);
  Bitmap corrected(width, height, true, false);
  Pixmap rawPixels(raw);
  Pixmap correctedPixels(corrected);
  ASSERT_TRUE(rawRasterizer->readPixels(rawPixels.info(), rawPixels.writablePixels()));
  ASSERT_TRUE(
      gammaRasterizer->readPixels(correctedPixels.info(), correctedPixels.writablePixels()));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 160, 160, true);
  ASSERT_NE(surface, nullptr);
  Point position = Point::Make(40, 100);
  surface->getCanvas()->drawGlyphs(&glyphID, &position, 1, font, Paint());
  Bitmap actual(160, 160, true, false);
  Pixmap actualPixels(actual);
  ASSERT_TRUE(surface->readPixels(actualPixels.info(), actualPixels.writablePixels()));

  const auto* rawData = static_cast<const uint8_t*>(rawPixels.pixels());
  const auto* correctedData = static_cast<const uint8_t*>(correctedPixels.pixels());
  const auto* actualData = static_cast<const uint8_t*>(actualPixels.pixels());
  int maxDifference = 0;
  int rawCoverage = 0;
  int correctedCoverage = 0;
  auto left = static_cast<int>(position.x + bounds.left);
  auto top = static_cast<int>(position.y + bounds.top);
  ASSERT_GE(left, 0);
  ASSERT_GE(top, 0);
  ASSERT_LE(left + width, actual.width());
  ASSERT_LE(top + height, actual.height());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto expected =
          rawData[static_cast<size_t>(y) * rawPixels.rowBytes() + static_cast<size_t>(x)];
      auto rendered = actualData[static_cast<size_t>(y + top) * actualPixels.rowBytes() +
                                 static_cast<size_t>(x + left)];
      maxDifference = std::max(maxDifference, std::abs(static_cast<int>(expected) - rendered));
      rawCoverage += expected;
      correctedCoverage += correctedData[static_cast<size_t>(y) * correctedPixels.rowBytes() +
                                         static_cast<size_t>(x)];
    }
  }
  EXPECT_LE(maxDifference, 1);
  EXPECT_GT(correctedCoverage, rawCoverage);
}

TGFX_TEST(FauxBoldTest, ColorGlyphKeepsItsBitmap) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_NE(typeface, nullptr);
  ASSERT_TRUE(typeface->hasColor());
  Font font(typeface, 64.0f);
  auto glyphID = font.getGlyphID(0x1F600);
  ASSERT_NE(glyphID, 0);
  auto regular = font.getImage(glyphID, nullptr, nullptr);
  ASSERT_NE(regular, nullptr);
  font.setFauxBold(true);
  auto bold = font.getImage(glyphID, nullptr, nullptr);
  ASSERT_NE(bold, nullptr);
  EXPECT_EQ(regular->width(), bold->width());
  EXPECT_EQ(regular->height(), bold->height());
}

}  // namespace tgfx
#endif
