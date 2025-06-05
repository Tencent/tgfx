/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/utils/MathExtra.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/TextLayer.h"
#include "utils/TestUtils.h"

namespace tgfx {
class CustomPathGlyphFace : public GlyphFace {
 public:
  explicit CustomPathGlyphFace(float size = 1.0f) : _size(size) {
  }

  bool hasColor() const override {
    return false;
  }
  bool hasOutlines() const override {
    return true;
  }

  std::shared_ptr<GlyphFace> makeWithSize(float size) const override {
    return std::make_shared<CustomPathGlyphFace>(size);
  }

  bool getPath(GlyphID glyphID, Path* path) const override {
    switch (glyphID) {
      case 1:
        path->moveTo(Point::Make(25.0f, 5.0f) * _size);
        path->lineTo(Point::Make(45.0f, 45.0f) * _size);
        path->lineTo(Point::Make(5.0f, 45.0f) * _size);
        path->close();
        return true;
      case 2:
        path->moveTo(Point::Make(5.0f, 5.0f) * _size);
        path->lineTo(Point::Make(45.0f, 5.0f) * _size);
        path->lineTo(Point::Make(45.0f, 45.0f) * _size);
        path->lineTo(Point::Make(5.0f, 45.0f) * _size);
        path->close();
        return true;
      case 3: {
        Rect rect = Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
        rect.scale(_size, _size);
        path->addOval(rect);
        path->close();
        return true;
      }
      case 100: {
        Rect rect = Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f);
        rect.scale(_size, _size);
        path->addRect(rect);
        path->close();
        return true;
      }
      default:
        return false;
    }
  }

  std::shared_ptr<ImageCodec> getImage(GlyphID /*glyphID*/, const Stroke* /*stroke*/,
                                       Matrix* /*matrix*/) const override {
    return nullptr;
  }

  Rect getBounds(GlyphID glyphID) const override {
    if (glyphID == 100) {
      return Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f);
    }

    if (glyphID < 1 || glyphID > 3) {
      return {};
    }
    Rect bounds = Rect::MakeXYWH(50 * (glyphID - 1), 0, 50, 50);
    bounds.scale(_size, _size);
    return bounds;
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

  float getSize() const override {
    return _size;
  }

  uint32_t getTypefaceID() const override {
    return 0;
  }

 private:
  float _size = 1.0f;
};

class CustomImageGlyphFace : public GlyphFace {
 public:
  explicit CustomImageGlyphFace(float size = 1.0f) : _size(size) {
  }

  bool hasColor() const override {
    return true;
  }

  bool hasOutlines() const override {
    return false;
  }

  std::shared_ptr<GlyphFace> makeWithSize(float size) const override {
    return std::make_shared<CustomImageGlyphFace>(size);
  }

  bool getPath(GlyphID /*glyphID*/, Path* /*path*/) const override {
    return false;
  }

  std::shared_ptr<ImageCodec> getImage(GlyphID glyphID, const Stroke*,
                                       Matrix* matrix) const override {
    std::string imagePath;
    switch (glyphID) {
      case 4:
        imagePath = "resources/assets/glyph1.png";
        break;
      case 5:
        imagePath = "resources/assets/glyph2.png";
        break;
      case 6:
        imagePath = "resources/assets/glyph3.png";
        break;
      default:
        return nullptr;
    }

    matrix->setScale(0.25f * _size, 0.25f * _size);
    return ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  }

  Rect getBounds(GlyphID glyphID) const override {
    if (glyphID < 3 || glyphID > 6) {
      return {};
    }
    Rect bounds = Rect::MakeXYWH(50 * (glyphID - 1), 0, 50, 50);
    bounds.scale(_size, _size);
    return bounds;
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

  float getSize() const override {
    return _size;
  }

  uint32_t getTypefaceID() const override {
    return 1;
  }

 private:
  float _size = 1.0f;
};

TGFX_TEST(GlyphFaceTest, GlyphFaceSimple) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  float scaleFactor = 1.0f;
  canvas->scale(scaleFactor, scaleFactor);

  std::vector<GlyphID> glyphIDs1 = {1, 2, 3};
  std::vector<Point> positions1 = {};
  positions1.push_back(Point::Make(0.0f, 0.0f));
  positions1.push_back(Point::Make(50.0f, 0.0f));
  positions1.push_back(Point::Make(100.0f, 0.0f));
  canvas->drawGlyphs(glyphIDs1.data(), positions1.data(), glyphIDs1.size(),
                     std::make_shared<CustomPathGlyphFace>(), paint);

  std::vector<GlyphID> glyphIDs2 = {4, 5, 6};
  std::vector<Point> positions2 = {};
  positions2.push_back(Point::Make(150.0f, 0.0f));
  positions2.push_back(Point::Make(205.0f, 0.0f));
  positions2.push_back(Point::Make(260.0f, 0.0f));
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(),
                     std::make_shared<CustomImageGlyphFace>(), paint);

  EXPECT_TRUE(Baseline::Compare(surface, "GlyphFaceTest/GlyphFaceSimple"));
}

class CustomPathGlyphFace2 : public GlyphFace, std::enable_shared_from_this<CustomPathGlyphFace2> {
 public:
  CustomPathGlyphFace2(std::shared_ptr<Typeface> typeface, float size)
      : _size(size), font20(typeface, 20 * size), font40(typeface, 40 * size),
        font60(typeface, 60 * size) {
  }

  bool hasColor() const override {
    return false;
  }
  bool hasOutlines() const override {
    return true;
  }
  std::shared_ptr<GlyphFace> makeWithSize(float size) const override {
    return std::make_shared<CustomPathGlyphFace2>(font20.getTypeface(), size);
  }

  bool getPath(GlyphID glyphID, Path* path) const override {
    if (glyphID == 8699 || glyphID == 16266) {
      return font40.getPath(glyphID, path);
    }
    if (glyphID == 16671 || glyphID == 24458) {
      return font60.getPath(glyphID, path);
    }
    return font20.getPath(glyphID, path);
  }

  std::shared_ptr<ImageCodec> getImage(GlyphID /*glyphID*/, const Stroke* /*stroke*/,
                                       Matrix* /*matrix*/) const override {
    return nullptr;
  }

  Rect getBounds(GlyphID glyphID) const override {
    if (glyphID == 8699 || glyphID == 16266) {
      return font40.getBounds(glyphID);
    }
    if (glyphID == 16671 || glyphID == 24458) {
      return font60.getBounds(glyphID);
    }
    return font20.getBounds(glyphID);
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

  float getSize() const override {
    return _size;
  }

  uint32_t getTypefaceID() const override {
    return 2;
  }

 private:
  float _size = 1.0f;
  Font font20 = {};
  Font font40 = {};
  Font font60 = {};
};

class CustomImageGlyphFace2 : public GlyphFace,
                              std::enable_shared_from_this<CustomImageGlyphFace2> {
 public:
  CustomImageGlyphFace2(std::shared_ptr<Typeface> typeface, float size)
      : _size(size), fontEmoji(typeface, 50 * _size) {
  }

  bool hasColor() const override {
    return true;
  }
  bool hasOutlines() const override {
    return false;
  }
  std::shared_ptr<GlyphFace> makeWithSize(float size) const override {
    return std::make_shared<CustomImageGlyphFace2>(fontEmoji.getTypeface(), size);
  }

  bool getPath(GlyphID /*glyphID*/, Path* /*path*/) const override {
    return false;
  }

  std::shared_ptr<ImageCodec> getImage(GlyphID glyphID, const Stroke* stroke,
                                       Matrix* matrix) const override {
    return fontEmoji.getImage(glyphID, stroke, matrix);
  }

  Rect getBounds(GlyphID glyphID) const override {
    return fontEmoji.getBounds(glyphID);
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

  float getSize() const override {
    return _size;
  }

  uint32_t getTypefaceID() const override {
    return 3;
  }

 private:
  float _size = 1.0f;
  Font fontEmoji = {};
};

TGFX_TEST(GlyphFaceTest, GlyphFaceWithStyle) {
  auto typeface1 = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font20(typeface1, 20);
  Font font40(typeface1, 40);
  Font font60(typeface1, 60);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 200);
  auto canvas = surface->getCanvas();
  ASSERT_TRUE(canvas != nullptr);

  // GlyphID:25483 14857 8699 16266 16671 24458 14689 15107 29702 41 70 77 77 80 29702 53 40 39 57 95
  // Text:这是一段测试文本，Hello，TGFX~
  auto glyphFace1 = std::make_shared<CustomPathGlyphFace2>(typeface1, 1.0f);

  float scaleFactor = 1.0f;
  canvas->scale(scaleFactor, scaleFactor);

  auto paint = Paint();
  paint.setColor(Color::Red());
  std::vector<GlyphID> glyphIDs1 = {25483, 14857};
  std::vector<Point> positions1 = {};
  positions1.push_back(Point::Make(0.0f, 100.0f));
  positions1.push_back(Point::Make(20.0f, 100.0f));
  canvas->drawGlyphs(glyphIDs1.data(), positions1.data(), glyphIDs1.size(), glyphFace1, paint);

  paint.setColor(Color::Green());
  std::vector<GlyphID> glyphIDs2 = {8699, 16266};
  std::vector<Point> positions2 = {};
  positions2.push_back(Point::Make(40.0f, 100.0f));
  positions2.push_back(Point::Make(80.0f, 100.0f));
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(), glyphFace1, paint);

  paint.setColor(Color::Blue());
  std::vector<GlyphID> glyphIDs3 = {16671, 24458};
  std::vector<Point> positions3 = {};
  positions3.push_back(Point::Make(120.0f, 100.0f));
  positions3.push_back(Point::Make(180.0f, 100.0f));
  canvas->drawGlyphs(glyphIDs3.data(), positions3.data(), glyphIDs3.size(), glyphFace1, paint);

  paint.setColor(Color::FromRGBA(255, 0, 255, 255));
  std::vector<GlyphID> glyphIDs4 = {14689, 15107, 29702, 41, 70, 77, 77,
                                    80,    29702, 53,    40, 39, 57, 95};
  std::vector<Point> positions4 = {};
  float advance = 240.0f;
  positions4.push_back(Point::Make(advance, 100.0f));
  for (size_t i = 1; i < glyphIDs4.size(); ++i) {
    advance += font20.getAdvance(glyphIDs4[i - 1]) / scaleFactor;
    positions4.push_back(Point::Make(advance, 100.0f));
  }
  canvas->drawGlyphs(glyphIDs4.data(), positions4.data(), glyphIDs4.size(), glyphFace1, paint);

  auto typeface2 =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  Font fontEmoji(typeface2, 50);

  // 1109 886 1110 888
  // 🤩😃🤪😅
  std::vector<GlyphID> emojiGlyphIDs = {1109, 886, 1110, 888};
  std::vector<Point> emojiPositions = {};
  emojiPositions.push_back(Point::Make(450.0f, 100.0f));
  emojiPositions.push_back(Point::Make(510.0f, 100.0f));
  emojiPositions.push_back(Point::Make(570.0f, 100.0f));
  emojiPositions.push_back(Point::Make(630.0f, 100.0f));
  canvas->drawGlyphs(emojiGlyphIDs.data(), emojiPositions.data(), emojiGlyphIDs.size(),
                     std::make_shared<CustomImageGlyphFace2>(typeface2, 1.0f), paint);

  EXPECT_TRUE(Baseline::Compare(surface, "GlyphFaceTest/GlyphFaceWithStyle"));
}

TGFX_TEST(GlyphFaceTest, MakeTextBlobWithGlyphFace) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 100);
  Font fontText(typeface, 20);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 720);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();

  // text: 找个地方吃饭
  // GlyphID: 13917 8741 11035 14739 10228 27929
  GlyphID glyphIDs[] = {13917, 8741, 11035, 14739, 10228, 27929};
  Point positions[] = {Point::Make(150.0f, 150.0f), Point::Make(250.0f, 150.0f),
                       Point::Make(350.0f, 150.0f), Point::Make(450.0f, 150.0f),
                       Point::Make(550.0f, 150.0f), Point::Make(650.0f, 150.0f)};
  auto textBlob = TextBlob::MakeFrom(glyphIDs, positions, 6, GlyphFace::Wrap(font));
  auto textShape = Shape::MakeFrom(textBlob);

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setColor(Color::Red());

  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setShape(textShape);
  auto strokeStyle1 = SolidColor::Make(Color::White());
  shapeLayer1->setStrokeStyle(strokeStyle1);
  shapeLayer1->setLineWidth(5.0f);
  shapeLayer1->setStrokeAlign(StrokeAlign::Outside);
  auto fillStyle1 = SolidColor::Make(Color::Blue());
  shapeLayer1->setFillStyle(fillStyle1);
  rootLayer->addChild(shapeLayer1);

  auto shapeLayer2 = ShapeLayer::Make();
  shapeLayer2->setMatrix(Matrix::MakeTrans(0.0f, 150.0f));
  shapeLayer2->setShape(textShape);
  auto strokeStyle2 = SolidColor::Make(Color::White());
  shapeLayer2->setStrokeStyle(strokeStyle2);
  shapeLayer2->setLineWidth(4.0f);
  shapeLayer2->setStrokeAlign(StrokeAlign::Center);
  auto fillStyle2 = SolidColor::Make(Color::Blue());
  shapeLayer2->setFillStyle(fillStyle2);
  rootLayer->addChild(shapeLayer2);

  auto shapeLayer3 = ShapeLayer::Make();
  shapeLayer3->setMatrix(Matrix::MakeTrans(0.0f, 300.0f));
  shapeLayer3->setShape(textShape);
  auto strokeStyle3 = SolidColor::Make(Color::White());
  shapeLayer3->setStrokeStyle(strokeStyle3);
  shapeLayer3->setLineWidth(2.0f);
  shapeLayer3->setStrokeAlign(StrokeAlign::Inside);
  shapeLayer3->setLineDashPattern({4.0f, 4.0f});
  shapeLayer3->setLineDashPhase(2.0f);
  auto fillStyle3 = SolidColor::Make(Color::Blue());
  shapeLayer3->setFillStyle(fillStyle3);
  rootLayer->addChild(shapeLayer3);

  auto textLayer1 = TextLayer::Make();
  textLayer1->setMatrix(Matrix::MakeTrans(30.0f, 100.0f));
  textLayer1->setTextColor(Color::Red());
  textLayer1->setText("外描边：");
  textLayer1->setFont(fontText);
  rootLayer->addChild(textLayer1);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(30.0f, 250.0f));
  textLayer2->setTextColor(Color::Red());
  textLayer2->setText("居中描边：");
  textLayer2->setFont(fontText);
  rootLayer->addChild(textLayer2);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(30.0f, 400.0f));
  textLayer3->setTextColor(Color::Red());
  textLayer3->setText("内描边：");
  textLayer3->setFont(fontText);
  rootLayer->addChild(textLayer3);

  // Rect
  std::vector<GlyphID> glyphIDs2 = {100};
  std::vector<Point> positions2 = {{0.0f, 0.0f}};
  auto textBlob2 = TextBlob::MakeFrom(glyphIDs2.data(), positions2.data(), 1,
                                      std::make_shared<CustomPathGlyphFace>());
  auto textShape2 = Shape::MakeFrom(textBlob2);

  auto shapeLayer4 = ShapeLayer::Make();
  shapeLayer4->setMatrix(Matrix::MakeTrans(150.0f, 550.0f));
  shapeLayer4->setShape(textShape2);
  auto strokeStyle4 = SolidColor::Make(Color::White());
  shapeLayer4->setStrokeStyle(strokeStyle4);
  shapeLayer4->setLineWidth(10.0f);
  shapeLayer4->setStrokeAlign(StrokeAlign::Outside);
  auto fillStyle4 = SolidColor::Make(Color::Blue());
  shapeLayer4->setFillStyle(fillStyle4);
  rootLayer->addChild(shapeLayer4);

  auto shapeLayer5 = ShapeLayer::Make();
  shapeLayer5->setMatrix(Matrix::MakeTrans(350.0f, 550.0f));
  shapeLayer5->setShape(textShape2);
  auto strokeStyle5 = SolidColor::Make(Color::White());
  shapeLayer5->setStrokeStyle(strokeStyle5);
  shapeLayer5->setLineWidth(10.0f);
  shapeLayer5->setStrokeAlign(StrokeAlign::Center);
  shapeLayer5->setLineDashPattern({10.0f, 10.0f});
  shapeLayer5->setLineDashPhase(5.0f);
  auto fillStyle5 = SolidColor::Make(Color::Blue());
  shapeLayer5->setFillStyle(fillStyle5);
  rootLayer->addChild(shapeLayer5);

  auto shapeLayer6 = ShapeLayer::Make();
  shapeLayer6->setMatrix(Matrix::MakeTrans(550.0f, 550.0f));
  shapeLayer6->setShape(textShape2);
  auto strokeStyle6 = SolidColor::Make(Color::White());
  shapeLayer6->setStrokeStyle(strokeStyle6);
  shapeLayer6->setLineWidth(10.0f);
  shapeLayer6->setStrokeAlign(StrokeAlign::Inside);
  shapeLayer6->setLineDashPattern({10.0f, 10.0f});
  shapeLayer6->setLineDashPhase(5.0f);
  auto fillStyle6 = SolidColor::Make(Color::Blue());
  shapeLayer6->setFillStyle(fillStyle6);
  rootLayer->addChild(shapeLayer6);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "GlyphFaceTest/MakeTextBlobWithGlyphFace"));
}
}  // namespace tgfx
