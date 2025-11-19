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
#include "base/TGFXTest.h"
#include "core/utils/MD5.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/core/WriteStream.h"
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

TGFX_TEST(PDFExportTest, ColorSpaceTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto PDFStream = MemoryWriteStream::Make();

  PDFMetadata metadata;
  auto document =
      PDFDocument::Make(PDFStream, context, metadata, ColorSpaceConverter::MakeDefaultConverter());
  auto canvas = document->beginPage(256.f, 256.f);
  canvas->drawColor(Color::FromRGBA(
      0, 255, 0, 255, ColorSpace::MakeRGB(NamedTransferFunction::SRGB, NamedGamut::DisplayP3)));
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
  EXPECT_TRUE(ComparePDF(PDFStream, "PDFTest/ColorSpace"));
}

}  // namespace tgfx
