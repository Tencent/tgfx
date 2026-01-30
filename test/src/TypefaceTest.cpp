/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/CustomTypeface.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

namespace tgfx {

// A PathProvider that creates paths in 25x25 coordinate space for testing unitsPerEm scaling.
// With unitsPerEm=25 and fontSize=50, textScale=2.0, so paths scale to 50x50 pixels.
// This also tests fauxBold scaling: if fauxBold incorrectly uses textScale * fauxBoldScale
// instead of fontSize * fauxBoldScale, the bold effect would be wrong.
class GlyphPathProvider final : public PathProvider {
 public:
  explicit GlyphPathProvider(int pathIndex) : pathIndex(pathIndex) {
  }

  Path getPath() const override {
    Path path;
    switch (pathIndex) {
      case 0:
        path.moveTo(Point::Make(12.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 22.5f));
        path.lineTo(Point::Make(2.5f, 22.5f));
        path.close();
        break;
      case 1:
        path.moveTo(Point::Make(2.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 22.5f));
        path.lineTo(Point::Make(2.5f, 22.5f));
        path.close();
        break;
      case 2: {
        Rect rect = Rect::MakeXYWH(2.5f, 2.5f, 20.0f, 20.0f);
        path.addOval(rect);
        path.close();
        break;
      }
      default:
        break;
    }
    return path;
  }

  Rect getBounds() const override {
    return Rect::MakeXYWH(2.5f, 2.5f, 20.0f, 20.0f);
  }

 private:
  int pathIndex = 0;
};

TGFX_TEST(TypefaceTest, CustomPathTypeface) {
  const std::string fontFamily = "customPath";
  const std::string fontStyle = "customStyle";
  // Paths are designed in 25x25 coordinate space. Set unitsPerEm=25 so that with fontSize=50,
  // textScale=2.0 scales them to 50x50 pixels.
  PathTypefaceBuilder builder(25);
  builder.setFontName(fontFamily, fontStyle);

  builder.addGlyph(std::make_shared<GlyphPathProvider>(0));
  builder.addGlyph(std::make_shared<GlyphPathProvider>(1));
  builder.addGlyph(std::make_shared<GlyphPathProvider>(2));
  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasOutlines());
  ASSERT_FALSE(typeface->hasColor());
  ASSERT_TRUE(typeface->openStream() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(3));
  ASSERT_EQ(typeface->unitsPerEm(), 25);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 150);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  // fontSize=50, unitsPerEm=25 => textScale=2.0
  // Glyphs will be scaled 2x from 25x25 design space to 50x50 pixels.
  // FauxBold uses fontSize(50) for calculation.
  // fauxBoldSize = 50 * FauxBoldScale(50) ≈ 1.5625 pixels.
  Font font(typeface, 50.0f);
  font.setFauxBold(true);
  std::vector<GlyphID> glyphIDs = {1, 2, 3};
  std::vector<Point> positions = {Point::Make(45, 50), Point::Make(105, 50),
                                  Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs.data(), positions.data(), glyphIDs.size(), font, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomPathTypeface"));
}

TGFX_TEST(TypefaceTest, CustomImageTypeface) {
  const std::string fontFamily = "customImage";
  const std::string fontStyle = "customStyle";
  // Glyph images are 200x200 pixels. Set unitsPerEm=200 to match the image size.
  ImageTypefaceBuilder builder(200);
  builder.setFontName(fontFamily, fontStyle);
  std::string imagePath = "resources/assets/glyph1.png";
  auto imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  imagePath = "resources/assets/glyph2.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasColor());
  ASSERT_FALSE(typeface->hasOutlines());
  ASSERT_TRUE(typeface->openStream() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(2));
  ASSERT_EQ(typeface->unitsPerEm(), 200);

  imagePath = "resources/assets/glyph3.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(3));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 150);
  auto canvas = surface->getCanvas();

  // With fontSize=50 and unitsPerEm=200, textScale = 0.25
  // 200x200 images will render as 50x50 pixels
  Font font(std::move(typeface), 50.0f);
  std::vector<GlyphID> glyphIDs2 = {1, 2, 3};
  std::vector<Point> positions2 = {Point::Make(45, 50), Point::Make(105, 50),
                                   Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(), font, {});

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomImageTypeface"));
}
}  // namespace tgfx
