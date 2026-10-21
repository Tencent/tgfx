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
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlobBuilder.h"
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

  builder.addGlyph(std::make_shared<GlyphPathProvider>(0), 25.0f);
  builder.addGlyph(std::make_shared<GlyphPathProvider>(1), 25.0f);
  builder.addGlyph(std::make_shared<GlyphPathProvider>(2), 25.0f);
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
  std::vector<Point> positions = {Point::Make(45, 50), Point::Make(105, 50), Point::Make(165, 50)};
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
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f), 200.0f);

  imagePath = "resources/assets/glyph2.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f), 200.0f);

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
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f), 200.0f);

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
  std::vector<Point> positions2 = {Point::Make(45, 50), Point::Make(105, 50), Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(), font, {});

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomImageTypeface"));
}

// A frame with a hole inside it. Both rectangles run the same way, so the hole is solid under the
// winding rule and open under the even-odd rule. That makes the two rules distinguishable by
// asking whether the centre is inside, without needing a real font file.
static Path MakeRingPath(PathFillType fillType) {
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  path.addRect(Rect::MakeLTRB(200.0f, 200.0f, 800.0f, 800.0f));
  path.setFillType(fillType);
  return path;
}

static std::shared_ptr<Typeface> MakeRingTypeface(PathFillType fillType) {
  PathTypefaceBuilder builder(1000);
  if (builder.addGlyph(MakeRingPath(fillType), 1000.0f) == 0) {
    return nullptr;
  }
  return builder.detach();
}

// Glyphs of one typeface do not have to share a fill rule, and path operations such as faux bold
// can change the rule of an outline. A typeface-wide rule would report one answer for all of them,
// so the shape of each glyph has to read the rule from the outline it actually returns.
TGFX_TEST(TypefaceTest, MixedGlyphFillRulesStayIndependent) {
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(MakeRingPath(PathFillType::Winding), 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(MakeRingPath(PathFillType::EvenOdd), 1000.0f), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);

  Path solidPath = {};
  ASSERT_TRUE(font.getPath(1, &solidPath));
  EXPECT_EQ(solidPath.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(solidPath.contains(500.0f, 500.0f));
  auto solidShape = Shape::MakeFrom(font, 1);
  ASSERT_TRUE(solidShape != nullptr);
  EXPECT_EQ(solidShape->fillType(), PathFillType::Winding);
  EXPECT_EQ(solidShape->getPath().getFillType(), PathFillType::Winding);

  Path openPath = {};
  ASSERT_TRUE(font.getPath(2, &openPath));
  EXPECT_EQ(openPath.getFillType(), PathFillType::EvenOdd);
  EXPECT_FALSE(openPath.contains(500.0f, 500.0f));
  auto openShape = Shape::MakeFrom(font, 2);
  ASSERT_TRUE(openShape != nullptr);
  EXPECT_EQ(openShape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(openShape->getPath().getFillType(), PathFillType::EvenOdd);
}

// A blob can mix typefaces whose outlines follow different rules. Merging them must not force one
// rule onto the other: the solid frame has to stay solid and the open frame has to stay open.
TGFX_TEST(TypefaceTest, MixedRunTextBlobKeepsBothAreas) {
  auto windingTypeface = MakeRingTypeface(PathFillType::Winding);
  auto evenOddTypeface = MakeRingTypeface(PathFillType::EvenOdd);
  ASSERT_TRUE(windingTypeface != nullptr);
  ASSERT_TRUE(evenOddTypeface != nullptr);
  Font windingFont(windingTypeface, 1000.0f);
  Font evenOddFont(evenOddTypeface, 1000.0f);

  TextBlobBuilder blobBuilder = {};
  auto firstRun = blobBuilder.allocRunPos(windingFont, 1);
  firstRun.glyphs[0] = 1;
  firstRun.positions[0] = 0.0f;
  firstRun.positions[1] = 0.0f;
  auto secondRun = blobBuilder.allocRunPos(evenOddFont, 1);
  secondRun.glyphs[0] = 1;
  secondRun.positions[0] = 1000.0f;
  secondRun.positions[1] = 0.0f;
  auto blob = blobBuilder.build();
  ASSERT_TRUE(blob != nullptr);

  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);
  auto path = shape->getPath();
  EXPECT_EQ(shape->fillType(), path.getFillType());
  EXPECT_TRUE(path.contains(500.0f, 500.0f));
  EXPECT_FALSE(path.contains(1500.0f, 500.0f));
}

// PathProvider can report its rule without computing the path. The wrapper around a Path has to
// report the rule of that Path, otherwise every wrapped outline looks like a winding one.
TGFX_TEST(TypefaceTest, PathProviderKeepsWrappedFillRule) {
  auto provider = PathProvider::Wrap(MakeRingPath(PathFillType::EvenOdd));
  ASSERT_TRUE(provider != nullptr);
  EXPECT_EQ(provider->fillType(), PathFillType::EvenOdd);

  auto shape = Shape::MakeFrom(provider);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(shape->getPath().getFillType(), PathFillType::EvenOdd);

  // A glyph contributed through a provider keeps the rule the provider reports. The rule has to
  // stay reachable without resolving the path, which is the whole point of a provider.
  PathTypefaceBuilder typefaceBuilder(1000);
  ASSERT_NE(typefaceBuilder.addGlyph(provider, 1000.0f), 0);
  auto typeface = typefaceBuilder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);
  auto glyphShape = Shape::MakeFrom(font, 1);
  ASSERT_TRUE(glyphShape != nullptr);
  EXPECT_EQ(glyphShape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(glyphShape->getPath().getFillType(), PathFillType::EvenOdd);
}

// An empty path is empty only while it uses a normal fill type. With an inverse fill type it has
// no contour segments yet fills the entire plane, so skipping it would turn the whole plane into
// nothing.
TGFX_TEST(TypefaceTest, EmptyInverseGlyphKeepsWholePlaneFill) {
  Path inversePath = {};
  inversePath.setFillType(PathFillType::InverseEvenOdd);
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(inversePath, 1000.0f), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);

  GlyphID glyphs[] = {1};
  Point positions[] = {Point::Make(0.0f, 0.0f)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 1, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);

  auto path = shape->getPath();
  EXPECT_TRUE(path.isInverseFillType());
  EXPECT_TRUE(path.contains(2000.0f, 2000.0f)) << "whole-plane fill lost";
  EXPECT_EQ(shape->fillType(), path.getFillType());
}

// An inverse outline unioned with the rectangle it excludes covers the whole plane. That result
// has no contour segments either, so merging a third glyph must not mistake it for "not
// initialized" and overwrite the plane.
TGFX_TEST(TypefaceTest, InverseGlyphUnionKeepsWholePlaneResult) {
  auto makeRect = [](PathFillType fillType) {
    Path path = {};
    path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
    path.setFillType(fillType);
    return path;
  };
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(makeRect(PathFillType::InverseEvenOdd), 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(makeRect(PathFillType::EvenOdd), 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(makeRect(PathFillType::EvenOdd), 1000.0f), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);

  GlyphID glyphs[] = {1, 2, 3};
  Point positions[] = {Point::Make(0.0f, 0.0f), Point::Make(0.0f, 0.0f), Point::Make(0.0f, 0.0f)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 3, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);

  auto path = shape->getPath();
  // The first two glyphs already cover everything, so a point far outside the rectangle is filled.
  EXPECT_TRUE(path.contains(2000.0f, 2000.0f)) << "whole-plane result overwritten";
  EXPECT_TRUE(path.contains(500, 500));
  EXPECT_TRUE(path.isEmpty());
  EXPECT_TRUE(path.isInverseFillType());
  EXPECT_EQ(shape->fillType(), path.getFillType());
}

// Two overlapping even-odd outlines share one rule, but appending them and filling with that rule
// would cancel the overlap out. The merged shape has to keep it filled.
TGFX_TEST(TypefaceTest, OverlappingGlyphsDoNotCancelOut) {
  auto typeface = MakeRingTypeface(PathFillType::EvenOdd);
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);
  GlyphID glyphs[] = {1, 1};
  Point positions[] = {Point::Make(0.0f, 0.0f), Point::Make(100.0f, 100.0f)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 2, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);

  auto path = shape->getPath();
  // Solid in both frames: appending the outlines would make this a hole.
  EXPECT_TRUE(path.contains(150.0f, 150.0f));
  // The counter of the first frame stays open.
  EXPECT_FALSE(path.contains(500.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, OppositeWindingGlyphsDoNotCancelOut) {
  Path first = {};
  first.moveTo(0, 0);
  first.lineTo(1000, 0);
  first.lineTo(1000, 1000);
  first.lineTo(0, 1000);
  first.close();
  first.setFillType(PathFillType::Winding);
  Path second = {};
  second.moveTo(0, 0);
  second.lineTo(0, 1000);
  second.lineTo(1000, 1000);
  second.lineTo(1000, 0);
  second.close();
  second.setFillType(PathFillType::Winding);
  ASSERT_TRUE(first.contains(500, 500));
  ASSERT_TRUE(second.contains(500, 500));
  auto appended = first;
  appended.addPath(second);
  ASSERT_FALSE(appended.contains(500, 500));

  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(first, 1000), 0);
  ASSERT_NE(builder.addGlyph(second, 1000), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000);
  GlyphID glyphs[] = {1, 2};
  Point positions[] = {Point::Make(0, 0), Point::Make(100, 100)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 2, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);
  auto path = shape->getPath();
  EXPECT_EQ(shape->fillType(), path.getFillType());
  EXPECT_TRUE(path.contains(500, 500));
  EXPECT_TRUE(path.contains(50, 50));
  EXPECT_TRUE(path.contains(1050, 1050));
  EXPECT_FALSE(path.contains(1200, 1200));
}
}  // namespace tgfx
