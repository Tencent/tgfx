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
        constexpr Rect rect = Rect::MakeXYWH(5.0f, 5.0f, 40.0f, 40.0f);
        path->addOval(rect);
        path->close();
        return true;
    }
    return false;
  }

  std::shared_ptr<ImageBuffer> getImage(const std::shared_ptr<Typeface>& /*typeface*/,
                                        GlyphID glyphID, bool /*tryHardware*/) const override {
    switch (glyphID) {
      case 4:
        return PngCodec::MakeFrom(
                   ProjectPath::Absolute(
                       "/Users/henryjpxie/workspace/tgfx/resources/assets/image1.png"))
            ->makeBuffer();
      case 5:
        return PngCodec::MakeFrom(
                   ProjectPath::Absolute(
                       "/Users/henryjpxie/workspace/tgfx/resources/assets/image2.png"))
            ->makeBuffer();
      case 6:
        return PngCodec::MakeFrom(
                   ProjectPath::Absolute(
                       "/Users/henryjpxie/workspace/tgfx/resources/assets/image3.png"))
            ->makeBuffer();
    }
    return nullptr;
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
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface a\n");
  ContextScope scope;
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface b\n");
  auto context = scope.getContext();
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 0\n");
  ASSERT_TRUE(context != nullptr);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1\n");
  auto surface = Surface::Make(context, 400, 200);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1.1\n");
  //auto canvas = surface->getCanvas();
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1.2\n");

  const auto virtualTypeface1 = Typeface::MakeVirtual(false);
  Font font1(virtualTypeface1, 20);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1.3\n");
  const auto virtualTypeface2 = Typeface::MakeVirtual(true);
  Font font2(virtualTypeface2, 20);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1.4\n");

  const auto virtualTypefaceProvider = std::make_shared<CustomTypefaceProvider>();
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 1.5\n");
  TypefaceProviderManager::GetInstance()->registerProvider(virtualTypefaceProvider);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 2\n");

  std::vector<GlyphRun> glyphRunList;
  GlyphRun glyphRun1(font1, {}, {});
  glyphRun1.glyphs.push_back(1);
  glyphRun1.glyphs.push_back(2);
  glyphRun1.glyphs.push_back(3);
  glyphRun1.positions.push_back(Point::Make(0.0f, 0.0f));
  glyphRun1.positions.push_back(Point::Make(50.0f, 0.0f));
  glyphRun1.positions.push_back(Point::Make(100.0f, 0.0f));
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 3\n");

  GlyphRun glyphRun2(font2, {}, {});
  glyphRun2.glyphs.push_back(4);
  glyphRun2.glyphs.push_back(5);
  glyphRun2.glyphs.push_back(6);
  glyphRun2.positions.push_back(Point::Make(150.0f, 0.0f));
  glyphRun2.positions.push_back(Point::Make(205.0f, 0.0f));
  glyphRun2.positions.push_back(Point::Make(260.0f, 0.0f));
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 4\n");
  glyphRunList.push_back(glyphRun1);
  glyphRunList.push_back(glyphRun2);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5\n");

  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5.1\n");
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRunList));
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5.2\n");

  auto paint = Paint();
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5.3\n");
  paint.setColor(Color::Red());
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5.4\n");
#if 0
  canvas->drawTextBlob(textBlob, 0.0f, 0.0f, paint);
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 5.5\n");
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 6\n");
  EXPECT_TRUE(Baseline::Compare(surface, "VirtualTypefaceTest/DrawTextWithVirtualTypeface"));
  printf("VirtualTypefaceTest::DrawTextWithVirtualTypeface 7\n");
#endif
}
}  // namespace tgfx
