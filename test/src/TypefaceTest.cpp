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

#include <chrono>
#include <limits>
#include "core/GlyphTransform.h"
#include "core/utils/ShapeUtils.h"
#include "tgfx/core/CustomTypeface.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PathMeasure.h"
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
  // Read the rule before getPath(). Read after it, the rule is whatever the cached path carries and
  // the two agree by construction, which is the disagreement this has to be able to catch.
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  auto path = shape->getPath();
  EXPECT_EQ(path.getFillType(), PathFillType::EvenOdd);
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

  EXPECT_EQ(shape->fillType(), PathFillType::InverseEvenOdd);
  auto path = shape->getPath();
  EXPECT_TRUE(path.isInverseFillType());
  EXPECT_TRUE(path.contains(2000.0f, 2000.0f)) << "whole-plane fill lost";
  EXPECT_EQ(path.getFillType(), PathFillType::InverseEvenOdd);
}

static Path MakeRectPath(PathFillType fillType) {
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  path.setFillType(fillType);
  return path;
}

// An inverse outline unioned with the rectangle it excludes covers the whole plane. That result
// has no contour segments either, so merging a third glyph must not mistake it for "not
// initialized" and overwrite the plane.
TGFX_TEST(TypefaceTest, InverseGlyphUnionKeepsWholePlaneResult) {
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(MakeRectPath(PathFillType::InverseEvenOdd), 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(MakeRectPath(PathFillType::EvenOdd), 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(MakeRectPath(PathFillType::EvenOdd), 1000.0f), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);

  GlyphID glyphs[] = {1, 2, 3};
  Point positions[] = {Point::Make(0.0f, 0.0f), Point::Make(0.0f, 0.0f), Point::Make(0.0f, 0.0f)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 3, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);

  EXPECT_EQ(shape->fillType(), PathFillType::InverseEvenOdd);
  auto path = shape->getPath();
  // The first two glyphs already cover everything, so a point far outside the rectangle is filled.
  EXPECT_TRUE(path.contains(2000.0f, 2000.0f)) << "whole-plane result overwritten";
  EXPECT_TRUE(path.contains(500, 500));
  EXPECT_TRUE(path.isEmpty());
  EXPECT_TRUE(path.isInverseFillType());
  EXPECT_EQ(path.getFillType(), PathFillType::InverseEvenOdd);
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
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  auto path = shape->getPath();
  EXPECT_EQ(path.getFillType(), PathFillType::EvenOdd);
  EXPECT_TRUE(path.contains(500, 500));
  EXPECT_TRUE(path.contains(50, 50));
  EXPECT_TRUE(path.contains(1050, 1050));
  EXPECT_FALSE(path.contains(1200, 1200));
}

// Faux bold thickens a glyph by merging an outline traced around it into the glyph itself. That
// merge has to combine filled areas the way generatePath() does: appending the two instead lets the
// stroke and the glyph cancel each other wherever they overlap, which under a parity rule hollows
// small glyphs out. This covers generatePath(); the rasterized path is exercised by
// CustomPathTypeface, which draws faux bold glyphs against a baseline image.
TGFX_TEST(TypefaceTest, FauxBoldKeepsEvenOddGlyphFilled) {
  auto typeface = MakeRingTypeface(PathFillType::EvenOdd);
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);
  font.setFauxBold(true);

  GlyphID glyphs[] = {1};
  Point positions[] = {Point::Make(0.0f, 0.0f)};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 1, font);
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  auto path = shape->getPath();
  EXPECT_EQ(path.getFillType(), PathFillType::EvenOdd);
  EXPECT_TRUE(path.contains(100.0f, 100.0f)) << "ring lost";
  EXPECT_FALSE(path.contains(500.0f, 500.0f)) << "counter filled in";
}

namespace {

// Replica of the merge TextShape::onGetPath() used before it was replaced by Path::Merge(), so
// the two can be compared on the same outlines without going through a screenshot.
static int DiagDirection(const Path& path) {
  auto bounds = path.getBounds();
  if (bounds.isEmpty()) {
    return 0;
  }
  auto ox = static_cast<double>(bounds.centerX());
  auto oy = static_cast<double>(bounds.centerY());
  double area = 0.0;
  Point from = {};
  auto hasFrom = false;
  for (const auto& segment : path) {
    if (segment.verb == PathVerb::Move) {
      from = segment.points[0];
      hasFrom = true;
      continue;
    }
    if (!hasFrom || segment.verb == PathVerb::Close) {
      continue;
    }
    auto to = segment.points[1];
    if (segment.verb == PathVerb::Quad || segment.verb == PathVerb::Conic) {
      to = segment.points[2];
    } else if (segment.verb == PathVerb::Cubic) {
      to = segment.points[3];
    }
    area += (static_cast<double>(from.x) - ox) * (static_cast<double>(to.y) - oy) -
            (static_cast<double>(to.x) - ox) * (static_cast<double>(from.y) - oy);
    from = to;
  }
  return area > 0.0 ? 1 : (area < 0.0 ? -1 : 0);
}

static bool DiagCanAppend(const Path& total, const Path& glyph) {
  if (total.getFillType() != glyph.getFillType()) {
    return false;
  }
  if (total.isInverseFillType() || glyph.isInverseFillType()) {
    return false;
  }
  if (total.getFillType() != PathFillType::Winding) {
    return false;
  }
  auto a = total.getBounds();
  auto b = glyph.getBounds();
  if (a.isEmpty() || b.isEmpty() || !a.intersect(b)) {
    return true;
  }
  auto td = DiagDirection(total);
  auto gd = DiagDirection(glyph);
  return td != 0 && gd != 0 && td == gd;
}

static float DiagnosticLength(const Path& path) {
  auto measure = PathMeasure::MakeFrom(path);
  float length = 0.0f;
  do {
    length += measure->getLength();
  } while (measure->nextContour());
  return length;
}

static std::vector<Path> DiagnosticGlyphs(const std::string& text, float size, bool fauxBold,
                                          const std::string& fontFile) {
  auto typeface = Typeface::MakeFromPath(ProjectPath::Absolute(fontFile));
  Font font(typeface, size);
  font.setFauxBold(fauxBold);
  auto blob = TextBlob::MakeFrom(text, font);
  std::vector<Path> glyphs = {};
  if (blob == nullptr) {
    return glyphs;
  }
  for (auto run : *blob) {
    for (size_t index = 0; index < run.glyphCount; ++index) {
      Path path = {};
      if (run.font.getPath(run.glyphs[index], &path) && !path.isEmpty()) {
        path.transform(GetGlyphMatrix(run, index));
        glyphs.push_back(std::move(path));
      }
    }
  }
  return glyphs;
}

static Path DiagnosticOldMerge(const std::vector<Path>& glyphs) {
  Path result = {};
  bool initialized = false;
  for (const auto& glyph : glyphs) {
    if (!initialized) {
      result = glyph;
      initialized = true;
    } else {
      result.addPath(glyph, DiagCanAppend(result, glyph) ? PathOp::Append : PathOp::Union);
    }
  }
  return result;
}

static int DiagnosticInteriorDifferences(const Path& a, const Path& b) {
  auto bounds = a.getBounds();
  bounds.join(b.getBounds());
  int differences = 0;
  for (int y = 0; y < 320; ++y) {
    for (int x = 0; x < 320; ++x) {
      auto px = bounds.left + bounds.width() * (static_cast<float>(x) + 0.317f) / 320.0f;
      auto py = bounds.top + bounds.height() * (static_cast<float>(y) + 0.613f) / 320.0f;
      differences += a.contains(px, py) != b.contains(px, py) ? 1 : 0;
    }
  }
  return differences;
}

}  // namespace

TGFX_TEST(TypefaceTest, UnionDoesNotPreserveContourEffectInputs) {
  Path first = {};
  first.addRect(Rect::MakeLTRB(0.0f, 0.0f, 300.0f, 300.0f));
  Path second = {};
  second.addRect(Rect::MakeLTRB(200.0f, 0.0f, 500.0f, 300.0f));
  auto ordered = first;
  ASSERT_TRUE(ordered.addPath(second));
  Path united = {};
  ASSERT_TRUE(united.addPath(ordered, PathOp::Union));
  EXPECT_EQ(united.getFillType(), PathFillType::EvenOdd);
  EXPECT_FLOAT_EQ(DiagnosticLength(ordered), 2400.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(united), 1600.0f);
  EXPECT_TRUE(ordered.contains(250.0f, 150.0f));
  EXPECT_TRUE(united.contains(250.0f, 150.0f));
  auto difference = ordered;
  ASSERT_TRUE(difference.addPath(united, PathOp::XOR));
  EXPECT_TRUE(difference.isEmpty());

  Stroke stroke(2.0f);
  auto orderedStroke = ordered;
  auto unitedStroke = united;
  ASSERT_TRUE(stroke.applyToPath(&orderedStroke));
  ASSERT_TRUE(stroke.applyToPath(&unitedStroke));
  EXPECT_TRUE(orderedStroke.contains(300.0f, 150.0f));
  EXPECT_FALSE(unitedStroke.contains(300.0f, 150.0f));

  auto trim = PathEffect::MakeTrim(0.2f, 0.8f);
  ASSERT_TRUE(trim->filterPath(&ordered));
  ASSERT_TRUE(trim->filterPath(&united));
  auto orderedMeasure = PathMeasure::MakeFrom(ordered);
  auto unitedMeasure = PathMeasure::MakeFrom(united);
  Point oldStart = {};
  Point newStart = {};
  Point tangent = {};
  ASSERT_TRUE(orderedMeasure->getPosTan(0.0f, &oldStart, &tangent));
  ASSERT_TRUE(unitedMeasure->getPosTan(0.0f, &newStart, &tangent));
  EXPECT_NE(oldStart, newStart);
}

TGFX_TEST(TypefaceTest, UnionKeepsOppositeDirectionIslandFilled) {
  Path glyphA = {};
  glyphA.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  glyphA.addRect(Rect::MakeLTRB(2000.0f, 0.0f, 2400.0f, 400.0f), true);
  Path glyphB = {};
  glyphB.addRect(Rect::MakeLTRB(2100.0f, 100.0f, 2300.0f, 300.0f));
  ASSERT_EQ(DiagDirection(glyphA), 1);
  ASSERT_EQ(DiagDirection(glyphB), 1);
  ASSERT_TRUE(DiagCanAppend(glyphA, glyphB));
  ASSERT_TRUE(glyphA.contains(2200.0f, 200.0f));
  ASSERT_TRUE(glyphB.contains(2200.0f, 200.0f));

  auto oldResult = DiagnosticOldMerge({glyphA, glyphB});
  EXPECT_FALSE(oldResult.contains(2200.0f, 200.0f));
  auto merged = Path::Merge({glyphA, glyphB});
  ASSERT_TRUE(merged.has_value());
  auto united = std::move(*merged);
  EXPECT_EQ(united.getFillType(), PathFillType::EvenOdd);
  EXPECT_TRUE(united.contains(2200.0f, 200.0f));
  EXPECT_TRUE(united.contains(500.0f, 500.0f));
  EXPECT_FALSE(united.contains(1500.0f, 200.0f));
}

TGFX_TEST(TypefaceTest, FilledAreaEquivalenceDoesNotPreserveTextPathEffects) {
  auto glyphs = DiagnosticGlyphs("Trim", 48.0f, true, "resources/font/NotoSansSC-Regular.otf");
  ASSERT_FALSE(glyphs.empty());
  auto ordered = DiagnosticOldMerge(glyphs);
  Path united = {};
  ASSERT_TRUE(united.addPath(ordered, PathOp::Union));
  EXPECT_EQ(united.getFillType(), PathFillType::EvenOdd);
  auto difference = ordered;
  ASSERT_TRUE(difference.addPath(united, PathOp::XOR));
  EXPECT_TRUE(difference.isEmpty());
  EXPECT_NE(DiagnosticLength(ordered), DiagnosticLength(united));

  auto orderedTrim = ordered;
  auto unitedTrim = united;
  auto trim = PathEffect::MakeTrim(0.2f, 0.8f);
  ASSERT_TRUE(trim->filterPath(&orderedTrim));
  ASSERT_TRUE(trim->filterPath(&unitedTrim));
  Stroke stroke(2.0f);
  ASSERT_TRUE(stroke.applyToPath(&orderedTrim));
  ASSERT_TRUE(stroke.applyToPath(&unitedTrim));
  EXPECT_GT(DiagnosticInteriorDifferences(orderedTrim, unitedTrim), 0);

  float intervals[] = {8.0f, 4.0f};
  auto dash = PathEffect::MakeDash(intervals, 2, 0.0f);
  ASSERT_TRUE(dash->filterPath(&ordered));
  ASSERT_TRUE(dash->filterPath(&united));
  ASSERT_TRUE(stroke.applyToPath(&ordered));
  ASSERT_TRUE(stroke.applyToPath(&united));
  EXPECT_GT(DiagnosticInteriorDifferences(ordered, united), 0);
}

TGFX_TEST(TypefaceTest, OrderedTextSourcesKeepMixedFillRulesAndStrokeBoundaries) {
  Path winding = MakeRingPath(PathFillType::Winding);
  Path evenOdd = MakeRingPath(PathFillType::EvenOdd);
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(winding, 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(evenOdd, 1000.0f), 0);
  auto typeface = builder.detach();
  GlyphID ids[] = {1, 2};
  Point positions[] = {{0.0f, 0.0f}, {1200.0f, 0.0f}};
  auto blob = TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f));
  auto shape = Shape::MakeFrom(blob);
  ASSERT_TRUE(shape != nullptr);
  std::vector<Path> sources = {};
  ASSERT_TRUE(ShapeUtils::GetPathSources(shape, 1.0f, &sources));
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].getFillType(), PathFillType::Winding);
  EXPECT_EQ(sources[1].getFillType(), PathFillType::EvenOdd);
  auto filled = shape->getPath();
  EXPECT_EQ(filled.getFillType(), PathFillType::EvenOdd);
  EXPECT_TRUE(filled.contains(500.0f, 500.0f));
  EXPECT_FALSE(filled.contains(1700.0f, 500.0f));
  Stroke stroke(10.0f);
  auto stroked = Shape::ApplyStroke(shape, &stroke)->getPath();
  EXPECT_TRUE(stroked.contains(200.0f, 500.0f));
  EXPECT_TRUE(stroked.contains(1400.0f, 500.0f));
  EXPECT_FALSE(stroked.contains(500.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, OrderedTrimKeepsGlobalIntervalsAcrossSources) {
  Path a = {};
  a.addRect(Rect::MakeLTRB(0.0f, 0.0f, 300.0f, 300.0f));
  Path b = {};
  b.addRect(Rect::MakeLTRB(200.0f, 0.0f, 500.0f, 300.0f));
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(a, 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(b, 1000.0f), 0);
  auto typeface = builder.detach();
  GlyphID ids[] = {1, 2};
  Point positions[] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
  auto shape = Shape::MakeFrom(TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f)));
  ASSERT_TRUE(shape != nullptr);
  for (auto range : {Point::Make(0.2f, 0.8f), Point::Make(0.8f, 1.2f), Point::Make(0.8f, 0.2f),
                     Point::Make(1.0f, 0.0f)}) {
    auto reference = a;
    ASSERT_TRUE(reference.addPath(b));
    auto trim = PathEffect::MakeTrim(range.x, range.y);
    ASSERT_TRUE(trim != nullptr);
    ASSERT_TRUE(trim->filterPath(&reference));
    auto effected = Shape::ApplyEffect(shape, trim);
    auto actual = ShapeUtils::GetContourPath(effected, 1.0f);
    EXPECT_FLOAT_EQ(DiagnosticLength(actual), DiagnosticLength(reference));
    Stroke stroke(2.0f);
    ASSERT_TRUE(stroke.applyToPath(&reference));
    ASSERT_TRUE(stroke.applyToPath(&actual));
    auto difference = reference;
    ASSERT_TRUE(difference.addPath(actual, PathOp::XOR));
    EXPECT_TRUE(difference.isEmpty()) << range.x << ", " << range.y;
  }
}

TGFX_TEST(TypefaceTest, OrderedEffectsKeepEmptyInverseSource) {
  Path inverse = {};
  inverse.setFillType(PathFillType::InverseEvenOdd);
  Path rect = {};
  rect.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(inverse, 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(rect, 1000.0f), 0);
  auto typeface = builder.detach();
  GlyphID ids[] = {1, 2};
  Point positions[] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
  auto shape = Shape::MakeFrom(TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f)));
  auto trimmed = Shape::ApplyEffect(shape, PathEffect::MakeTrim(0.0f, 0.5f));
  std::vector<Path> sources = {};
  ASSERT_TRUE(ShapeUtils::GetPathSources(trimmed, 1.0f, &sources));
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].getFillType(), PathFillType::InverseEvenOdd);
  EXPECT_TRUE(sources[0].isEmpty());
  EXPECT_TRUE(trimmed->getPath().contains(2000.0f, 2000.0f));
}

TGFX_TEST(TypefaceTest, FillTypeOverrideEstablishesSourceBoundary) {
  Path a = {};
  a.addRect(Rect::MakeLTRB(0.0f, 0.0f, 300.0f, 300.0f));
  a.setFillType(PathFillType::EvenOdd);
  Path b = {};
  b.addRect(Rect::MakeLTRB(200.0f, 0.0f, 500.0f, 300.0f));
  b.setFillType(PathFillType::EvenOdd);
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(a, 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(b, 1000.0f), 0);
  auto typeface = builder.detach();
  GlyphID ids[] = {1, 2};
  Point positions[] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
  auto shape = Shape::MakeFrom(TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f)));
  auto overridden = Shape::ApplyFillType(shape, PathFillType::Winding);
  auto contours = ShapeUtils::GetContourPath(overridden, 1.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(contours), 1600.0f);
  EXPECT_EQ(contours, overridden->getPath());
  EXPECT_EQ(overridden->getPath().getFillType(), PathFillType::Winding);
  Stroke stroke(2.0f);
  auto stroked = Shape::ApplyStroke(overridden, &stroke);
  EXPECT_FALSE(stroked->getPath().contains(300.0f, 150.0f));
  EXPECT_TRUE(stroked->getPath().contains(500.0f, 150.0f));
}

TGFX_TEST(TypefaceTest, OrderedDashAndRepeatedTrimKeepContourSequence) {
  const std::string fontFile = "resources/font/NotoSansSC-Regular.otf";
  auto typeface = Typeface::MakeFromPath(ProjectPath::Absolute(fontFile));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 48.0f);
  font.setFauxBold(true);
  auto shape = Shape::MakeFrom(TextBlob::MakeFrom("Trim", font));
  auto reference = ShapeUtils::GetContourPath(shape, 1.0f);
  auto first = PathEffect::MakeTrim(0.2f, 0.8f);
  auto second = PathEffect::MakeTrim(0.1f, 0.9f);
  ASSERT_TRUE(first->filterPath(&reference));
  ASSERT_TRUE(second->filterPath(&reference));
  auto actualShape = Shape::ApplyEffect(Shape::ApplyEffect(shape, first), second);
  auto actual = ShapeUtils::GetContourPath(actualShape, 1.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(actual), DiagnosticLength(reference));
  Stroke stroke(2.0f);
  ASSERT_TRUE(stroke.applyToPath(&actual));
  ASSERT_TRUE(stroke.applyToPath(&reference));
  EXPECT_EQ(DiagnosticInteriorDifferences(actual, reference), 0);

  float intervals[] = {8.0f, 4.0f};
  auto dash = PathEffect::MakeDash(intervals, 2, 0.0f);
  reference = ShapeUtils::GetContourPath(shape, 1.0f);
  ASSERT_TRUE(dash->filterPath(&reference));
  actual = ShapeUtils::GetContourPath(Shape::ApplyEffect(shape, dash), 1.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(actual), DiagnosticLength(reference));
  ASSERT_TRUE(stroke.applyToPath(&actual));
  ASSERT_TRUE(stroke.applyToPath(&reference));
  EXPECT_EQ(DiagnosticInteriorDifferences(actual, reference), 0);
}

namespace {

class RejectingSourceEffect final : public PathEffect {
 public:
  bool filterPath(Path* path) const override {
    path->reset();
    return false;
  }
};

}  // namespace

TGFX_TEST(TypefaceTest, OrderedEffectFailureKeepsSourcesUnchanged) {
  std::vector<Path> paths = {MakeRingPath(PathFillType::Winding),
                             MakeRingPath(PathFillType::EvenOdd)};
  auto original = paths;
  RejectingSourceEffect effect;
  EXPECT_FALSE(ShapeUtils::ApplyEffectToSources(effect, &paths));
  ASSERT_EQ(paths.size(), original.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    EXPECT_EQ(paths[i], original[i]);
  }
  EXPECT_FALSE(ShapeUtils::ApplyEffectToSources(effect, nullptr));
}

TGFX_TEST(TypefaceTest, AreaUnionKeepsDisjointContoursAndRejectsInvalidSources) {
  auto a = MakeRingPath(PathFillType::EvenOdd);
  auto b = a;
  b.transform(Matrix::MakeTrans(1200.0f, 0.0f));
  auto expected = a;
  ASSERT_TRUE(expected.addPath(b));
  auto merged = Path::Merge({a, b});
  ASSERT_TRUE(merged.has_value());
  auto result = std::move(*merged);
  EXPECT_EQ(result, expected);
  EXPECT_EQ(result.getFillType(), PathFillType::EvenOdd);
  EXPECT_FALSE(result.contains(500.0f, 500.0f));
  EXPECT_FALSE(result.contains(1700.0f, 500.0f));
  auto original = result;
  Path invalid = {};
  invalid.moveTo(std::numeric_limits<float>::infinity(), 0.0f);
  EXPECT_FALSE(Path::Merge({invalid}).has_value());
  EXPECT_EQ(result, original);
  auto selfMerged = Path::Merge({result});
  ASSERT_TRUE(selfMerged.has_value());
  result = std::move(*selfMerged);
  EXPECT_TRUE(result.contains(100.0f, 100.0f));
  EXPECT_FALSE(result.contains(500.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, OrderedTrimKeepsTransformedSourceIntervals) {
  auto typeface = MakeRingTypeface(PathFillType::EvenOdd);
  ASSERT_TRUE(typeface != nullptr);
  GlyphID ids[] = {1, 1};
  Point positions[] = {{0.0f, 0.0f}, {1200.0f, 0.0f}};
  auto shape = Shape::MakeFrom(TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f)));
  auto matrix = Matrix::MakeScale(0.5f, 1.5f);
  matrix.postRotate(20.0f);
  matrix.postTranslate(30.0f, 50.0f);
  auto transformed = Shape::ApplyMatrix(shape, matrix);
  auto trim = PathEffect::MakeTrim(0.2f, 0.8f);
  auto effected = Shape::ApplyEffect(transformed, trim);
  for (float scale : {0.5f, 1.0f, 2.0f}) {
    auto reference = ShapeUtils::GetContourPath(transformed, scale);
    ASSERT_TRUE(trim->filterPath(&reference));
    auto actual = ShapeUtils::GetContourPath(effected, scale);
    EXPECT_FLOAT_EQ(DiagnosticLength(actual), DiagnosticLength(reference));
    Stroke stroke(2.0f);
    ASSERT_TRUE(stroke.applyToPath(&reference));
    ASSERT_TRUE(stroke.applyToPath(&actual));
    EXPECT_EQ(DiagnosticInteriorDifferences(actual, reference), 0);
  }
}

TGFX_TEST(TypefaceTest, OrderedSourcesKeepPerGlyphRSXform) {
  auto typeface = MakeRingTypeface(PathFillType::EvenOdd);
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 1000.0f);
  TextBlobBuilder builder;
  auto run = builder.allocRunRSXform(font, 2);
  run.glyphs[0] = 1;
  run.glyphs[1] = 1;
  float values[] = {0.8f, 0.6f, 50.0f, 20.0f, 0.6f, -0.8f, 1800.0f, 120.0f};
  for (size_t i = 0; i < 8; ++i) {
    run.positions[i] = values[i];
  }
  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);
  auto shape = Shape::MakeFrom(blob);
  std::vector<Path> sources = {};
  ASSERT_TRUE(ShapeUtils::GetPathSources(shape, 1.0f, &sources));
  ASSERT_EQ(sources.size(), 2u);
  size_t sourceIndex = 0;
  for (auto glyphRun : *blob) {
    for (size_t index = 0; index < glyphRun.glyphCount; ++index) {
      Path expected = {};
      ASSERT_TRUE(glyphRun.font.getPath(glyphRun.glyphs[index], &expected));
      expected.transform(GetGlyphMatrix(glyphRun, index));
      EXPECT_EQ(sources[sourceIndex], expected);
      EXPECT_EQ(sources[sourceIndex].getFillType(), PathFillType::EvenOdd);
      ++sourceIndex;
    }
  }
  auto trim = PathEffect::MakeTrim(0.2f, 0.8f);
  auto expected = ShapeUtils::GetContourPath(shape, 1.0f);
  ASSERT_TRUE(trim->filterPath(&expected));
  auto actual = ShapeUtils::GetContourPath(Shape::ApplyEffect(shape, trim), 1.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(actual), DiagnosticLength(expected));
  Stroke stroke(2.0f);
  ASSERT_TRUE(stroke.applyToPath(&actual));
  ASSERT_TRUE(stroke.applyToPath(&expected));
  EXPECT_EQ(DiagnosticInteriorDifferences(actual, expected), 0);
}

TGFX_TEST(TypefaceTest, AreaUnionKeepsNonIntersectingGlyphsWithOverlappingBounds) {
  auto glyphs = DiagnosticGlyphs("TGFX", 50.0f, true, "resources/font/NotoSerifSC-Regular.otf");
  ASSERT_EQ(glyphs.size(), 4u);
  ASSERT_TRUE(Rect::Intersects(glyphs[2].getBounds(), glyphs[3].getBounds()));
  Path intersection = glyphs[2];
  ASSERT_TRUE(intersection.addPath(glyphs[3], PathOp::Intersect));
  ASSERT_TRUE(intersection.isEmpty());
  auto expected = glyphs.front();
  for (size_t i = 1; i < glyphs.size(); ++i) {
    ASSERT_TRUE(expected.addPath(glyphs[i]));
  }
  auto merged = Path::Merge(glyphs);
  ASSERT_TRUE(merged.has_value());
  auto result = std::move(*merged);
  EXPECT_EQ(result.getFillType(), PathFillType::Winding);
  EXPECT_EQ(result, expected);
}

TGFX_TEST(TypefaceTest, AreaUnionKeepsVerifiedWindingOverlapContours) {
  Path a = {};
  a.addRect(Rect::MakeLTRB(0.0f, 0.0f, 300.0f, 300.0f));
  Path b = {};
  b.addRect(Rect::MakeLTRB(200.0f, 0.0f, 500.0f, 300.0f));
  auto expected = a;
  ASSERT_TRUE(expected.addPath(b));
  auto merged = Path::Merge({a, b});
  ASSERT_TRUE(merged.has_value());
  auto result = std::move(*merged);
  EXPECT_EQ(result, expected);
  EXPECT_EQ(result.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(result.contains(250.0f, 150.0f));
  EXPECT_FALSE(result.contains(600.0f, 150.0f));

  a.setFillType(PathFillType::EvenOdd);
  b.setFillType(PathFillType::EvenOdd);
  auto evenOddMerged = Path::Merge({a, b});
  ASSERT_TRUE(evenOddMerged.has_value());
  result = std::move(*evenOddMerged);
  EXPECT_EQ(result.getFillType(), PathFillType::EvenOdd);
  EXPECT_TRUE(result.contains(250.0f, 150.0f));
  EXPECT_FLOAT_EQ(DiagnosticLength(result), 1600.0f);
}

TGFX_TEST(TypefaceTest, FauxBoldEvenOddMaskKeepsStrokeOverlapFilled) {
  Path path = {};
  path.addRect(Rect::MakeLTRB(2.0f, 2.0f, 23.0f, 23.0f));
  path.addRect(Rect::MakeLTRB(7.0f, 7.0f, 18.0f, 18.0f));
  path.setFillType(PathFillType::EvenOdd);
  PathTypefaceBuilder builder(25);
  ASSERT_NE(builder.addGlyph(path, 25.0f), 0);
  auto typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 50.0f);
  font.setFauxBold(true);
  Matrix matrix = {};
  auto image = font.getImage(1, nullptr, &matrix);
  ASSERT_TRUE(image != nullptr);
  auto info = ImageInfo::Make(image->width(), image->height(), ColorType::ALPHA_8);
  std::vector<uint8_t> pixels(info.byteSize(), 0);
  ASSERT_TRUE(image->readPixels(info, pixels.data()));
  auto boundaryX = static_cast<int>(4.0f - matrix.getTranslateX());
  auto sampleY = static_cast<int>(25.0f - matrix.getTranslateY());
  auto centerX = static_cast<int>(25.0f - matrix.getTranslateX());
  ASSERT_GE(boundaryX, 0);
  ASSERT_GE(sampleY, 0);
  ASSERT_GE(centerX, 0);
  ASSERT_LT(boundaryX, image->width());
  ASSERT_LT(centerX, image->width());
  ASSERT_LT(sampleY, image->height());
  auto row = static_cast<size_t>(sampleY) * info.rowBytes();
  EXPECT_GT(pixels[row + static_cast<size_t>(boundaryX)], 200);
  EXPECT_EQ(pixels[row + static_cast<size_t>(centerX)], 0);
  Path outline = {};
  ASSERT_TRUE(font.getPath(1, &outline));
  EXPECT_TRUE(outline.contains(4.0f, 25.0f));
  EXPECT_FALSE(outline.contains(25.0f, 25.0f));
}

namespace {

class InverseFillEffect final : public PathEffect {
 public:
  bool filterPath(Path* path) const override {
    if (path == nullptr) {
      return false;
    }
    path->toggleInverseFillType();
    return true;
  }
};

}  // namespace

TGFX_TEST(TypefaceTest, FillTypeTrimUsesOneContourIntervalForFillAndStroke) {
  Path a = {};
  a.addRect(Rect::MakeLTRB(0.0f, 0.0f, 300.0f, 300.0f));
  a.setFillType(PathFillType::EvenOdd);
  Path b = {};
  b.addRect(Rect::MakeLTRB(200.0f, 0.0f, 500.0f, 300.0f));
  b.setFillType(PathFillType::EvenOdd);
  PathTypefaceBuilder builder(1000);
  ASSERT_NE(builder.addGlyph(a, 1000.0f), 0);
  ASSERT_NE(builder.addGlyph(b, 1000.0f), 0);
  auto typeface = builder.detach();
  GlyphID ids[] = {1, 2};
  Point positions[] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
  auto text = Shape::MakeFrom(TextBlob::MakeFrom(ids, positions, 2, Font(typeface, 1000.0f)));
  ASSERT_TRUE(text != nullptr);
  EXPECT_FLOAT_EQ(DiagnosticLength(ShapeUtils::GetContourPath(text, 1.0f)), 2400.0f);
  auto boundary = Shape::ApplyFillType(text, PathFillType::Winding);
  auto expected = boundary->getPath();
  EXPECT_FLOAT_EQ(DiagnosticLength(expected), 1600.0f);
  auto trim = PathEffect::MakeTrim(0.0f, 0.5f);
  ASSERT_TRUE(trim->filterPath(&expected));
  auto trimmed = Shape::ApplyEffect(boundary, trim);
  auto fillPath = trimmed->getPath();
  auto contourPath = ShapeUtils::GetContourPath(trimmed, 1.0f);
  EXPECT_EQ(fillPath, expected);
  EXPECT_EQ(contourPath, expected);
  EXPECT_FLOAT_EQ(DiagnosticLength(fillPath), 800.0f);
  EXPECT_FLOAT_EQ(DiagnosticLength(contourPath), 800.0f);
  Stroke stroke(2.0f);
  ASSERT_TRUE(stroke.applyToPath(&expected));
  auto stroked = Shape::ApplyStroke(trimmed, &stroke)->getPath();
  EXPECT_EQ(stroked, expected);
}

TGFX_TEST(TypefaceTest, CustomPathEffectKeepsItsSuccessfulFillTypeChange) {
  Path rectangle = {};
  rectangle.addRect(Rect::MakeLTRB(0.0f, 0.0f, 100.0f, 100.0f));
  auto effect = std::make_shared<InverseFillEffect>();
  auto direct = rectangle;
  ASSERT_TRUE(effect->filterPath(&direct));
  auto shape = Shape::ApplyEffect(Shape::MakeFrom(rectangle), effect);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::InverseWinding);
  auto actual = shape->getPath();
  EXPECT_EQ(actual, direct);
  EXPECT_FALSE(actual.contains(50.0f, 50.0f));
  EXPECT_TRUE(actual.contains(150.0f, 150.0f));
  auto contours = ShapeUtils::GetContourPath(shape, 1.0f);
  EXPECT_EQ(contours, direct);
  std::vector<Path> sources = {rectangle};
  ASSERT_TRUE(ShapeUtils::ApplyEffectToSources(*effect, &sources));
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources.front(), direct);
}

TGFX_TEST(TypefaceTest, BuiltInEffectsPreserveDeclaredRuleDirectlyAndInBatches) {
  float intervals[] = {8.0f, 4.0f};
  std::vector<std::shared_ptr<PathEffect>> effects = {PathEffect::MakeDash(intervals, 2, 0.0f),
                                                      PathEffect::MakeCorner(5.0f)};
  for (const auto& effect : effects) {
    ASSERT_TRUE(effect != nullptr);
    for (auto fillType : {PathFillType::Winding, PathFillType::EvenOdd,
                          PathFillType::InverseWinding, PathFillType::InverseEvenOdd}) {
      auto source = MakeRingPath(fillType);
      auto direct = source;
      ASSERT_TRUE(effect->filterPath(&direct));
      EXPECT_EQ(direct.getFillType(), fillType);
      std::vector<Path> sources = {source};
      ASSERT_TRUE(ShapeUtils::ApplyEffectToSources(*effect, &sources));
      ASSERT_EQ(sources.size(), 1u);
      EXPECT_EQ(sources.front(), direct);
      auto shape = Shape::ApplyEffect(Shape::MakeFrom(source), effect);
      ASSERT_TRUE(shape != nullptr);
      EXPECT_EQ(shape->getPath(), direct);
    }
  }
}

namespace {

class DefaultEvenOddProvider final : public PathProvider {
 public:
  Path getPath() const override {
    return MakeRingPath(PathFillType::EvenOdd);
  }

  Rect getBounds() const override {
    return Rect::MakeWH(1000.0f, 1000.0f);
  }
};

}  // namespace

TGFX_TEST(TypefaceTest, ProviderDefaultReportsComputedPathRule) {
  auto provider = std::make_shared<DefaultEvenOddProvider>();
  EXPECT_EQ(provider->fillType(), PathFillType::EvenOdd);
  auto shape = Shape::MakeFrom(provider);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  EXPECT_FALSE(shape->getPath().contains(500.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, AppendReportsRuleOfAnEmptyInverseFirstPath) {
  Path empty = {};
  empty.setFillType(PathFillType::InverseWinding);
  auto first = Shape::MakeFrom(empty);
  auto second = Shape::MakeFrom(MakeRingPath(PathFillType::EvenOdd));
  auto appended = Shape::Merge(first, second, PathOp::Append);
  ASSERT_TRUE(appended != nullptr);
  EXPECT_EQ(appended->fillType(), PathFillType::InverseWinding);
  EXPECT_EQ(appended->getPath().getFillType(), PathFillType::InverseWinding);
  EXPECT_TRUE(appended->getPath().contains(5000.0f, 5000.0f));
  EXPECT_FALSE(appended->getPath().contains(500.0f, 500.0f));
  std::vector<std::shared_ptr<Shape>> shapes = {nullptr, nullptr};
  EXPECT_EQ(Shape::Merge(shapes), nullptr);
}

namespace {

class MismatchedRuleProvider final : public PathProvider {
 public:
  Path getPath() const override {
    return MakeRingPath(PathFillType::EvenOdd);
  }

  Rect getBounds() const override {
    return Rect::MakeWH(1000.0f, 1000.0f);
  }

  PathFillType fillType() const override {
    return PathFillType::Winding;
  }
};

class CountingPathProvider final : public PathProvider {
 public:
  explicit CountingPathProvider(Path path) : path(std::move(path)) {
  }

  Path getPath() const override {
    // The count is reached through a shared owner so it stays writable from a const method without
    // the member itself being mutable: getPath() is const, so a plain counter could not be bumped.
    ++(*calls);
    return path;
  }

  Rect getBounds() const override {
    return path.getBounds();
  }

  std::shared_ptr<int> calls = std::make_shared<int>(0);

 private:
  Path path = {};
};

}  // namespace

TGFX_TEST(TypefaceTest, ProviderShapeUsesOutlineDespiteIncorrectMetadata) {
  auto provider = std::make_shared<MismatchedRuleProvider>();
  EXPECT_EQ(provider->fillType(), PathFillType::Winding);
  auto shape = Shape::MakeFrom(provider);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(shape->getPath().getFillType(), PathFillType::EvenOdd);
  EXPECT_FALSE(shape->getPath().contains(500.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, AppendRuleQueryDoesNotGenerateTrailingContours) {
  // A plain empty source contributes nothing and is skipped; an inverse one would fill the whole
  // plane and stop the scan here instead.
  Path empty = {};
  auto emptyProvider = std::make_shared<CountingPathProvider>(empty);
  auto firstProvider = std::make_shared<CountingPathProvider>(MakeRingPath(PathFillType::EvenOdd));
  auto trailingProvider =
      std::make_shared<CountingPathProvider>(MakeRingPath(PathFillType::Winding));
  auto shape = Shape::Merge({Shape::MakeFrom(emptyProvider), Shape::MakeFrom(firstProvider),
                             Shape::MakeFrom(trailingProvider)});
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(*emptyProvider->calls, 1);
  EXPECT_EQ(*firstProvider->calls, 1);
  EXPECT_EQ(*trailingProvider->calls, 0);
  EXPECT_EQ(shape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(*emptyProvider->calls, 1);
  EXPECT_EQ(*firstProvider->calls, 1);
  EXPECT_EQ(*trailingProvider->calls, 0);
  EXPECT_EQ(shape->getPath().getFillType(), PathFillType::EvenOdd);
  EXPECT_EQ(*trailingProvider->calls, 1);
}

// The review measured 300 disjoint even-odd glyphs at 137.75 ms, against 9.30 ms for the same
// geometry under Winding: after the first union the accumulated path is no longer Winding, so every
// later glyph took a boolean operation even though the bounds never met. Disjoint sources of one
// rule now keep their contours, which is what this guards. The bound is far above the measured cost
// so it only catches the quadratic behaviour, not ordinary machine noise.
// The counterpart of the case above: glyphs that do overlap have to go through the boolean pass,
// and the check that decides whether their contours can be kept is the expensive part there. The
// bound is again far above the measured cost, so it catches the path being taken on a run whose
// size makes it quadratic, not ordinary noise.
TGFX_TEST(TypefaceTest, OverlappingGlyphMergeIsNotQuadratic) {
  for (auto rule : {PathFillType::Winding, PathFillType::EvenOdd}) {
    Path glyph = {};
    glyph.addRect(Rect::MakeWH(30.0f, 30.0f));
    glyph.setFillType(rule);
    std::vector<Path> sources = {};
    for (int index = 0; index < 200; ++index) {
      auto path = glyph;
      path.transform(Matrix::MakeTrans(static_cast<float>(index % 20) * 10.0f,
                                       static_cast<float>(index / 20) * 10.0f));
      sources.push_back(std::move(path));
    }
    auto start = std::chrono::steady_clock::now();
    auto merged = Path::Merge(sources);
    ASSERT_TRUE(merged.has_value());
    auto result = std::move(*merged);
    auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(elapsed, 400.0);
    EXPECT_TRUE(result.contains(15.0f, 15.0f));
  }
}

TGFX_TEST(TypefaceTest, DisjointGlyphMergeIsNotQuadratic) {
  for (auto rule : {PathFillType::Winding, PathFillType::EvenOdd}) {
    Path glyph = {};
    glyph.addRect(Rect::MakeWH(10.0f, 10.0f));
    glyph.setFillType(rule);
    std::vector<Path> sources = {};
    sources.reserve(300);
    for (int index = 0; index < 300; ++index) {
      auto path = glyph;
      path.transform(Matrix::MakeTrans(static_cast<float>(index) * 20.0f, 0.0f));
      sources.push_back(std::move(path));
    }
    auto start = std::chrono::steady_clock::now();
    auto merged = Path::Merge(sources);
    ASSERT_TRUE(merged.has_value());
    auto result = std::move(*merged);
    auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    printf("[PERF] 300 disjoint glyphs rule=%d: %.3f ms\n", static_cast<int>(rule), elapsed);
    EXPECT_LT(elapsed, 20.0);
    EXPECT_EQ(result.getFillType(), rule);
    EXPECT_TRUE(result.contains(5.0f, 5.0f));
  }
}

TGFX_TEST(TypefaceTest, DisjointAreaScanPreservesInputContourOrder) {
  for (auto rule : {PathFillType::Winding, PathFillType::EvenOdd}) {
    for (bool vertical : {false, true}) {
      Path rectangle = {};
      rectangle.addRect(Rect::MakeWH(10.0f, 10.0f));
      rectangle.setFillType(rule);
      std::vector<Path> sources = {};
      Path expected = {};
      for (int index = 2999; index >= 0; --index) {
        auto offset = static_cast<float>(index) * 20.0f;
        auto path = rectangle;
        path.transform(Matrix::MakeTrans(vertical ? 0.0f : offset, vertical ? offset : 0.0f));
        ASSERT_TRUE(expected.addPath(path));
        sources.push_back(std::move(path));
      }
      auto merged = Path::Merge(sources);
      ASSERT_TRUE(merged.has_value());
      auto result = std::move(*merged);
      EXPECT_EQ(result.getFillType(), rule);
      EXPECT_EQ(result, expected);
      EXPECT_TRUE(result.contains(5.0f, 5.0f));
      EXPECT_FALSE(result.contains(vertical ? 5.0f : 15.0f, vertical ? 15.0f : 5.0f));
    }
  }
}

TGFX_TEST(TypefaceTest, AreaScanChecksAllActiveBoundsAndEqualStarts) {
  for (bool vertical : {false, true}) {
    Path first = {};
    first.addRect(Rect::MakeLTRB(0.0f, 0.0f, 100.0f, 10.0f));
    Path middle = {};
    middle.addRect(Rect::MakeLTRB(20.0f, 20.0f, 30.0f, 30.0f));
    Path last = {};
    last.addRect(Rect::MakeLTRB(90.0f, 0.0f, 110.0f, 10.0f));
    std::vector<Path> sources = {last, middle, first};
    for (auto& source : sources) {
      source.setFillType(PathFillType::EvenOdd);
      if (vertical) {
        source.transform(Matrix::MakeRotate(90.0f));
      }
    }
    auto merged = Path::Merge(sources);
    ASSERT_TRUE(merged.has_value());
    auto result = std::move(*merged);
    EXPECT_TRUE(result.contains(vertical ? -5.0f : 95.0f, vertical ? 95.0f : 5.0f));
    EXPECT_TRUE(result.contains(vertical ? -25.0f : 25.0f, 25.0f));
  }
  auto ring = MakeRingPath(PathFillType::EvenOdd);
  Path island = {};
  island.addRect(Rect::MakeLTRB(300.0f, 300.0f, 700.0f, 700.0f));
  island.setFillType(PathFillType::EvenOdd);
  auto expected = ring;
  ASSERT_TRUE(expected.addPath(island));
  auto merged = Path::Merge({island, ring});
  ASSERT_TRUE(merged.has_value());
  auto result = std::move(*merged);
  EXPECT_TRUE(result.contains(500.0f, 500.0f));
  EXPECT_FALSE(result.contains(250.0f, 500.0f));
  auto difference = result;
  ASSERT_TRUE(difference.addPath(expected, PathOp::XOR));
  EXPECT_TRUE(difference.isEmpty());
  Path sameStart = {};
  sameStart.addRect(Rect::MakeLTRB(0.0f, 0.0f, 500.0f, 1000.0f));
  sameStart.setFillType(PathFillType::EvenOdd);
  auto extended = Path::Merge({result, sameStart});
  ASSERT_TRUE(extended.has_value());
  result = std::move(*extended);
  EXPECT_TRUE(result.contains(250.0f, 500.0f));
  EXPECT_FALSE(result.contains(750.0f, 500.0f));
}

TGFX_TEST(TypefaceTest, AppendAllEmptyPathsUsesLastSourceRule) {
  Path firstPath = {};
  firstPath.setFillType(PathFillType::EvenOdd);
  Path lastPath = {};
  lastPath.setFillType(PathFillType::InverseWinding);
  auto first = std::make_shared<CountingPathProvider>(firstPath);
  auto last = std::make_shared<CountingPathProvider>(lastPath);
  auto shape = Shape::Merge({Shape::MakeFrom(first), Shape::MakeFrom(last)});
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::InverseWinding);
  EXPECT_EQ(shape->fillType(), PathFillType::InverseWinding);
  EXPECT_EQ(*first->calls, 1);
  EXPECT_EQ(*last->calls, 1);
  EXPECT_TRUE(shape->getPath().isEmpty());
  EXPECT_EQ(shape->getPath().getFillType(), PathFillType::InverseWinding);
}

// isInverseFillType() is the one question a run can settle without merging its outlines, which is
// what keeps that merge off the recording thread when OpsCompositor asks about it. It still has to
// agree with the merged path: the cheap answer is inferred from the glyphs, so a disagreement would
// mean the inference behind it is wrong. It is asked before getPath() on purpose -- afterwards the
// answer would be read off a path that is already cached and the test would prove nothing.
TGFX_TEST(TypefaceTest, TextRunAnswersInverseQuestionWithoutMerging) {
  struct RunCase {
    std::vector<PathFillType> rules = {};
    bool inverse = false;
  };
  std::vector<RunCase> cases = {
      {{PathFillType::Winding, PathFillType::Winding}, false},
      {{PathFillType::EvenOdd, PathFillType::EvenOdd}, false},
      {{PathFillType::Winding, PathFillType::EvenOdd}, false},
      {{PathFillType::InverseEvenOdd, PathFillType::EvenOdd}, true},
      {{PathFillType::EvenOdd, PathFillType::InverseWinding}, true},
  };
  for (size_t caseIndex = 0; caseIndex < cases.size(); caseIndex++) {
    const auto& runCase = cases[caseIndex];
    auto context = "case " + std::to_string(caseIndex);
    PathTypefaceBuilder builder(1000);
    for (const auto& rule : runCase.rules) {
      ASSERT_NE(builder.addGlyph(MakeRectPath(rule), 1000.0f), 0) << context;
    }
    auto typeface = builder.detach();
    ASSERT_TRUE(typeface != nullptr) << context;
    Font font(typeface, 1000.0f);
    std::vector<GlyphID> glyphs = {};
    std::vector<Point> positions = {};
    for (size_t index = 0; index < runCase.rules.size(); index++) {
      glyphs.push_back(static_cast<GlyphID>(index + 1));
      positions.push_back(Point::Make(static_cast<float>(index) * 1200.0f, 0.0f));
    }
    auto blob = TextBlob::MakeFrom(glyphs.data(), positions.data(), glyphs.size(), font);
    ASSERT_TRUE(blob != nullptr) << context;
    auto shape = Shape::MakeFrom(blob);
    ASSERT_TRUE(shape != nullptr) << context;
    EXPECT_EQ(shape->isInverseFillType(), runCase.inverse) << context;
    EXPECT_EQ(shape->getPath().isInverseFillType(), runCase.inverse) << context;
  }
}

}  // namespace tgfx
