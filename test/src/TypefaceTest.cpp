/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/vectors/custom/ImageTypeface.h"
#include "core/vectors/custom/PathTypeface.h"
#include "tgfx/core/ImageTypefaceBuilder.h"
#include "tgfx/core/PathTypefaceBuilder.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(TypefaceTest, CustomPathTypeface) {
  const std::string fontFamily = "customPath";
  const std::string fontStyle = "customStyle";
  PathTypefaceBuilder builder(fontFamily, fontStyle, {});
  Path path;
  path.moveTo(Point::Make(25.0f, 5.0f));
  path.lineTo(Point::Make(45.0f, 45.0f));
  path.lineTo(Point::Make(5.0f, 45.0f));
  path.close();
  builder.addGlyph(path, 1);

  path.reset();
  path.moveTo(Point::Make(5.0f, 5.0f));
  path.lineTo(Point::Make(45.0f, 5.0f));
  path.lineTo(Point::Make(45.0f, 45.0f));
  path.lineTo(Point::Make(5.0f, 45.0f));
  path.close();
  builder.addGlyph(path, 2);

  path.reset();
  Rect rect = Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
  path.addOval(rect);
  path.close();
  builder.addGlyph(path, 3);

  path.reset();
  rect = Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f);
  path.addOval(rect);
  path.close();
  builder.addGlyph(path, 100);

  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasOutlines());
  ASSERT_FALSE(typeface->hasColor());
  ASSERT_TRUE(typeface->getBytes() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(4));
  ASSERT_EQ(typeface->getGlyphID(100), static_cast<GlyphID>(4));
  ASSERT_EQ(typeface->getGlyphID(1000), static_cast<GlyphID>(0));

  path.reset();
  path.moveTo(Point::Make(25.0f, 5.0f));
  path.lineTo(Point::Make(35.0f, 35.0f));
  path.lineTo(Point::Make(45.0f, 5.0f));
  path.close();
  builder.addGlyph(path, 101);

  typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(5));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 100);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  float scaleFactor = 1.0f;
  canvas->scale(scaleFactor, scaleFactor);

  Font font(std::move(typeface), 1.f);
  std::vector<GlyphID> glyphIDs1 = {1, 2, 3};
  std::vector<Point> positions1 = {};
  positions1.push_back(Point::Make(10.0f, 0.0f));
  positions1.push_back(Point::Make(60.0f, 0.0f));
  positions1.push_back(Point::Make(120.0f, 0.0f));
  canvas->drawGlyphs(glyphIDs1.data(), positions1.data(), glyphIDs1.size(), font, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomPathTypeface"));
}

TGFX_TEST(TypefaceTest, CustomImageTypeface) {
  const std::string fontFamily = "customImage";
  const std::string fontStyle = "customStyle";
  ImageTypefaceBuilder builder(fontFamily, fontStyle, {});

  std::string imagePath = "resources/assets/glyph1.png";
  auto imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f), 4);

  imagePath = "resources/assets/glyph2.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(5.0f, 5.0f), 5);

  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasColor());
  ASSERT_FALSE(typeface->hasOutlines());
  ASSERT_TRUE(typeface->getBytes() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(2));
  ASSERT_EQ(typeface->getGlyphID(4), static_cast<GlyphID>(1));
  ASSERT_EQ(typeface->getGlyphID(1000), static_cast<GlyphID>(0));

  imagePath = "resources/assets/glyph3.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 5.0f), 6);

  typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(3));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  float scaleFactor = 1.0f;
  canvas->scale(scaleFactor, scaleFactor);

  Font font(std::move(typeface), 60);
  std::vector<GlyphID> glyphIDs2 = {1, 2, 3};
  std::vector<Point> positions2 = {};
  positions2.push_back(Point::Make(150.0f, 0.0f));
  positions2.push_back(Point::Make(215.0f, 0.0f));
  positions2.push_back(Point::Make(280.0f, 0.0f));
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(), font, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomImageTypeface"));
}

}  // namespace tgfx