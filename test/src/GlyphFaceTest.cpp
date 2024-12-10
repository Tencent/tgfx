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
#include "utils/TestUtils.h"

namespace tgfx {
class CustomPathGlyphFace : public GlyphFace, std::enable_shared_from_this<CustomPathGlyphFace> {
 public:
  bool hasColor() const override {
    return false;
  }
  bool hasOutlines() const override {
    return false;
  }
  std::shared_ptr<GlyphFace> makeScaled(float scale) override {
    _scale = scale;
    return shared_from_this();
  }

  bool getPath(GlyphID glyphID, Path* path) const override {
    switch (glyphID) {
      case 1:
        path->moveTo(Point::Make(25.0f, 5.0f) * _scale);
        path->lineTo(Point::Make(45.0f, 45.0f) * _scale);
        path->lineTo(Point::Make(5.0f, 45.0f) * _scale);
        path->close();
        return true;
      case 2:
        path->moveTo(Point::Make(5.0f, 5.0f) * _scale);
        path->lineTo(Point::Make(45.0f, 5.0f) * _scale);
        path->lineTo(Point::Make(45.0f, 45.0f) * _scale);
        path->lineTo(Point::Make(5.0f, 45.0f) * _scale);
        path->close();
        return true;
      case 3: {
        Rect rect = Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
        rect.scale(_scale, _scale);
        path->addOval(rect);
        path->close();
        return true;
      }
      default:
        return false;
    }
  }

  std::shared_ptr<Image> getImage(GlyphID /*glyphID*/, Matrix* /*matrix*/) const override {
    return nullptr;
  }

  Rect getBounds(GlyphID glyphID) const override {
    if (glyphID < 1 || glyphID > 3) {
      return Rect::MakeEmpty();
    }
    Rect bounds = Rect::MakeXYWH(50 * (glyphID - 1), 0, 50, 50);
    bounds.scale(_scale, _scale);
    return bounds;
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

 private:
  float _scale = 1.0f;
};

class CustomImageGlyphFace : public GlyphFace, std::enable_shared_from_this<CustomImageGlyphFace> {
 public:
  bool hasColor() const override {
    return true;
  }
  bool hasOutlines() const override {
    return false;
  }
  std::shared_ptr<GlyphFace> makeScaled(float scale) override {
    if (FloatNearlyZero(scale)) {
      return nullptr;
    }
    _scale = scale;
    return shared_from_this();
  }

  bool getPath(GlyphID /*glyphID*/, Path* /*path*/) const override {
    return false;
  }

  std::shared_ptr<Image> getImage(GlyphID glyphID, Matrix* matrix) const override {
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

    matrix->setScale(0.25f * _scale, 0.25f * _scale);
    return Image::MakeFromFile(ProjectPath::Absolute(imagePath));
  }

  Rect getBounds(GlyphID glyphID) const override {
    if (glyphID < 3 || glyphID > 6) {
      return Rect::MakeEmpty();
    }
    Rect bounds = Rect::MakeXYWH(50 * (glyphID - 1), 0, 50, 50);
    bounds.scale(_scale, _scale);
    return bounds;
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

 private:
  float _scale = 1.0f;
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

auto typeface1 = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
Font font20(typeface1, 20);
Font font40(typeface1, 40);
Font font60(typeface1, 60);

auto typeface2 = Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
Font fontEmoji(typeface2, 50);

class CustomPathGlyphFace2 : public GlyphFace, std::enable_shared_from_this<CustomPathGlyphFace2> {
 public:
  bool hasColor() const override {
    return false;
  }
  bool hasOutlines() const override {
    return false;
  }
  std::shared_ptr<GlyphFace> makeScaled(float scale) override {
    if (FloatNearlyZero(scale)) {
      return nullptr;
    }

    if (!FloatNearlyEqual(scale, _scale)) {
      _scale = scale;
      font20 = font20.makeWithSize(font20.getSize() * scale);
      font40 = font40.makeWithSize(font40.getSize() * scale);
      font60 = font60.makeWithSize(font60.getSize() * scale);
    }

    return shared_from_this();
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

  std::shared_ptr<Image> getImage(GlyphID /*glyphID*/, Matrix* /*matrix*/) const override {
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

 private:
  float _scale = 1.0f;
};

class CustomImageGlyphFace2 : public GlyphFace,
                              std::enable_shared_from_this<CustomImageGlyphFace2> {
 public:
  bool hasColor() const override {
    return true;
  }
  bool hasOutlines() const override {
    return false;
  }
  std::shared_ptr<GlyphFace> makeScaled(float scale) override {
    if (FloatNearlyZero(scale)) {
      return nullptr;
    }

    if (!FloatNearlyEqual(scale, _scale)) {
      _scale = scale;
      fontEmoji = fontEmoji.makeWithSize(fontEmoji.getSize() * scale);
    }

    return shared_from_this();
  }

  bool getPath(GlyphID /*glyphID*/, Path* /*path*/) const override {
    return false;
  }

  std::shared_ptr<Image> getImage(GlyphID glyphID, Matrix* matrix) const override {
    return fontEmoji.getImage(glyphID, matrix);
  }

  Rect getBounds(GlyphID glyphID) const override {
    return fontEmoji.getBounds(glyphID);
  }

  bool asFont(Font* /*font*/) const override {
    return false;
  }

 private:
  float _scale = 1.0f;
};

TGFX_TEST(GlyphFaceTest, GlyphFaceWithStyle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 200);
  auto canvas = surface->getCanvas();
  ASSERT_TRUE(canvas != nullptr);

  // GlyphID:25483 14857 8699 16266 16671 24458 14689 15107 29702 41 70 77 77 80 29702 53 40 39 57 95
  // Text:这是一段测试文本，Hello，TGFX~
  auto glyphFace1 = std::make_shared<CustomPathGlyphFace2>();

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

  // 1109 886 1110 888
  // 🤩😃🤪😅
  std::vector<GlyphID> emojiGlyphIDs = {1109, 886, 1110, 888};
  std::vector<Point> emojiPositions = {};
  emojiPositions.push_back(Point::Make(450.0f, 100.0f));
  emojiPositions.push_back(Point::Make(510.0f, 100.0f));
  emojiPositions.push_back(Point::Make(570.0f, 100.0f));
  emojiPositions.push_back(Point::Make(630.0f, 100.0f));
  canvas->drawGlyphs(emojiGlyphIDs.data(), emojiPositions.data(), emojiGlyphIDs.size(),
                     std::make_shared<CustomImageGlyphFace2>(), paint);

  EXPECT_TRUE(Baseline::Compare(surface, "GlyphFaceTest/GlyphFaceWithStyle"));
}
}  // namespace tgfx
