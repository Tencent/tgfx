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

class GlyphPathProvider final : public PathProvider {
 public:
  explicit GlyphPathProvider(int pathIndex) : pathIndex(pathIndex) {
  }

  Path getPath() const override {
    Path path;
    switch (pathIndex) {
      case 0:
        path.moveTo(Point::Make(25.0f, 5.0f));
        path.lineTo(Point::Make(45.0f, 45.0f));
        path.lineTo(Point::Make(5.0f, 45.0f));
        path.close();
        break;
      case 1:
        path.moveTo(Point::Make(5.0f, 5.0f));
        path.lineTo(Point::Make(45.0f, 5.0f));
        path.lineTo(Point::Make(45.0f, 45.0f));
        path.lineTo(Point::Make(5.0f, 45.0f));
        path.close();
        break;
      case 2: {
        Rect rect = Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
        path.addOval(rect);
        path.close();
        break;
      }
      default: {
        Rect rect = Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f);
        path.addOval(rect);
        path.close();
        break;
      }
    }
    return path;
  }

  Rect getBounds() const override {
    switch (pathIndex) {
      case 0:
      case 1:
      case 2:
        return Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
      default:
        return Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f);
    }
  }

 private:
  int pathIndex = 0;
};

TGFX_TEST(TypefaceTest, CustomPathTypeface) {
  const std::string fontFamily = "customPath";
  const std::string fontStyle = "customStyle";
  // GlyphPathProvider creates paths with coordinates in the 5-45 range (about 50 pixels).
  // Set unitsPerEm=50 to match the path coordinate space.
  PathTypefaceBuilder builder(50);
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
  ASSERT_EQ(typeface->unitsPerEm(), 50);

  builder.addGlyph(std::make_shared<GlyphPathProvider>(4));

  typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(4));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 150);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  // With fontSize=50 and unitsPerEm=50, textScale = 1.0 (original size)
  Font font(std::move(typeface), 50.0f);
  std::vector<GlyphID> glyphIDs1 = {1, 2, 3};
  std::vector<Point> positions1 = {Point::Make(45, 50), Point::Make(105, 50),
                                   Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs1.data(), positions1.data(), glyphIDs1.size(), font, paint);

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
