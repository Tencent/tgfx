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
#include "core/codecs/png/PngCodec.h"
#include "tgfx/core/TypefaceProvider.h"
#include "tgfx/core/VirtualTypeface.h"
#include "utils/TestUtils.h"

namespace tgfx {
class CustomTypefaceProvider : public TypefaceProvider {
 public:
  bool getPath(const std::shared_ptr<Typeface>& /*typeface*/, GlyphID glyphID, bool /*fauxBold*/,
               bool /*fauxItalic*/, Path* path) const override {
    switch (glyphID) {
      case 1:
        path->moveTo(25.0f, 5.0f);
        path->lineTo(45.0f, 45.0f);
        path->lineTo(5.0f, 45.0f);
        path->close();
        return true;
      case 2:
        path->moveTo(5.0f, 5.0f);
        path->lineTo(45.0f, 5.0f);
        path->lineTo(45.0f, 45.0f);
        path->lineTo(5.0f, 45.0f);
        path->close();
        return true;
      case 3:
        path->addOval(Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f));
        path->close();
        return true;
      default:
        return false;
    }
  }

  std::shared_ptr<ImageBuffer> getImage(const std::shared_ptr<Typeface>& /*typeface*/,
                                        GlyphID glyphID, bool /*tryHardware*/) const override {
    std::string imagePath;
    switch (glyphID) {
      case 4:
        imagePath = "resources/assets/image1.png";
        break;
      case 5:
        imagePath = "resources/assets/image2.png";
        break;
      case 6:
        imagePath = "resources/assets/image3.png";
        break;
      default:
        return nullptr;
    }

    const auto imageCodec = PngCodec::MakeFrom(ProjectPath::Absolute(imagePath));
    return imageCodec ? imageCodec->makeBuffer() : nullptr;
  }

  Rect getBounds(const std::shared_ptr<Typeface>& /*typeface*/, GlyphID glyphID, bool /*fauxBold*/,
                 bool /*fauxItalic*/) const override {
    if (glyphID < 1 || glyphID > 6) {
      return Rect::MakeEmpty();
    }
    return Rect::MakeXYWH(50 * (glyphID - 1), 0, 50, 50);
  }

  Size getImageTransform(const std::shared_ptr<Typeface>& /*typeface*/, GlyphID /*glyphID*/,
                         Matrix* matrixOut) const override {
    matrixOut->setScale(0.25f, 0.25f);
    return Size::Make(200, 200);
  }
};

TGFX_TEST(VirtualTypefaceTest, DrawTextWithVirtualTypeface) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();

  const auto virtualTypeface1 = Typeface::MakeVirtual(false);
  Font font1(virtualTypeface1, 20);
  const auto virtualTypeface2 = Typeface::MakeVirtual(true);
  Font font2(virtualTypeface2, 20);

  const auto virtualTypefaceProvider = std::make_shared<CustomTypefaceProvider>();
  TypefaceProviderManager::GetInstance()->registerProvider(virtualTypefaceProvider);

  std::vector<GlyphRun> glyphRunList;
  GlyphRun glyphRun1(font1, {}, {});
  glyphRun1.glyphs.push_back(1);
  glyphRun1.glyphs.push_back(2);
  glyphRun1.glyphs.push_back(3);
  glyphRun1.positions.push_back(Point::Make(0.0f, 0.0f));
  glyphRun1.positions.push_back(Point::Make(50.0f, 0.0f));
  glyphRun1.positions.push_back(Point::Make(100.0f, 0.0f));

  GlyphRun glyphRun2(font2, {}, {});
  glyphRun2.glyphs.push_back(4);
  glyphRun2.glyphs.push_back(5);
  glyphRun2.glyphs.push_back(6);
  glyphRun2.positions.push_back(Point::Make(150.0f, 0.0f));
  glyphRun2.positions.push_back(Point::Make(205.0f, 0.0f));
  glyphRun2.positions.push_back(Point::Make(260.0f, 0.0f));
  glyphRunList.push_back(glyphRun1);
  glyphRunList.push_back(glyphRun2);

  auto textBlob = TextBlob::MakeFrom(std::move(glyphRunList));

  auto paint = Paint();
  paint.setColor(Color::Red());
  canvas->drawTextBlob(textBlob, 0.0f, 0.0f, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "VirtualTypefaceTest/DrawTextWithVirtualTypeface"));
}
}  // namespace tgfx
