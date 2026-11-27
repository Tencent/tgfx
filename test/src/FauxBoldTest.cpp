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
#include "tgfx/core/CustomTypeface.h"
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

// The reference scaler context is inspected through TGFX_TEST_ACCESS_PRIVATE: the test needs the
// scaler's own glyph flags to build an unhinted expectation. MSVC builds do not enable
// -fno-access-control, so the whole test has to be excluded there instead of just unregistered.
#ifdef TGFX_TEST_ACCESS_PRIVATE
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
#endif

TGFX_TEST(FauxBoldTest, FauxBoldBoundsContainTheDrawnOutline) {
  auto typeface = MakeFauxBoldTestTypeface();
  ASSERT_NE(typeface, nullptr);
  for (float size : {12.0f, 36.0f, 72.0f, 160.0f}) {
    for (bool italic : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "size=" << size << " italic=" << italic);
      Font font(typeface, size);
      font.setFauxItalic(italic);
      Font bold = font;
      bold.setFauxBold(true);
      // 'A' and 'W' carry sharp corners where a miter join reaches furthest.
      for (Unichar character : {'A', 'O', 'g', 'W'}) {
        SCOPED_TRACE(::testing::Message() << "glyph=" << character);
        auto glyphID = font.getGlyphID(character);
        ASSERT_NE(glyphID, 0);
        Path drawn;
        ASSERT_TRUE(bold.getPath(glyphID, &drawn));
        ASSERT_FALSE(drawn.isEmpty());
        // The bounds only need to contain the drawn geometry now; they are no longer an exact
        // re-measurement of the unioned path. Path::getBounds() is the control-point box, which is
        // at least as large as the curves, so containing it is a sufficient condition — the
        // converse does not hold: a box that misses a control point can still hold every curve.
        auto ink = drawn.getBounds();
        auto bounds = bold.getBounds(glyphID);
        ASSERT_FALSE(bounds.isEmpty());
        EXPECT_LE(bounds.left, ink.left);
        EXPECT_LE(bounds.top, ink.top);
        EXPECT_GE(bounds.right, ink.right);
        EXPECT_GE(bounds.bottom, ink.bottom);
        // Whole pixels keep the raster origin and the atlas cell size predictable; floor() cannot
        // reject an infinite value, so finiteness is checked on its own.
        EXPECT_TRUE(std::isfinite(bounds.left));
        EXPECT_TRUE(std::isfinite(bounds.top));
        EXPECT_TRUE(std::isfinite(bounds.right));
        EXPECT_TRUE(std::isfinite(bounds.bottom));
        EXPECT_EQ(bounds.left, std::floor(bounds.left));
        EXPECT_EQ(bounds.top, std::floor(bounds.top));
        EXPECT_EQ(bounds.right, std::floor(bounds.right));
        EXPECT_EQ(bounds.bottom, std::floor(bounds.bottom));
        // Synthetic bold is rasterised from the path, not from a glyph bitmap.
        EXPECT_EQ(bold.getImage(glyphID, nullptr, nullptr), nullptr);
        EXPECT_EQ(bold.getAdvance(glyphID), font.getAdvance(glyphID));
      }
      auto space = font.getGlyphID(' ');
      ASSERT_NE(space, 0);
      Path spacePath;
      ASSERT_TRUE(bold.getPath(space, &spacePath));
      EXPECT_TRUE(spacePath.isEmpty());
      // An empty glyph has nothing to contain, so it must not be expanded either.
      EXPECT_TRUE(bold.getBounds(space).isEmpty());
      // Measuring with synthetic bold on must not disturb the metrics of the same font.
      auto probe = font.getGlyphID('A');
      auto regularBounds = font.getBounds(probe);
      auto regularAdvance = font.getAdvance(probe);
      Path regularPath;
      ASSERT_TRUE(font.getPath(probe, &regularPath));
      (void)bold.getBounds(probe);
      EXPECT_EQ(font.getBounds(probe), regularBounds);
      EXPECT_EQ(font.getAdvance(probe), regularAdvance);
      Path regularPathAfter;
      ASSERT_TRUE(font.getPath(probe, &regularPathAfter));
      EXPECT_EQ(regularPathAfter, regularPath);
    }
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

namespace tgfx {

// A convex glyph in a 100x100 design box. Convex shapes keep the stroke band and the original
// contour from overlapping in a way that would make appended and unioned contours differ, so this
// fixture isolates the coverage policy instead of fill-rule composition.
class FauxBoldOvalProvider final : public PathProvider {
 public:
  Path getPath() const override {
    Path path = {};
    path.addOval(Rect::MakeXYWH(22, 18, 56, 64));
    path.close();
    return path;
  }

  Rect getBounds() const override {
    return Rect::MakeXYWH(22, 18, 56, 64);
  }
};

class FauxBoldRectProvider final : public PathProvider {
 public:
  Path getPath() const override {
    Path path = {};
    path.addRect(Rect::MakeXYWH(20, 24, 60, 52));
    path.close();
    return path;
  }

  Rect getBounds() const override {
    return Rect::MakeXYWH(20, 24, 60, 52);
  }
};

static std::shared_ptr<Typeface> MakeFauxBoldCustomTypeface() {
  PathTypefaceBuilder builder(100);
  builder.setFontName("fauxCustom", "Regular");
  builder.addGlyph(std::make_shared<FauxBoldOvalProvider>(), 100.0f);
  builder.addGlyph(std::make_shared<FauxBoldRectProvider>(), 100.0f);
  return builder.detach();
}

// Rasterizes the glyph's own path at the same size and offset the scaler context uses, so the
// expectation depends only on the public path and not on how the image is produced internally.
static Bitmap RasterizeGlyph(const Font& font, GlyphID glyphID, const Rect& bounds,
                             bool needsGammaCorrection) {
  Bitmap bitmap(static_cast<int>(bounds.width()), static_cast<int>(bounds.height()), true, false);
  Path path = {};
  if (!font.getPath(glyphID, &path) || path.isEmpty()) {
    return bitmap;
  }
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto rasterizer = PathRasterizer::MakeFrom(bitmap.width(), bitmap.height(), std::move(path), true,
                                             &matrix, needsGammaCorrection);
  if (rasterizer == nullptr) {
    return bitmap;
  }
  auto pixels = bitmap.lockPixels();
  bool ok = pixels != nullptr && rasterizer->readPixels(bitmap.info(), pixels);
  if (pixels != nullptr) {
    bitmap.unlockPixels();
  }
  if (!ok) {
    return Bitmap();
  }
  return bitmap;
}

TGFX_TEST(FauxBoldTest, CustomPathFauxBoldUsesRawCoverage) {
  auto typeface = MakeFauxBoldCustomTypeface();
  ASSERT_NE(typeface, nullptr);
  ASSERT_TRUE(typeface->hasOutlines());
  ASSERT_FALSE(typeface->hasColor());
  // unitsPerEm == size keeps textScale at 1, so this test measures the coverage policy alone and
  // does not depend on how design-space strokes are scaled.
  constexpr float size = 100.0f;
  {
    Font font(typeface, size);
    for (GlyphID glyphID : {static_cast<GlyphID>(1), static_cast<GlyphID>(2)}) {
      SCOPED_TRACE(::testing::Message() << "glyph=" << glyphID);
      Font regular = font;
      Font bold = font;
      bold.setFauxBold(true);
      auto regularBounds = regular.getBounds(glyphID);
      auto boldBounds = bold.getBounds(glyphID);
      ASSERT_FALSE(regularBounds.isEmpty());
      ASSERT_FALSE(boldBounds.isEmpty());
      Path regularPath = {};
      ASSERT_TRUE(regular.getPath(glyphID, &regularPath));
      ASSERT_FALSE(regularPath.isEmpty());

      // The rendered outline is the stroked and unioned one, so it must be strictly wider.
      EXPECT_GT(boldBounds.width(), regularBounds.width());

      auto rawBold = RasterizeGlyph(bold, glyphID, boldBounds, false);
      auto correctedBold = RasterizeGlyph(bold, glyphID, boldBounds, true);
      ASSERT_FALSE(rawBold.isEmpty());
      ASSERT_FALSE(correctedBold.isEmpty());

      // The fixture has curved and diagonal coverage, otherwise the comparison proves nothing.
      long rawSum = 0;
      long correctedSum = 0;
      auto rawPixels = static_cast<const uint8_t*>(rawBold.lockPixels());
      auto correctedPixels = static_cast<const uint8_t*>(correctedBold.lockPixels());
      ASSERT_NE(rawPixels, nullptr);
      ASSERT_NE(correctedPixels, nullptr);
      for (size_t i = 0; i < rawBold.info().byteSize(); ++i) {
        rawSum += rawPixels[i];
        correctedSum += correctedPixels[i];
      }
      rawBold.unlockPixels();
      correctedBold.unlockPixels();
      ASSERT_GT(rawSum, 0);
      EXPECT_GT(correctedSum, rawSum);

      auto image = bold.getImage(glyphID, nullptr, nullptr);
      ASSERT_NE(image, nullptr);
      ASSERT_EQ(image->width(), rawBold.width());
      ASSERT_EQ(image->height(), rawBold.height());

      Bitmap actual(image->width(), image->height(), true, false);
      auto actualPixels = actual.lockPixels();
      ASSERT_NE(actualPixels, nullptr);
      bool ok = image->readPixels(actual.info(), actualPixels);
      actual.unlockPixels();
      ASSERT_TRUE(ok);

      // fauxBold must render the raw coverage, matching the CoreGraphics and FreeType backends.
      auto expectedPixels = static_cast<const uint8_t*>(rawBold.lockPixels());
      auto producedPixels = static_cast<const uint8_t*>(actual.lockPixels());
      ASSERT_NE(expectedPixels, nullptr);
      ASSERT_NE(producedPixels, nullptr);
      int maxDifference = 0;
      for (size_t i = 0; i < actual.info().byteSize(); ++i) {
        maxDifference = std::max(maxDifference, std::abs(static_cast<int>(expectedPixels[i]) -
                                                         static_cast<int>(producedPixels[i])));
      }
      rawBold.unlockPixels();
      actual.unlockPixels();
      EXPECT_LE(maxDifference, 1);
    }
  }
}

TGFX_TEST(FauxBoldTest, CustomPathRegularKeepsGammaCoverage) {
  auto typeface = MakeFauxBoldCustomTypeface();
  ASSERT_NE(typeface, nullptr);
  Font font(typeface, 100.0f);
  auto glyphID = static_cast<GlyphID>(1);
  auto bounds = font.getBounds(glyphID);
  ASSERT_FALSE(bounds.isEmpty());
  auto corrected = RasterizeGlyph(font, glyphID, bounds, true);
  auto raw = RasterizeGlyph(font, glyphID, bounds, false);
  ASSERT_FALSE(corrected.isEmpty());
  ASSERT_FALSE(raw.isEmpty());

  auto image = font.getImage(glyphID, nullptr, nullptr);
  ASSERT_NE(image, nullptr);
  Bitmap actual(image->width(), image->height(), true, false);
  auto actualPixels = actual.lockPixels();
  ASSERT_NE(actualPixels, nullptr);
  bool ok = image->readPixels(actual.info(), actualPixels);
  actual.unlockPixels();
  ASSERT_TRUE(ok);

  auto correctedPixels = static_cast<const uint8_t*>(corrected.lockPixels());
  auto rawPixels = static_cast<const uint8_t*>(raw.lockPixels());
  auto producedPixels = static_cast<const uint8_t*>(actual.lockPixels());
  ASSERT_NE(correctedPixels, nullptr);
  ASSERT_NE(rawPixels, nullptr);
  ASSERT_NE(producedPixels, nullptr);
  int maxDifference = 0;
  long correctedSum = 0;
  long rawSum = 0;
  // Regular custom-path text keeps whatever coverage policy the build was compiled with, so the
  // expectation follows the same switch as the production code instead of assuming gamma is on.
#ifdef TGFX_USE_TEXT_GAMMA_CORRECTION
  const uint8_t* expectedPixels = correctedPixels;
#else
  const uint8_t* expectedPixels = rawPixels;
#endif
  for (size_t i = 0; i < actual.info().byteSize(); ++i) {
    maxDifference = std::max(maxDifference, std::abs(static_cast<int>(expectedPixels[i]) -
                                                     static_cast<int>(producedPixels[i])));
    correctedSum += correctedPixels[i];
    rawSum += rawPixels[i];
  }
  corrected.unlockPixels();
  raw.unlockPixels();
  actual.unlockPixels();
  // The fixture has partial coverage, so the two policies must actually differ; otherwise matching
  // either one would prove nothing about the switch.
  EXPECT_GT(correctedSum, rawSum);
  EXPECT_LE(maxDifference, 1);
}

// Platform-independent visual lock: the same custom outline rendered as regular (top row) and
// fauxBold (bottom row). CI collects the per-backend result; the image is not meant to match Adobe
// output pixel for pixel.
TGFX_TEST(FauxBoldTest, FauxBoldCoverageBaseline) {
  auto typeface = MakeFauxBoldCustomTypeface();
  ASSERT_NE(typeface, nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 260, 280);
  ASSERT_NE(surface, nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  Paint paint = {};
  paint.setColor(Color::Black());
  Font font(typeface, 100.0f);
  const std::vector<GlyphID> glyphs = {1, 2};
  const std::vector<Point> regularPositions = {Point::Make(25, 110), Point::Make(140, 110)};
  canvas->drawGlyphs(glyphs.data(), regularPositions.data(), glyphs.size(), font, paint);
  Font bold = font;
  bold.setFauxBold(true);
  const std::vector<Point> boldPositions = {Point::Make(25, 255), Point::Make(140, 255)};
  canvas->drawGlyphs(glyphs.data(), boldPositions.data(), glyphs.size(), bold, paint);
  // Always assert: a missing reference is a real failure here, the same way it is for every other
  // screenshot case. Skip this test with --gtest_filter instead of swallowing the result.
  EXPECT_TRUE(Baseline::Compare(surface, "FauxBoldTest/FauxBoldCoverage"));
}

}  // namespace tgfx
