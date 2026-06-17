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

#include <hb-subset.h>
#include <hb.h>
#include <cstring>
#include "base/TGFXTest.h"
#include "core/utils/MD5.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/InnerShadowFilter.h"
#include "tgfx/layers/filters/NoiseFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "tgfx/pdf/PDFDocument.h"
#include "tgfx/pdf/PDFMetadata.h"
#include "tgfx/svg/SVGPathParser.h"
#include "utils/Baseline.h"
#include "utils/ContextScope.h"
#include "utils/ProjectPath.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {
bool ComparePDF(const std::shared_ptr<MemoryWriteStream>& stream, const std::string& key) {
  auto data = stream->readData();
#ifdef GENERATE_BASELINE_IMAGES
  SaveFile(data, key + "_base.pdf");
#endif
  auto result = Baseline::Compare(data, key);
  if (result) {
    RemoveFile(key + ".pdf");
  } else {
    SaveFile(data, key + ".pdf");
  }
  return result;
}
}  // namespace

TGFX_TEST(PDFExportTest, Empty) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata;
  metadata.title = "Empty PDF";

  auto document = PDFDocument::Make(PDFStream, context, metadata);
  document->beginPage(256.f, 256.f);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/Empty"));
}

TGFX_TEST(PDFExportTest, EmptyMultiPage) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata;
  metadata.title = "Empty Multi Page";

  auto document = PDFDocument::Make(PDFStream, context, metadata);
  document->beginPage(256.f, 256.f);
  document->endPage();
  document->beginPage(512.f, 512.f);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/EmptyMultiPage"));
}

TGFX_TEST(PDFExportTest, DrawColor) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::Red());
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DrawColor"));
}

TGFX_TEST(PDFExportTest, DrawShape) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(512.f, 512.f);
  {
    Paint paint;
    paint.setStyle(PaintStyle::Fill);

    paint.setColor(Color::FromRGBA(0, 0, 255, 128));
    canvas->drawRect(Rect::MakeXYWH(10.f, 10.f, 236.f, 236.f), paint);
    canvas->translate(256.f, 0);
    paint.setColor(Color::Green());
    canvas->drawRoundRect(Rect::MakeXYWH(10.f, 10.f, 236.f, 236.f), 30.f, 30.f, paint);
    canvas->translate(0.f, 256.f);
    paint.setColor(Color::Red());
    canvas->drawCircle(128.f, 128.f, 50.f, paint);
    canvas->translate(-256.f, 0.f);
    paint.setColor(Color::Black());
    canvas->drawOval(Rect::MakeXYWH(28.f, 78.f, 200.f, 100.f), paint);
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DrawShape"));
}

TGFX_TEST(PDFExportTest, DrawShapeStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(512.f, 512.f);
  {
    Paint paint;
    paint.setStyle(PaintStyle::Stroke);
    paint.setStrokeWidth(2.f);

    paint.setColor(Color::Blue());
    canvas->drawRect(Rect::MakeXYWH(10.f, 10.f, 236.f, 236.f), paint);
    canvas->translate(256.f, 0);
    paint.setColor(Color::Green());
    canvas->drawRoundRect(Rect::MakeXYWH(10.f, 10.f, 236.f, 236.f), 30.f, 30.f, paint);
    canvas->translate(0.f, 256.f);
    paint.setColor(Color::Red());
    canvas->drawCircle(128.f, 128.f, 50.f, paint);
    canvas->translate(-256.f, 0.f);
    paint.setColor(Color::Black());
    canvas->drawOval(Rect::MakeXYWH(28.f, 78.f, 200.f, 100.f), paint);
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DrawShapeStroke"));
}

TGFX_TEST(PDFExportTest, SimpleText) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(1500.f, 400.f);
  canvas->translate(40.0, 20.0);
  {
    auto typeface =
        Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
    Font font(typeface, 150.f);

    Paint paint;
    paint.setColor(Color::Blue());
    canvas->drawSimpleText("TGFX from 腾讯", 55, 125, font, paint);

    Paint strokePaint;
    strokePaint.setColor(Color::Black());
    strokePaint.setStyle(PaintStyle::Stroke);
    strokePaint.setStrokeWidth(2.f);
    canvas->drawSimpleText("TGFX from 腾讯", 55, 325, font, strokePaint);
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/SimpleText"));
}

TGFX_TEST(PDFExportTest, EmojiText) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(1500.f, 500.f);
  canvas->translate(40.0, 20.0);
  {
    auto typeface =
        Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
    Font font(typeface, 150.f);

    Paint paint;
    canvas->drawSimpleText("🏎🗻🧋🧟", 55, 125, font, paint);
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/EmojiText"));
}

TGFX_TEST(PDFExportTest, Image) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(500.f, 500.f);
  {
    canvas->translate(50.f, 50.f);
    auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/assets/glyph1.png"));
    auto shader = Shader::MakeImageShader(image);
    Paint paint;
    paint.setShader(shader);
    canvas->drawRect(Rect::MakeWH(200, 200), paint);
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/Image"));
}

TGFX_TEST(PDFExportTest, Complex) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(1000.f, 500.f);
  canvas->translate(40.0, 20.0);

  {
    auto makeStrokePaint = []() -> Paint {
      Paint strokePaint;
      strokePaint.setStyle(PaintStyle::Stroke);
      strokePaint.setColor(Color::Blue());
      Stroke stroke;
      stroke.width = 25.0f;
      strokePaint.setStroke(stroke);
      return strokePaint;
    };

    // T
    auto path = SVGPathParser::FromSVGString("M114.5 206L228.382 8.74997H0.617676L114.5 206Z");
    if (path) {
      Paint paint;
      paint.setColor(Color::Blue());
      canvas->drawPath(*path, paint);

      auto strokePaint = makeStrokePaint();
      strokePaint.setStyle(PaintStyle::Stroke);
      auto gradientShader = tgfx::Shader::MakeLinearGradient(
          tgfx::Point{0.f, 0.f}, tgfx::Point{0.f, 200.f},
          {tgfx::Color::FromRGBA(157, 239, 132), tgfx::Color::FromRGBA(255, 156, 69)}, {});
      strokePaint.setShader(gradientShader);
      auto blurFilter = ImageFilter::Blur(6, 6, TileMode::Decal);
      strokePaint.setImageFilter(blurFilter);
      canvas->drawPath(*path, strokePaint);
    }

    //G
    path = SVGPathParser::FromSVGString(
        "M423 106C423 125.778 417.135 145.112 406.147 161.557C395.159 178.002 379.541 190.819 "
        "361.268 198.388C342.996 205.957 322.889 207.937 303.491 204.078C284.093 200.22 266.275 "
        "190.696 252.289 176.711C238.304 162.725 228.78 144.907 224.921 125.509C221.063 106.111 "
        "223.043 86.0042 230.612 67.7316C238.181 49.459 250.998 33.8411 267.443 22.853C283.888 "
        "11.8649 303.222 5.99997 323 5.99997L323 106H423Z");
    if (path) {
      Paint paint;
      tgfx::Point center{path->getBounds().centerX() + 25.f, path->getBounds().centerY() + 25.f};
      auto gradientShader = tgfx::Shader::MakeRadialGradient(
          center, 75, {tgfx::Color::FromRGBA(69, 151, 247), tgfx::Color::FromRGBA(130, 228, 153)},
          {0, 1.0});
      paint.setShader(gradientShader);
      auto imageFilter = ImageFilter::InnerShadow(20, 20, 9, 9, Color::Red());
      paint.setImageFilter(imageFilter);
      canvas->drawPath(*path, paint);

      auto strokePaint = makeStrokePaint();
      strokePaint.setColor(Color::FromRGBA(232, 133, 133));
      canvas->drawPath(*path, strokePaint);
    }

    //X
    path = SVGPathParser::FromSVGString(
        "M917.168 0.0357666L866.632 106.116L917.228 212.168L811.148 161.632L705.096 "
        "212.228L755.632 106.148L705.036 0.0961968L811.116 50.632L917.168 0.0357666Z");
    if (path) {
      Paint paint;
      paint.setColor(Color::FromRGBA(230, 234, 147));
      auto imageFilter = ImageFilter::DropShadow(30, 30, 13, 13, Color::Blue());
      paint.setImageFilter(imageFilter);
      canvas->drawPath(*path, paint);
    }

    // F
    path = SVGPathParser::FromSVGString("M656 5.99997H456V206H536V86H656V5.99997Z");
    if (path) {
      Paint paint;
      paint.setColor(Color::FromRGBA(230, 234, 147));
      auto imageFilter = ImageFilter::InnerShadow(10, 10, 3, 3, Color::Blue());
      paint.setImageFilter(imageFilter);
      canvas->drawPath(*path, paint);

      auto strokePaint = makeStrokePaint();
      auto gradientShader = tgfx::Shader::MakeLinearGradient(
          tgfx::Point{0.f, 0.f}, tgfx::Point{0.f, 200.f},
          {tgfx::Color::FromRGBA(157, 239, 132), tgfx::Color::FromRGBA(255, 156, 69)}, {});
      strokePaint.setShader(gradientShader);
      canvas->drawPath(*path, strokePaint);
    }
  }

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/Complex"));
}

TGFX_TEST(PDFExportTest, MD5Test) {
  const char* testString = "The quick brown fox jumps over the lazy dog";
  auto digest = MD5::Calculate(testString, strlen(testString));
  const uint8_t expectedDigest[16] = {0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82,
                                      0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6};
  EXPECT_EQ(0, std::memcmp(digest.data(), expectedDigest, 16));
}

TGFX_TEST(PDFExportTest, DstColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata{};
  metadata.targetColorSpace = ColorSpace::DisplayP3();
  auto document = PDFDocument::Make(PDFStream, context, metadata);
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::DisplayP3()));
  document->endPage();
  canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::SRGB()));
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  auto image = MakeImage("resources/apitest/green_p3.png");
  canvas->drawImage(image);
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  Paint paint{};
  paint.setImageFilter(ImageFilter::DropShadow(500, 500, 10, 10, Color::Green()));
  canvas->drawImage(image, &paint);
  document->endPage();
  document->close();
  PDFStream->flush();
  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DstColorSpace"));
}

TGFX_TEST(PDFExportTest, AssignColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata{};
  metadata.assignColorSpace = ColorSpace::SRGB();
  auto document = PDFDocument::Make(PDFStream, context, metadata);
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::DisplayP3()));
  document->endPage();
  canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::SRGB()));
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  auto image = MakeImage("resources/apitest/green_p3.png");
  canvas->drawImage(image);
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  Paint paint{};
  paint.setImageFilter(ImageFilter::DropShadow(500, 500, 10, 10, Color::Green()));
  canvas->drawImage(image, &paint);
  document->endPage();
  document->close();
  PDFStream->flush();
  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/AssignColorSpace"));
}

namespace {
std::shared_ptr<MemoryWriteStream> MakeSinglePagePDF(Context* context, float width, float height,
                                                     void (*drawFunc)(Canvas*)) {
  auto stream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(stream, context, PDFMetadata());
  auto canvas = document->beginPage(width, height);
  drawFunc(canvas);
  document->endPage();
  document->close();
  stream->flush();
  return stream;
}

void DrawInnerShadowTranslate(Canvas* canvas) {
  Paint paint{};
  canvas->translate(50, 50);
  paint.setColor(Color::FromRGBA(100, 200, 255));
  paint.setImageFilter(ImageFilter::InnerShadow(8, 8, 6, 6, Color::FromRGBA(0, 0, 100)));
  canvas->drawRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 15, 15, paint);
}

void DrawInnerShadowTranslateRotateScale(Canvas* canvas) {
  Paint paint{};
  canvas->translate(100, 100);
  canvas->rotate(-30);
  canvas->scale(0.7f, 0.7f);
  paint.setColor(Color::FromRGBA(255, 255, 150));
  paint.setImageFilter(ImageFilter::InnerShadow(8, 8, 6, 6, Color::FromRGBA(100, 100, 0)));
  canvas->drawRoundRect(Rect::MakeXYWH(-50, -50, 100, 100), 15, 15, paint);
}

void DrawInnerShadowOnlyTranslate(Canvas* canvas) {
  Paint paint{};
  canvas->translate(50, 50);
  paint.setColor(Color::FromRGBA(200, 200, 200));
  paint.setImageFilter(ImageFilter::InnerShadowOnly(8, 8, 6, 6, Color::FromRGBA(50, 0, 80)));
  canvas->drawRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 15, 15, paint);
}

void DrawInnerShadowOnlyTranslateRotate(Canvas* canvas) {
  Paint paint{};
  canvas->translate(100, 100);
  canvas->rotate(30);
  paint.setColor(Color::FromRGBA(200, 200, 200));
  paint.setImageFilter(ImageFilter::InnerShadowOnly(8, 8, 6, 6, Color::FromRGBA(80, 50, 0)));
  canvas->drawRoundRect(Rect::MakeXYWH(-50, -50, 100, 100), 15, 15, paint);
}
}  // namespace

TGFX_TEST(PDFExportTest, InnerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  // 1. InnerShadow with translate only
  EXPECT_TRUE(ComparePDF(MakeSinglePagePDF(context, 200.f, 200.f, DrawInnerShadowTranslate),
                         "PDFTest/InnerShadow_Translate"));

  // 2. InnerShadow with translate + rotate + scale (covers all matrix types)
  EXPECT_TRUE(
      ComparePDF(MakeSinglePagePDF(context, 200.f, 200.f, DrawInnerShadowTranslateRotateScale),
                 "PDFTest/InnerShadow_TranslateRotateScale"));

  // 3. InnerShadowOnly with translate
  EXPECT_TRUE(ComparePDF(MakeSinglePagePDF(context, 200.f, 200.f, DrawInnerShadowOnlyTranslate),
                         "PDFTest/InnerShadowOnly_Translate"));

  // 4. InnerShadowOnly with translate + rotate
  EXPECT_TRUE(
      ComparePDF(MakeSinglePagePDF(context, 200.f, 200.f, DrawInnerShadowOnlyTranslateRotate),
                 "PDFTest/InnerShadowOnly_TranslateRotate"));
}

TGFX_TEST(PDFExportTest, InnerShadowMultipleMatrices) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(600.f, 400.f);
  {
    // Use explicit saveLayer with InnerShadow filter so that multiple draw operations with
    // different matrices are recorded into a single Picture. Each canvas->save/restore pair
    // resets the matrix state, so the recorded SetMatrix values are absolute (e.g.,
    // translate(50,50) then translate(250,50)). In drawLayer, the code tracks currentMatrix
    // by direct assignment from SetMatrix records. If it incorrectly used preConcat instead,
    // the second draw's matrix would become translate(50,50) * translate(250,50), causing
    // wrong shadow offset and blur compensation.
    Paint layerPaint;
    layerPaint.setImageFilter(ImageFilter::InnerShadow(8, 8, 6, 6, Color::FromRGBA(0, 0, 100)));
    canvas->saveLayer(&layerPaint);

    // Draw 1: translate only
    Paint paint;
    paint.setColor(Color::FromRGBA(100, 200, 255));
    canvas->save();
    canvas->translate(50, 50);
    canvas->drawRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 15, 15, paint);
    canvas->restore();

    // Draw 2: different translate — absolute matrix should be translate(250, 50),
    // NOT translate(50,50) * translate(250,50) which preConcat would produce.
    paint.setColor(Color::FromRGBA(255, 180, 100));
    canvas->save();
    canvas->translate(250, 50);
    canvas->drawRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 15, 15, paint);
    canvas->restore();

    // Draw 3: translate + rotate
    paint.setColor(Color::FromRGBA(150, 255, 150));
    canvas->save();
    canvas->translate(450, 100);
    canvas->rotate(45);
    canvas->drawRoundRect(Rect::MakeXYWH(-50, -50, 100, 100), 15, 15, paint);
    canvas->restore();

    // Draw 4: translate + scale
    paint.setColor(Color::FromRGBA(255, 150, 255));
    canvas->save();
    canvas->translate(150, 250);
    canvas->scale(1.5f, 1.5f);
    canvas->drawRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 15, 15, paint);
    canvas->restore();

    canvas->restore();  // end saveLayer
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/InnerShadowMultipleMatrices"));
}

TGFX_TEST(PDFExportTest, DstAssignColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata{};
  metadata.targetColorSpace = ColorSpace::DisplayP3();
  metadata.assignColorSpace = ColorSpace::SRGB();
  auto document = PDFDocument::Make(PDFStream, context, metadata);
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::DisplayP3()));
  document->endPage();
  canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::SRGB()));
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  auto image = MakeImage("resources/apitest/green_p3.png");
  canvas->drawImage(image);
  document->endPage();
  canvas = document->beginPage(2048.f, 2048.f);
  Paint paint{};
  paint.setImageFilter(ImageFilter::DropShadow(500, 500, 10, 10, Color::Green()));
  canvas->drawImage(image, &paint);
  document->endPage();
  document->close();
  PDFStream->flush();
  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DstAssignColorSpace"));
}

TGFX_TEST(PDFExportTest, LayerLinearGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto shapeLayer = ShapeLayer::Make();
  Rect rect = Rect::MakeWH(2501.f, 1860.f);
  Path path;
  path.addRect(rect);
  shapeLayer->setPath(path);
  shapeLayer->removeFillStyles();

  // Create vertical linear gradient with matrix wrapping
  auto shader = Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{0.f, 1860.f},
      {Color::FromRGBA(227, 136, 136), Color::FromRGBA(140, 210, 183)}, {});
  shader = shader->makeWithMatrix(Matrix::MakeTrans(10.f, 10.f));
  shapeLayer->addFillStyle(ShapeStyle::Make(shader));

  auto layer = Layer::Make();
  layer->addChild(shapeLayer);

  // Draw layer directly to PDF canvas
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(2501.f, 1860.f);
  layer->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LayerLinearGradient"));
}

TGFX_TEST(PDFExportTest, LinearGradientWhiteAlpha) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::Black());

  auto shader = Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{256.f, 0.f},
      {Color::FromRGBA(255, 255, 255, 128), Color::FromRGBA(255, 255, 255, 26)}, {});

  Paint paint;
  paint.setShader(shader);
  canvas->drawRect(Rect::MakeWH(256.f, 256.f), paint);

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LinearGradientWhiteAlpha"));
}

TGFX_TEST(PDFExportTest, LayerRadialGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto shapeLayer = ShapeLayer::Make();
  Rect rect = Rect::MakeWH(2501.f, 1860.f);
  Path path;
  path.addRect(rect);
  shapeLayer->setPath(path);
  shapeLayer->removeFillStyles();

  auto shader = Shader::MakeRadialGradient(
      Point{1250.5f, 930.f}, 930.f,
      {Color::FromRGBA(227, 136, 136), Color::FromRGBA(140, 210, 183)}, {});
  shader = shader->makeWithMatrix(Matrix::MakeTrans(10.f, 10.f));
  shapeLayer->addFillStyle(ShapeStyle::Make(shader));

  auto layer = Layer::Make();
  layer->addChild(shapeLayer);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(2501.f, 1860.f);
  layer->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LayerRadialGradient"));
}

TGFX_TEST(PDFExportTest, LayerConicGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto shapeLayer = ShapeLayer::Make();
  Rect rect = Rect::MakeWH(2501.f, 1860.f);
  Path path;
  path.addRect(rect);
  shapeLayer->setPath(path);
  shapeLayer->removeFillStyles();

  auto shader = Shader::MakeConicGradient(
      Point{1250.5f, 930.f}, 0.f, 360.f,
      {Color::FromRGBA(227, 136, 136), Color::FromRGBA(140, 210, 183)}, {});
  shader = shader->makeWithMatrix(Matrix::MakeTrans(10.f, 10.f));
  shapeLayer->addFillStyle(ShapeStyle::Make(shader));

  auto layer = Layer::Make();
  layer->addChild(shapeLayer);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(2501.f, 1860.f);
  layer->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LayerConicGradient"));
}

TGFX_TEST(PDFExportTest, LayerConicGradientRotated) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto shapeLayer = ShapeLayer::Make();
  Rect rect = Rect::MakeWH(2501.f, 1860.f);
  Path path;
  path.addRect(rect);
  shapeLayer->setPath(path);
  shapeLayer->removeFillStyles();

  auto shader = Shader::MakeConicGradient(
      Point{1250.5f, 930.f}, 0.f, 360.f,
      {Color::FromRGBA(227, 136, 136), Color::FromRGBA(140, 210, 183)}, {});
  shapeLayer->addFillStyle(ShapeStyle::Make(shader));

  auto layer = Layer::Make();
  layer->addChild(shapeLayer);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(1860.f, 2501.f);
  canvas->translate(1860.f, 0.f);
  canvas->rotate(90.f);
  layer->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LayerConicGradientRotated"));
}

TGFX_TEST(PDFExportTest, LayerDiamondGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto shapeLayer = ShapeLayer::Make();
  Rect rect = Rect::MakeWH(2501.f, 1860.f);
  Path path;
  path.addRect(rect);
  shapeLayer->setPath(path);
  shapeLayer->removeFillStyles();

  auto shader = Shader::MakeDiamondGradient(
      Point{1250.5f, 930.f}, 930.f,
      {Color::FromRGBA(227, 136, 136), Color::FromRGBA(140, 210, 183)}, {});
  shader = shader->makeWithMatrix(Matrix::MakeTrans(10.f, 10.f));
  shapeLayer->addFillStyle(ShapeStyle::Make(shader));

  auto layer = Layer::Make();
  layer->addChild(shapeLayer);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(2501.f, 1860.f);
  layer->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/LayerDiamondGradient"));
}

TGFX_TEST(PDFExportTest, ImageShaderScaledDown) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  // Page size is much smaller than the source images.
  auto canvas = document->beginPage(400.f, 250.f);
  {
    // JPEG image (1536x1536) rendered into a 150x150 area with shader matrix.
    auto jpegImage = Image::MakeFromFile(ProjectPath::Absolute("resources/assets/bridge.jpg"));
    EXPECT_TRUE(jpegImage != nullptr);
    auto jpegShader = Shader::MakeImageShader(jpegImage);
    float jpegScale = 150.f / 1536.f;
    jpegShader = jpegShader->makeWithMatrix(Matrix::MakeScale(jpegScale));
    Paint paint;
    paint.setShader(jpegShader);
    canvas->save();
    canvas->translate(25.f, 50.f);
    canvas->drawRect(Rect::MakeWH(150.f, 150.f), paint);
    canvas->restore();

    // PNG image (512x512) rendered into a 150x150 area with shader matrix.
    auto pngImage = Image::MakeFromFile(ProjectPath::Absolute("resources/assets/tgfx.png"));
    EXPECT_TRUE(pngImage != nullptr);
    auto pngShader = Shader::MakeImageShader(pngImage);
    float pngScale = 150.f / 512.f;
    pngShader = pngShader->makeWithMatrix(Matrix::MakeScale(pngScale));
    paint.setShader(pngShader);
    canvas->save();
    canvas->translate(225.f, 50.f);
    canvas->drawRect(Rect::MakeWH(150.f, 150.f), paint);
    canvas->restore();
  }
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ImageShaderScaledDown"));
}

TGFX_TEST(PDFExportTest, BitmapMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  // Load a bitmap image with alpha to use as mask (not a PictureImage, so drawPathWithFilter
  // will take the bitmap mask code path instead of the vector Picture path).
  auto maskImage = Image::MakeFromFile(ProjectPath::Absolute("resources/assets/tgfx.png"));
  EXPECT_TRUE(maskImage != nullptr);

  // Create a MaskFilter from the bitmap image shader.
  // Scale the 512x512 image down to 200x200 to fit the page.
  float scale = 200.f / static_cast<float>(maskImage->width());
  auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Decal, TileMode::Decal);
  maskShader = maskShader->makeWithMatrix(Matrix::MakeScale(scale));
  auto maskFilter = MaskFilter::MakeShader(maskShader);

  float pageW = 300.f;
  float pageH = 300.f;

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(pageW, pageH);
  canvas->drawColor(Color::White());
  canvas->translate(50.f, 50.f);
  Paint paint;
  paint.setColor(Color::Red());
  paint.setMaskFilter(maskFilter);
  canvas->drawRect(Rect::MakeWH(200.f, 200.f), paint);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/BitmapMask"));
}

TGFX_TEST(PDFExportTest, DropShadowLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto root = Layer::Make();

  // Left: showBehindLayer=false
  auto layerA = ShapeLayer::Make();
  Path pathA;
  pathA.addRect(Rect::MakeWH(200.f, 200.f));
  layerA->setPath(pathA);
  layerA->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127)));
  layerA->setPosition(Point{50.f, 50.f});
  layerA->setLayerStyles({DropShadowStyle::Make(20.f, 20.f, 10.f, 10.f, Color::Blue(), false)});
  root->addChild(layerA);

  // Right: showBehindLayer=true
  auto layerB = ShapeLayer::Make();
  Path pathB;
  pathB.addRect(Rect::MakeWH(200.f, 200.f));
  layerB->setPath(pathB);
  layerB->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127)));
  layerB->setPosition(Point{350.f, 50.f});
  layerB->setLayerStyles({DropShadowStyle::Make(20.f, 20.f, 10.f, 10.f, Color::Blue(), true)});
  root->addChild(layerB);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());

  // Page 1: rectangles
  auto canvas = document->beginPage(650.f, 350.f);
  canvas->drawColor(Color::FromRGBA(200, 200, 200));
  root->draw(canvas);
  document->endPage();

  // Page 2: circles with the same drop shadow configuration
  auto circleRoot = Layer::Make();

  auto circleA = ShapeLayer::Make();
  Path circlePath1;
  circlePath1.addOval(Rect::MakeWH(200.f, 200.f));
  circleA->setPath(circlePath1);
  circleA->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127)));
  circleA->setPosition(Point{50.f, 50.f});
  circleA->setLayerStyles({DropShadowStyle::Make(20.f, 20.f, 10.f, 10.f, Color::Blue(), false),
                           DropShadowStyle::Make(-20.f, -20.f, 10.f, 10.f, Color::Green(), false)});
  circleRoot->addChild(circleA);

  auto circleB = ShapeLayer::Make();
  Path circlePath2;
  circlePath2.addOval(Rect::MakeWH(200.f, 200.f));
  circleB->setPath(circlePath2);
  circleB->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127)));
  circleB->setPosition(Point{350.f, 50.f});
  circleB->setLayerStyles({DropShadowStyle::Make(20.f, 20.f, 10.f, 10.f, Color::Blue(), true),
                           DropShadowStyle::Make(-20.f, -20.f, 10.f, 10.f, Color::Green(), true)});
  circleRoot->addChild(circleB);

  canvas = document->beginPage(650.f, 350.f);
  canvas->drawColor(Color::FromRGBA(200, 200, 200));
  circleRoot->draw(canvas);
  document->endPage();

  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/DropShadowLayer"));
}

TGFX_TEST(PDFExportTest, NonRegularBlendMode) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  ASSERT_TRUE(document != nullptr);

  auto canvas = document->beginPage(200.f, 200.f);
  ASSERT_TRUE(canvas != nullptr);

  Paint paint;
  paint.setColor(Color::Red());
  paint.setBlendMode(BlendMode::Src);
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/NonRegularBlendMode"));
}

TGFX_TEST(PDFExportTest, SrcBlendOverlap) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  ASSERT_TRUE(document != nullptr);
  auto canvas = document->beginPage(300.f, 300.f);
  ASSERT_TRUE(canvas != nullptr);

  // Bottom rectangle: blue, drawn with default SrcOver blend mode.
  Paint basePaint;
  basePaint.setColor(Color::Blue());
  canvas->drawRect(Rect::MakeXYWH(50, 50, 150, 150), basePaint);

  // Top rectangle: semi-transparent red, drawn with Src blend mode.
  // Src replaces the destination entirely, so the overlap region should show only the red color
  // (including its alpha), with no blue bleed-through.
  Paint srcPaint;
  srcPaint.setColor(Color::FromRGBA(255, 0, 0, 128));
  srcPaint.setBlendMode(BlendMode::Src);
  canvas->drawRect(Rect::MakeXYWH(100, 100, 150, 150), srcPaint);

  document->endPage();
  document->close();
  PDFStream->flush();
  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/SrcBlendOverlap"));
}

// Verifies that setting encodingQuality <= 100 enables JPEG (DCT) compression for the RGB data of
// images in the exported PDF. The alpha channel should still use FlateDecode.
TGFX_TEST(PDFExportTest, ImageDCTEncode) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/mandrill_128.webp"));
  EXPECT_TRUE(image != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  PDFMetadata metadata;
  metadata.encodingQuality = 85;
  auto document = PDFDocument::Make(PDFStream, context, metadata);
  auto canvas = document->beginPage(228.f, 228.f);
  ASSERT_TRUE(canvas != nullptr);

  canvas->drawImage(image, 50.f, 50.f);

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ImageDCTEncode"));
}

// Verifies that drawing the same image multiple times reuses a single Image XObject in the PDF
// rather than embedding duplicate copies of the image data.
TGFX_TEST(PDFExportTest, ImageDeduplication) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/mandrill_128.webp"));
  EXPECT_TRUE(image != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(500.f, 250.f);
  ASSERT_TRUE(canvas != nullptr);

  canvas->drawImage(image, 50.f, 50.f);

  canvas->save();
  canvas->translate(200.f, 0.f);
  canvas->scale(0.5f, 0.5f);
  canvas->drawImage(image, 0.f, 0.f);
  canvas->restore();

  canvas->save();
  canvas->translate(350.f, 50.f);
  canvas->scale(0.25f, 0.25f);
  canvas->drawImage(image, 0.f, 0.f);
  canvas->restore();

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ImageDeduplication"));
}

// Verifies that JPEG encoding works correctly for images with alpha channel: RGB data should use
// DCTDecode while the alpha (SMask) should use FlateDecode.
TGFX_TEST(PDFExportTest, ImageDCTEncodeWithAlpha) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/imageReplacement.png"));
  EXPECT_TRUE(image != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  PDFMetadata metadata;
  metadata.encodingQuality = 80;
  auto document = PDFDocument::Make(PDFStream, context, metadata);
  auto canvas = document->beginPage(300.f, 300.f);
  ASSERT_TRUE(canvas != nullptr);

  canvas->drawImage(image, 50.f, 50.f);

  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ImageDCTEncodeWithAlpha"));
}

// Verifies that drawing the same image across multiple pages reuses a single Image XObject
// reference instead of embedding duplicate copies.
TGFX_TEST(PDFExportTest, ImageDeduplicationCrossPage) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/mandrill_128.webp"));
  EXPECT_TRUE(image != nullptr);

  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());

  auto canvas = document->beginPage(228.f, 228.f);
  ASSERT_TRUE(canvas != nullptr);
  canvas->drawImage(image, 50.f, 50.f);
  document->endPage();

  canvas = document->beginPage(228.f, 228.f);
  ASSERT_TRUE(canvas != nullptr);
  canvas->drawImage(image, 50.f, 50.f);
  document->endPage();

  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ImageDeduplicationCrossPage"));
}

// Direct unit-test fixture for PDFStreamOut. Builds a minimal PDFDocumentImpl whose underlying
// WriteStream is a MemoryWriteStream we own, runs PDFStreamOut on the supplied input, and returns
// the raw bytes that were emitted for the single stream object. The fixture does not call
// beginPage / endPage, so the document remains in BetweenPages state and ~PDFDocumentImpl exits
// cleanly via the empty-pages branch in onClose.
namespace {
struct EmittedStream {
  std::shared_ptr<Data> bytes;
  PDFIndirectReference ref;
};

EmittedStream RunPDFStreamOut(const std::string& input,
                              PDFMetadata::CompressionLevel compressionLevel,
                              PDFSteamCompressionEnabled compressFlag) {
  auto sink = MemoryWriteStream::Make();
  PDFMetadata metadata;
  metadata.compressionLevel = compressionLevel;
  PDFDocumentImpl doc(sink, /*context=*/nullptr, metadata);
  // PDFStreamOut routes through emitStream, which records object offsets via offsetMap. The full
  // PDFDocument pipeline initializes baseOffset inside SerializeHeader during the first
  // onBeginPage call; this fixture skips beginPage entirely, so initialize the offset map here so
  // the DEBUG_ASSERT inside Difference does not fire.
  doc.offsetMap.markStartOfDocument(sink);

  auto inputData = Data::MakeWithCopy(input.data(), input.size());
  auto inputStream = Stream::MakeFromData(inputData);

  auto dict = PDFDictionary::Make();
  PDFIndirectReference ref =
      PDFStreamOut(std::move(dict), std::move(inputStream), &doc, compressFlag);
  return EmittedStream{sink->readData(), ref};
}

// Searches `bytes` for the byte sequence `needle` and returns its start offset, or
// std::string::npos if not found.
size_t IndexOf(const std::shared_ptr<Data>& bytes, const char* needle) {
  size_t needleSize = std::strlen(needle);
  if (bytes == nullptr || needleSize == 0 || bytes->size() < needleSize) {
    return std::string::npos;
  }
  const auto* base = static_cast<const uint8_t*>(bytes->data());
  size_t size = bytes->size();
  for (size_t i = 0; i + needleSize <= size; ++i) {
    if (memcmp(base + i, needle, needleSize) == 0) {
      return i;
    }
  }
  return std::string::npos;
}

// Parses an unsigned decimal integer starting at `offset` in `bytes`, stopping at the first
// non-digit. Returns the parsed value; sets *consumed to the number of bytes consumed (0 if no
// digit was found).
size_t ParseUnsigned(const std::shared_ptr<Data>& bytes, size_t offset, size_t* consumed) {
  const auto* base = static_cast<const uint8_t*>(bytes->data());
  size_t size = bytes->size();
  size_t value = 0;
  size_t i = offset;
  while (i < size && base[i] >= '0' && base[i] <= '9') {
    value = value * 10 + static_cast<size_t>(base[i] - '0');
    ++i;
  }
  *consumed = i - offset;
  return value;
}

// Reads "/Length <N>" from the emitted bytes and returns N. Fails the current test if the entry
// is missing or malformed.
size_t ReadDeclaredLength(const std::shared_ptr<Data>& bytes) {
  size_t at = IndexOf(bytes, "/Length ");
  if (at == std::string::npos) {
    ADD_FAILURE() << "Emitted stream does not contain a '/Length ' entry.";
    return 0;
  }
  size_t consumed = 0;
  size_t value = ParseUnsigned(bytes, at + std::strlen("/Length "), &consumed);
  if (consumed == 0) {
    ADD_FAILURE() << "Emitted stream's /Length entry is not followed by a decimal integer.";
    return 0;
  }
  return value;
}

// Returns the byte count between the "stream\n" marker and the "\nendstream" marker — i.e. the
// number of bytes that PDFDocumentImpl::emitStream actually wrote between the keywords. /Length
// must equal this value (ISO 32000-1 §7.3.8.2).
size_t MeasureActualPayload(const std::shared_ptr<Data>& bytes) {
  size_t streamAt = IndexOf(bytes, "stream\n");
  size_t endstreamAt = IndexOf(bytes, "\nendstream");
  if (streamAt == std::string::npos || endstreamAt == std::string::npos ||
      endstreamAt <= streamAt) {
    ADD_FAILURE() << "Emitted bytes do not contain a complete stream/endstream pair.";
    return 0;
  }
  size_t payloadStart = streamAt + std::strlen("stream\n");
  return endstreamAt - payloadStart;
}
}  // namespace

// Bug 1 unit test, compressed branch. Drives PDFStreamOut with input large enough that
// FlateDecode actually saves bytes; expects /Length to equal the compressed payload size, not the
// original input size. This is the case the PR fix targets directly.
TGFX_TEST(PDFExportTest, PDFStreamOutWritesCompressedLength) {
  // Highly redundant input compresses well; the 4096-byte block reliably exceeds MinimumSavings.
  std::string input(4096, 'A');
  auto emitted = RunPDFStreamOut(input, PDFMetadata::CompressionLevel::Default,
                                 PDFSteamCompressionEnabled::Yes);

  EXPECT_NE(IndexOf(emitted.bytes, "/Filter /FlateDecode"), std::string::npos)
      << "Compressed branch should emit /Filter /FlateDecode.";

  size_t declared = ReadDeclaredLength(emitted.bytes);
  size_t actual = MeasureActualPayload(emitted.bytes);

  EXPECT_EQ(declared, actual) << "/Length must match the actual payload bytes (ISO 32000-1 "
                                 "§7.3.8.2). declared="
                              << declared << " actual=" << actual;
  EXPECT_LT(declared, input.size())
      << "Compressed payload should be smaller than the uncompressed input; otherwise the test "
         "input is not exercising the compression path.";
}

// Bug 1 unit test, uncompressed branch (compression disabled by metadata). /Length must equal the
// input size and no /Filter entry should be emitted.
TGFX_TEST(PDFExportTest, PDFStreamOutWritesUncompressedLength) {
  std::string input = "Hello, PDF stream length test.";
  auto emitted =
      RunPDFStreamOut(input, PDFMetadata::CompressionLevel::None, PDFSteamCompressionEnabled::Yes);

  EXPECT_EQ(IndexOf(emitted.bytes, "/Filter"), std::string::npos)
      << "Uncompressed branch must not emit a /Filter entry.";

  size_t declared = ReadDeclaredLength(emitted.bytes);
  size_t actual = MeasureActualPayload(emitted.bytes);

  EXPECT_EQ(declared, actual) << "/Length must match the actual payload bytes. declared="
                              << declared << " actual=" << actual;
  EXPECT_EQ(declared, input.size());
}

// Bug 1 unit test, "compression refused" branch: input is too short or too random for FlateDecode
// to save MinimumSavings bytes, so SerializeStream falls back to writing the raw bytes. The
// pre-fix code wrote the original input size into /Length unconditionally, so the bug surfaces in
// this branch as well — making sure /Length still matches the actually-emitted payload (== input
// size, since no Filter is written).
TGFX_TEST(PDFExportTest, PDFStreamOutFallsBackWhenCompressionDoesNotSave) {
  // 8 bytes is well under MinimumSavings (= strlen("/Filter_/FlateDecode_") = 21), so
  // SerializeStream skips the deflate path entirely.
  std::string input = "abcdefgh";
  auto emitted = RunPDFStreamOut(input, PDFMetadata::CompressionLevel::Default,
                                 PDFSteamCompressionEnabled::Yes);

  EXPECT_EQ(IndexOf(emitted.bytes, "/Filter"), std::string::npos)
      << "Short input below MinimumSavings should bypass FlateDecode.";

  size_t declared = ReadDeclaredLength(emitted.bytes);
  size_t actual = MeasureActualPayload(emitted.bytes);

  EXPECT_EQ(declared, actual) << "/Length must match the actual payload bytes even when the "
                                 "compression path is skipped. declared="
                              << declared << " actual=" << actual;
  EXPECT_EQ(declared, input.size());
}

TGFX_TEST(PDFExportTest, NoiseEffects) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto fill = Color::FromRGBA(60, 120, 200);
  constexpr float RectW = 80.f;
  constexpr float RectH = 80.f;
  constexpr float GapX = 20.f;
  constexpr float GapY = 20.f;
  constexpr float Margin = 40.f;
  constexpr int Cols = 3;
  constexpr int Rows = 7;

  auto root = Layer::Make();

  // Helper to create a rect ShapeLayer at (col, row) grid position.
  auto addRect = [&](int col, int row) {
    auto shape = ShapeLayer::Make();
    Path path;
    path.addRect(Rect::MakeWH(RectW, RectH));
    shape->setPath(path);
    shape->setFillStyle(ShapeStyle::Make(fill));
    float x = Margin + static_cast<float>(col) * (RectW + GapX);
    float y = Margin + static_cast<float>(row) * (RectH + GapY);
    shape->setMatrix(Matrix::MakeTrans(x, y));
    root->addChild(shape);
    return shape;
  };

  // Row 1: NoiseFilter only.
  addRect(0, 0)->setFilters({NoiseFilter::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                   42.0f, BlendMode::SrcOver)});
  addRect(1, 0)->setFilters(
      {NoiseFilter::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                            Color::FromRGBA(0, 0, 255, 128), 43.0f, BlendMode::SrcOver)});
  addRect(2, 0)->setFilters({NoiseFilter::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f, BlendMode::SrcOver)});

  // Row 2: NoiseStyle only.
  addRect(0, 1)->setLayerStyles(
      {NoiseStyle::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128), 42.0f)});
  addRect(1, 1)->setLayerStyles({NoiseStyle::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                     Color::FromRGBA(0, 0, 255, 128), 43.0f)});
  addRect(2, 1)->setLayerStyles({NoiseStyle::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f)});

  // Row 3: Row 1 filters + BlurFilter / InnerShadowFilter / DropShadowFilter.
  addRect(0, 2)->setFilters({NoiseFilter::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                   42.0f, BlendMode::SrcOver),
                             BlurFilter::Make(5.0f, 5.0f)});
  addRect(1, 2)->setFilters(
      {NoiseFilter::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                            Color::FromRGBA(0, 0, 255, 128), 43.0f, BlendMode::SrcOver),
       InnerShadowFilter::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});
  addRect(2, 2)->setFilters({NoiseFilter::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f, BlendMode::SrcOver),
                             DropShadowFilter::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  // Row 4: Row 2 styles + BlurFilter / InnerShadowFilter / DropShadowFilter.
  auto r4c0 = addRect(0, 3);
  r4c0->setLayerStyles({NoiseStyle::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128), 42.0f)});
  r4c0->setFilters({BlurFilter::Make(5.0f, 5.0f)});

  auto r4c1 = addRect(1, 3);
  r4c1->setLayerStyles({NoiseStyle::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                            Color::FromRGBA(0, 0, 255, 128), 43.0f)});
  r4c1->setFilters({InnerShadowFilter::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  auto r4c2 = addRect(2, 3);
  r4c2->setLayerStyles({NoiseStyle::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f)});
  r4c2->setFilters({DropShadowFilter::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  // Row 5: NoiseFilter with different BlendMode.
  addRect(0, 4)->setFilters({NoiseFilter::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                   42.0f, BlendMode::Multiply)});
  addRect(1, 4)->setFilters(
      {NoiseFilter::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                            Color::FromRGBA(0, 0, 255, 128), 43.0f, BlendMode::Overlay)});
  addRect(2, 4)->setFilters({NoiseFilter::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f, BlendMode::Screen)});

  // Row 6: NoiseStyle + DropShadowStyle combination.
  auto r6c0 = addRect(0, 5);
  r6c0->setLayerStyles({NoiseStyle::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128), 42.0f),
                        DropShadowStyle::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  auto r6c1 = addRect(1, 5);
  r6c1->setLayerStyles({NoiseStyle::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                            Color::FromRGBA(0, 0, 255, 128), 43.0f),
                        DropShadowStyle::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  auto r6c2 = addRect(2, 5);
  r6c2->setLayerStyles({NoiseStyle::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f),
                        DropShadowStyle::Make(5.0f, 5.0f, 5.0f, 5.0f, Color::Black())});

  // Row 7: TextLayer with NoiseFilter / NoiseStyle / both.
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));

  auto addText = [&](int col, int row) {
    auto textLayer = TextLayer::Make();
    textLayer->setText("Noise");
    textLayer->setTextColor(Color::FromRGBA(60, 120, 200));
    textLayer->setFont(Font(typeface, 32.f));
    float x = Margin + static_cast<float>(col) * (RectW + GapX);
    float y = Margin + static_cast<float>(row) * (RectH + GapY) + 20.f;
    textLayer->setMatrix(Matrix::MakeTrans(x, y));
    root->addChild(textLayer);
    return textLayer;
  };

  addText(0, 6)->setFilters({NoiseFilter::MakeMono(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                   42.0f, BlendMode::SrcOver)});
  addText(1, 6)->setLayerStyles({NoiseStyle::MakeDuo(8.0f, 0.5f, Color::FromRGBA(255, 0, 0, 128),
                                                     Color::FromRGBA(0, 0, 255, 128), 43.0f)});

  auto textBoth = addText(2, 6);
  textBoth->setLayerStyles({NoiseStyle::MakeMulti(8.0f, 0.5f, 0.5f, 44.0f)});
  textBoth->setFilters({BlurFilter::Make(3.0f, 3.0f)});

  // Export PDF with 2x pixel density for noise patterns.
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  float pageW = Margin * 2 + Cols * RectW + (Cols - 1) * GapX;
  float pageH = Margin * 2 + Rows * RectH + (Rows - 1) * GapY;
  constexpr float pdfDensity = 2.f;
  auto canvas = document->beginPage(pageW * pdfDensity, pageH * pdfDensity);
  canvas->scale(pdfDensity, pdfDensity);
  root->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/NoiseEffects"));

  // Render to surface for webp screenshot.
  auto bounds = root->getBounds(nullptr, true);
  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "PDFExportTest/NoiseEffects"));
}

TGFX_TEST(PDFExportTest, TextLayerStyleAlignment) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto root = Layer::Make();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));

  auto addText = [&](float x, float y) {
    auto textLayer = TextLayer::Make();
    textLayer->setText("Hello");
    textLayer->setTextColor(Color::FromRGBA(60, 120, 200));
    textLayer->setFont(Font(typeface, 40.f));
    textLayer->setMatrix(Matrix::MakeTrans(x, y));
    root->addChild(textLayer);
    return textLayer;
  };

  // Col 0: TextLayer + DropShadowStyle (Below position)
  addText(50.f, 50.f)->setLayerStyles({DropShadowStyle::Make(5.f, 5.f, 3.f, 3.f, Color::Black())});

  // Col 1: TextLayer + InnerShadowStyle (Above position) — positioned so half is outside page.
  addText(350.f, 50.f)
      ->setLayerStyles({InnerShadowStyle::Make(3.f, 3.f, 3.f, 3.f, Color::Black())});

  // Col 2: TextLayer + NoiseStyle (Above position)
  addText(50.f, 150.f)
      ->setLayerStyles({NoiseStyle::MakeMono(8.f, 0.5f, Color::FromRGBA(255, 0, 0, 128), 42.f)});

  // Col 3: TextLayer + DropShadowStyle + NoiseStyle (Below + Above) — half outside page.
  addText(350.f, 150.f)
      ->setLayerStyles({DropShadowStyle::Make(5.f, 5.f, 3.f, 3.f, Color::Black()),
                        NoiseStyle::MakeMono(8.f, 0.5f, Color::FromRGBA(255, 0, 0, 128), 42.f)});

  // Export PDF with fixed page width so right-column text is half inside, half outside.
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto bounds = root->getBounds(nullptr, true);
  float pageW = 400.f;
  float pageH = bounds.height() + 100.f;
  auto canvas = document->beginPage(pageW, pageH);
  canvas->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/TextLayerStyleAlignment"));

  // Render to surface for webp screenshot, matching page dimensions.
  auto surface = Surface::Make(context, static_cast<int>(pageW), static_cast<int>(pageH));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "PDFExportTest/TextLayerStyleAlignment"));
}

TGFX_TEST(PDFExportTest, TextMultiNoiseTransforms) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto root = Layer::Make();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));

  auto addText = [&](const std::string& text, const Matrix& matrix) {
    auto textLayer = TextLayer::Make();
    textLayer->setText(text);
    textLayer->setTextColor(Color::FromRGBA(60, 120, 200));
    textLayer->setFont(Font(typeface, 40.f));
    textLayer->setMatrix(matrix);
    textLayer->setAllowsEdgeAntialiasing(false);
    textLayer->setLayerStyles({NoiseStyle::MakeDuo(8.f, 1.f, Color::FromRGBA(200, 50, 50),
                                                   Color::FromRGBA(50, 200, 50), 42.f)});
    root->addChild(textLayer);
    return textLayer;
  };

  // Text 1: normal (translation only).
  addText("Hello", Matrix::MakeTrans(50.f, 50.f));

  // Text 2: rotated 30 degrees around its baseline origin.
  auto rotMatrix = Matrix::MakeTrans(50.f, 150.f);
  rotMatrix.preRotate(30.f, 50.f, 150.f);
  addText("Hello", rotMatrix);

  // Text 3: scaled 1.5x.
  auto scaleMatrix = Matrix::MakeTrans(50.f, 250.f);
  scaleMatrix.preScale(1.5f, 1.5f, 50.f, 250.f);
  addText("Hello", scaleMatrix);

  // Export PDF with 4x pixel density for noise patterns.
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto bounds = root->getBounds(nullptr, true);
  constexpr float pdfDensity = 4.f;
  float pageW = bounds.width() + 100.f;
  float pageH = bounds.height() + 100.f;
  auto canvas = document->beginPage(pageW * pdfDensity, pageH * pdfDensity);
  canvas->scale(pdfDensity, pdfDensity);
  canvas->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/TextMultiNoiseTransforms"));

  // Render to surface for webp screenshot.
  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "PDFExportTest/TextMultiNoiseTransforms"));

  // Save the DuoNoise content image for a single text layer.
  auto noiseRoot = Layer::Make();
  auto noiseTextLayer = TextLayer::Make();
  noiseTextLayer->setText("Hello");
  noiseTextLayer->setTextColor(Color::White());
  noiseTextLayer->setFont(Font(typeface, 40.f));
  noiseTextLayer->setAllowsEdgeAntialiasing(false);
  noiseTextLayer->setLayerStyles({NoiseStyle::MakeDuo(8.f, 1.f, Color::FromRGBA(200, 50, 50),
                                                      Color::FromRGBA(50, 200, 50), 42.f)});
  noiseRoot->addChild(noiseTextLayer);
  auto noiseBounds = noiseRoot->getBounds(nullptr, true);
  auto noiseSurface = Surface::Make(context, static_cast<int>(noiseBounds.width() + 20.f),
                                    static_cast<int>(noiseBounds.height() + 20.f));
  ASSERT_TRUE(noiseSurface != nullptr);
  noiseSurface->getCanvas()->clear(Color::Black());
  noiseSurface->getCanvas()->translate(-noiseBounds.left + 10.f, -noiseBounds.top + 10.f);
  noiseRoot->draw(noiseSurface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(noiseSurface, "PDFExportTest/DuoNoiseImage"));
}

TGFX_TEST(PDFExportTest, TextInnerShadow) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto root = Layer::Make();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 60.0f);

  // Text with InnerShadowStyle.
  auto textLayer = TextLayer::Make();
  textLayer->setText("text");
  textLayer->setTextColor(Color::FromRGBA(80, 160, 240));
  textLayer->setFont(font);
  textLayer->setLayerStyles({InnerShadowStyle::Make(3.f, 3.f, 3.f, 3.f, Color::Black())});
  root->addChild(textLayer);

  // Text with DuoNoiseStyle below.
  auto noiseTextLayer = TextLayer::Make();
  noiseTextLayer->setText("text");
  noiseTextLayer->setTextColor(Color::FromRGBA(80, 160, 240));
  noiseTextLayer->setFont(font);
  noiseTextLayer->setMatrix(Matrix::MakeTrans(0.f, 80.f));
  noiseTextLayer->setLayerStyles({NoiseStyle::MakeDuo(8.f, 1.f, Color::FromRGBA(200, 50, 50),
                                                      Color::FromRGBA(50, 200, 50), 42.f)});
  root->addChild(noiseTextLayer);

  auto bounds = root->getBounds(nullptr, true);

  // Export PDF with 4x pixel density.
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  constexpr float pdfDensity = 4.f;
  float pageW = bounds.width() + 100.f;
  float pageH = bounds.height() + 100.f;
  auto canvas = document->beginPage(pageW * pdfDensity, pageH * pdfDensity);
  canvas->scale(pdfDensity, pdfDensity);
  canvas->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/TextInnerShadow"));

  // Render to surface for webp screenshot.
  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "PDFExportTest/TextInnerShadow"));
}

TGFX_TEST(PDFExportTest, ShapeNoiseStyle) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto root = Layer::Make();
  auto noiseStyle = NoiseStyle::MakeMono(8.f, 0.5f, Color::FromRGBA(200, 50, 50, 128), 42.f);
  constexpr float ShapeW = 100.f;
  constexpr float ShapeH = 80.f;
  constexpr float GapX = 30.f;
  constexpr float GapY = 30.f;
  constexpr float Margin = 50.f;

  // Row 0: PathContent (custom bezier path)
  {
    Path path;
    path.moveTo(0, ShapeH * 0.5f);
    path.cubicTo(ShapeW * 0.25f, 0, ShapeW * 0.75f, ShapeH, ShapeW, ShapeH * 0.5f);
    path.close();
    auto layer = ShapeLayer::Make();
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(80, 160, 240)));
    layer->setMatrix(Matrix::MakeTrans(Margin, Margin));
    layer->setLayerStyles({noiseStyle});
    root->addChild(layer);
  }

  // Row 0 col 1: RectContent (addRect)
  {
    Path path;
    path.addRect(Rect::MakeWH(ShapeW, ShapeH));
    auto layer = ShapeLayer::Make();
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(80, 160, 240)));
    layer->setMatrix(Matrix::MakeTrans(Margin + static_cast<float>(1) * (ShapeW + GapX), Margin));
    layer->setLayerStyles({noiseStyle});
    root->addChild(layer);
  }

  // Row 0 col 2: RRectContent (addOval)
  {
    Path path;
    path.addOval(Rect::MakeWH(ShapeW, ShapeH));
    auto layer = ShapeLayer::Make();
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(80, 160, 240)));
    layer->setMatrix(Matrix::MakeTrans(Margin + static_cast<float>(2) * (ShapeW + GapX), Margin));
    layer->setLayerStyles({noiseStyle});
    root->addChild(layer);
  }

  // Row 1 col 0: ShapeContent (Shape::MakeFrom rect)
  {
    Path shapePath;
    shapePath.addRect(Rect::MakeWH(ShapeW, ShapeH));
    auto shape = Shape::MakeFrom(shapePath);
    auto layer = ShapeLayer::Make();
    layer->setShape(shape);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(240, 160, 80)));
    layer->setMatrix(Matrix::MakeTrans(Margin, Margin + static_cast<float>(1) * (ShapeH + GapY)));
    layer->setLayerStyles({noiseStyle});
    root->addChild(layer);
  }

  // Row 1 col 1: ComposeContent (multiple sublayers in a container layer)
  {
    auto container = Layer::Make();
    Path rectPath;
    rectPath.addRect(Rect::MakeWH(ShapeW * 0.4f, ShapeH));
    auto sub1 = ShapeLayer::Make();
    sub1->setPath(rectPath);
    sub1->setFillStyle(ShapeStyle::Make(Color::FromRGBA(80, 200, 120)));
    container->addChild(sub1);

    Path rectPath2;
    rectPath2.addRect(Rect::MakeXYWH(static_cast<float>(ShapeW * 0.6), 0.f,
                                     static_cast<float>(ShapeW * 0.4), ShapeH));
    auto sub2 = ShapeLayer::Make();
    sub2->setPath(rectPath2);
    sub2->setFillStyle(ShapeStyle::Make(Color::FromRGBA(80, 200, 120)));
    container->addChild(sub2);

    container->setMatrix(Matrix::MakeTrans(Margin + static_cast<float>(1) * (ShapeW + GapX),
                                           Margin + static_cast<float>(1) * (ShapeH + GapY)));
    container->setLayerStyles({noiseStyle});
    root->addChild(container);
  }

  // Row 1 col 2: MatrixContent (rotated layer)
  {
    Path path;
    path.addRRect(RRect::MakeRectXY(Rect::MakeWH(ShapeW, ShapeH), 10.f, 10.f));
    auto layer = ShapeLayer::Make();
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(180, 80, 200)));
    auto matrix =
        Matrix::MakeTrans(Margin + static_cast<float>(2) * (ShapeW + GapX) + ShapeW * 0.5f,
                          Margin + static_cast<float>(1) * (ShapeH + GapY) + ShapeH * 0.5f);
    matrix.preRotate(15.f);
    matrix.preTranslate(-ShapeW * 0.5f, -ShapeH * 0.5f);
    layer->setMatrix(matrix);
    layer->setLayerStyles({noiseStyle});
    root->addChild(layer);
  }

  auto bounds = root->getBounds(nullptr, true);

  // Export PDF.
  auto PDFStream = MemoryWriteStream::Make();
  auto document = PDFDocument::Make(PDFStream, context, PDFMetadata());
  auto canvas = document->beginPage(bounds.width() + 100.f, bounds.height() + 100.f);
  canvas->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(canvas);
  document->endPage();
  document->close();
  PDFStream->flush();

  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ShapeNoiseStyle"));

  // Render to surface for webp screenshot.
  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "PDFExportTest/ShapeNoiseStyle"));
}

}  // namespace tgfx
