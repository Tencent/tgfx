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

#include <filesystem>
#include <memory>
#include "base/TGFXTest.h"
#include "core/Records.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUnion.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/pdf/PDFMetadata.h"
#include "utils/ProjectPath.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(PDFExportTest, EmptyPDF) {
  auto path = ProjectPath::Absolute("test/out/EmptyPDF.pdf");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());
  auto PDFStream = WriteStream::MakeFromFile(path);

  PDFMetadata metadata;
  metadata.title = "Empty PDF";
  // metadata.PDFA = true;
  metadata.compressionLevel = PDFMetadata::CompressionLevel::Average;
  // metadata.compressionLevel = PDFMetadata::CompressionLevel::None;

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto document = MakePDFDocument(PDFStream, context, metadata);
  auto* canvas = document->beginPage(1000.f, 250.f);
  canvas->translate(40.0, 20.0);

  {
    Paint strokePaint;
    strokePaint.setColor(Color::Blue());
    Stroke stroke;
    stroke.width = 25.0f;
    strokePaint.setStroke(stroke);

    auto typeface =
        Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
    Font font(typeface, 200.f);
    Paint textPaint;
    textPaint.setColor(Color::Black());

    bool success = false;
    std::shared_ptr<Path> path = nullptr;

    //T
    std::tie(success, path) =
        PathMakeFromSVGString("M114.5 206L228.382 8.74997H0.617676L114.5 206Z");
    if (success) {
      Paint paint;
      // auto image = MakeImage("resources/apitest/apple.png");
      // auto imageShader = Shader::MakeImageShader(image);
      // paint.setShader(imageShader);
      paint.setColor(Color::Blue());
      // auto imageFilter = ImageFilter::InnerShadow(10, 10, 50, 50, Color::Black());
      // paint.setImageFilter(imageFilter);
      // auto blurFilter = ImageFilter::Blur(25, 25, TileMode::Decal);
      // paint.setImageFilter(blurFilter);
      canvas->drawPath(*path, paint);

      strokePaint.setStyle(PaintStyle::Stroke);
      auto gradientShader = tgfx::Shader::MakeLinearGradient(
          tgfx::Point{0.f, 0.f}, tgfx::Point{0.f, 200.f},
          {tgfx::Color::FromRGBA(157, 239, 132), tgfx::Color::FromRGBA(255, 156, 69)}, {});
      strokePaint.setShader(gradientShader);
      auto blurFilter = ImageFilter::Blur(25, 25, TileMode::Decal);
      strokePaint.setImageFilter(blurFilter);
      canvas->drawPath(*path, strokePaint);
    }
    //char T
    { canvas->drawSimpleText("T", 55, 175, font, textPaint); }

    //G
    std::tie(success, path) = PathMakeFromSVGString(
        "M423 106C423 125.778 417.135 145.112 406.147 161.557C395.159 178.002 379.541 190.819 "
        "361.268 198.388C342.996 205.957 322.889 207.937 303.491 204.078C284.093 200.22 266.275 "
        "190.696 252.289 176.711C238.304 162.725 228.78 144.907 224.921 125.509C221.063 106.111 "
        "223.043 86.0042 230.612 67.7316C238.181 49.459 250.998 33.8411 267.443 22.853C283.888 "
        "11.8649 303.222 5.99997 323 5.99997L323 106H423Z");
    if (success) {
      Paint paint;
      tgfx::Point center{path->getBounds().centerX() + 25.f, path->getBounds().centerY() + 25.f};
      auto gradientShader = tgfx::Shader::MakeRadialGradient(
          center, 75, {tgfx::Color::FromRGBA(69, 151, 247), tgfx::Color::FromRGBA(130, 228, 153)},
          {0, 1.0});
      paint.setShader(gradientShader);

      auto imageFilter = ImageFilter::InnerShadow(20, 20, 35, 35, Color::Red());
      paint.setImageFilter(imageFilter);

      canvas->drawPath(*path, paint);

      // strokePaint.setStyle(PaintStyle::Stroke);
      // strokePaint.setColor(Color::FromRGBA(232, 133, 133));
      // strokePaint.setShader(nullptr);
      // canvas->drawPath(*path, strokePaint);
    }
    //char G
    { canvas->drawSimpleText("G", 250, 175, font, textPaint); }

    //X
    std::tie(success, path) = PathMakeFromSVGString(
        "M917.168 0.0357666L866.632 106.116L917.228 212.168L811.148 161.632L705.096 "
        "212.228L755.632 106.148L705.036 0.0961968L811.116 50.632L917.168 0.0357666Z");
    if (success) {
      Paint paint;
      paint.setColor(Color::FromRGBA(230, 234, 147));

      auto imageFilter = ImageFilter::DropShadow(-20, -20, 35, 35, Color::Blue());
      paint.setImageFilter(imageFilter);

      auto maskImage = MakeImage("resources/apitest/apple.png");
      auto maskShader =
          Shader::MakeImageShader(std::move(maskImage), TileMode::Repeat, TileMode::Repeat);
      auto maskFilter = MaskFilter::MakeShader(std::move(maskShader));
      paint.setMaskFilter(std::move(maskFilter));

      canvas->drawPath(*path, paint);

      // strokePaint.setStyle(PaintStyle::Stroke);
      // strokePaint.setColor(Color::Blue());
      // strokePaint.setShader(nullptr);
      // canvas->drawPath(*path, strokePaint);
    }
    //char X
    {
      textPaint.setStyle(PaintStyle::Stroke);
      textPaint.setStrokeWidth(10.f);
      canvas->drawSimpleText("X", 750, 175, font, textPaint);
    }

    //F
    std::tie(success, path) = PathMakeFromSVGString("M656 5.99997H456V206H536V86H656V5.99997Z");
    if (success) {
      Paint paint;
      auto image = MakeImage("resources/apitest/spotify.png");
      auto imageShader = Shader::MakeImageShader(image);
      paint.setShader(imageShader);
      canvas->drawPath(*path, paint);

      strokePaint.setStyle(PaintStyle::Stroke);
      auto gradientShader = tgfx::Shader::MakeLinearGradient(
          tgfx::Point{0.f, 0.f}, tgfx::Point{0.f, 200.f},
          {tgfx::Color::FromRGBA(157, 239, 132), tgfx::Color::FromRGBA(255, 156, 69)}, {});
      strokePaint.setShader(gradientShader);
      canvas->drawPath(*path, strokePaint);
    }
    //char X
    { canvas->drawSimpleText("F", 500, 175, font, textPaint); }
  }

  document->endPage();
  document->close();
  PDFStream->flush();
}

TGFX_TEST(PDFExportTest, trySomething) {
  auto pdfPath = ProjectPath::Absolute("test/out/EmptyPDF.pdf");
  std::filesystem::path filePath = pdfPath;
  std::filesystem::create_directories(filePath.parent_path());
  auto PDFStream = WriteStream::MakeFromFile(pdfPath);

  PDFMetadata metadata;
  metadata.title = "Empty PDF";
  metadata.compressionLevel = PDFMetadata::CompressionLevel::Average;

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto document = MakePDFDocument(PDFStream, context, metadata);
  auto* canvas = document->beginPage(1000.f, 250.f);

  Paint colorPaint;
  colorPaint.setColor(Color::Blue());

  Paint filterPaint;
  auto imageFilter = ImageFilter::InnerShadowOnly(50, 50, 20, 20, Color::Red());
  filterPaint.setImageFilter(imageFilter);
  // auto blurFilter = ImageFilter::Blur(25, 25, TileMode::Decal);
  // filterPaint.setImageFilter(blurFilter);

  Recorder recorder;
  auto* s = recorder.beginRecording();
  bool success = false;
  std::shared_ptr<Path> path = nullptr;
  std::tie(success, path) = PathMakeFromSVGString("M114.5 206L228.382 8.74997H0.617676L114.5 206Z");
  s->drawPath(*path, colorPaint);
  auto picture = recorder.finishRecordingAsPicture();

  canvas->saveLayer(&filterPaint);
  // for (auto* record : picture->records) {
  //   record->playback(canvas->drawContext);
  // }
  canvas->translate(50, 50);
  // canvas->drawPicture(picture);
  canvas->drawPath(*path, colorPaint);
  canvas->restore();
}

TGFX_TEST(PDFExportTest, X) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 500, 500);
  auto* canvas = surface->getCanvas();
  canvas->clear();

  auto image = MakeImage("resources/apitest/apple.png");

  Paint paint;
  auto maskShader = Shader::MakeImageShader(std::move(image), TileMode::Repeat, TileMode::Repeat);
  paint.setShader(maskShader);
  // canvas->drawRect(Rect::MakeWH(500, 500), paint);
  canvas->drawPaint(paint);

  // canvas->drawImage(image);

  EXPECT_TRUE(Baseline::Compare(surface, "PDFTest/X"));
}

TGFX_TEST(PDFExportTest, tryMask) {
  auto path = ProjectPath::Absolute("test/out/EmptyPDF.pdf");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());
  auto PDFStream = WriteStream::MakeFromFile(path);

  PDFMetadata metadata;
  metadata.title = "Empty PDF";
  metadata.compressionLevel = PDFMetadata::CompressionLevel::None;

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto document = MakePDFDocument(PDFStream, context, metadata);
  auto* canvas = document->beginPage(1000.f, 250.f);

  // Recorder recorder;
  // auto* pictureCanvas = recorder.beginRecording();
  // Paint picturePaint;
  // auto gradientShader = tgfx::Shader::MakeLinearGradient(
  //     tgfx::Point{0.f, 0.f}, tgfx::Point{200.f, 200.f},
  //     {tgfx::Color::FromRGBA(255, 255, 255, 255), tgfx::Color::FromRGBA(0, 0, 0, 120)}, {0.0, 1.0});
  // picturePaint.setShader(gradientShader);
  // pictureCanvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), picturePaint);
  // auto picture = recorder.finishRecordingAsPicture();
  // auto image = Image::MakeFrom(picture, 200, 200);

  auto image = MakeImage("resources/apitest/apple.png");
  image = image->makeTextureImage(context);

  Paint paint;
  paint.setColor(Color::Blue());
  auto maskShader = Shader::MakeImageShader(std::move(image), TileMode::Repeat, TileMode::Repeat);
  auto maskFilter = MaskFilter::MakeShader(std::move(maskShader));
  maskFilter = maskFilter->makeWithMatrix(Matrix::MakeTrans(45, 45));
  paint.setMaskFilter(std::move(maskFilter));

  canvas->drawRect(Rect::MakeWH(150, 150), paint);
  // canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), picturePaint);

  document->endPage();
  document->close();
  PDFStream->flush();
}

}  // namespace tgfx