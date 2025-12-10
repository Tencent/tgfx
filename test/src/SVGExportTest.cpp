/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include <string>
#include "base/TGFXTest.h"
#include "gtest/gtest.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/svg/SVGExporter.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {
bool CompareSVG(const std::shared_ptr<MemoryWriteStream>& stream, const std::string& key) {
  auto data = stream->readData();
#ifdef GENERATE_BASELINE_IMAGES
  SaveFile(data, key + "_base.svg");
#endif
  auto result = Baseline::Compare(data, key);
  if (result) {
    RemoveFile(key + ".svg");
  } else {
    SaveFile(data, key + ".svg");
  }
  return result;
}
}  // namespace

TGFX_TEST(SVGExportTest, PureColor) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#00F\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, PureColorFile) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#00F\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  auto path = ProjectPath::Absolute("test/out/FileWrite.txt");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());

  auto SVGStream = WriteStream::MakeFromFile(path);
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();

  auto readStream = Stream::MakeFromFile(path);
  EXPECT_TRUE(readStream != nullptr);
  EXPECT_EQ(readStream->size(), 211U);
  Buffer buffer(readStream->size());
  readStream->read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);

  std::filesystem::remove(path);
}

TGFX_TEST(SVGExportTest, OpacityColor) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><circle "
      "fill=\"#00F\" fill-opacity=\"0.5\" cx=\"100\" cy=\"100\" r=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setColor(Color::Blue());
  paint.setAlpha(0.5f);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawCircle(100, 100, 100, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, OpacityColorFile) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><circle "
      "fill=\"#00F\" fill-opacity=\"0.5\" cx=\"100\" cy=\"100\" r=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto path = ProjectPath::Absolute("test/out/FileWrite.txt");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());

  tgfx::Paint paint;
  paint.setColor(Color::Blue());
  paint.setAlpha(0.5f);

  auto SVGStream = WriteStream::MakeFromFile(path);
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawCircle(100, 100, 100, paint);

  exporter->close();

  auto readStream = Stream::MakeFromFile(path);
  EXPECT_TRUE(readStream != nullptr);
  EXPECT_EQ(readStream->size(), 219U);
  Buffer buffer(readStream->size());
  readStream->read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);

  std::filesystem::remove(path);
}

TGFX_TEST(SVGExportTest, LinearGradient) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><linearGradient id=\"gradient_0\" gradientUnits=\"userSpaceOnUse\" "
      "x1=\"50\" y1=\"50\" x2=\"150\" y2=\"150\"><stop offset=\"0\" stop-color=\"#0F0\"/><stop "
      "offset=\"1\" stop-color=\"#000\"/></linearGradient></defs><circle fill=\"url(#gradient_0)\" "
      "cx=\"100\" cy=\"100\" r=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  auto shader = tgfx::Shader::MakeLinearGradient(
      tgfx::Point{50.f, 50.f}, tgfx::Point{150.f, 150.f},
      {tgfx::Color{0.f, 1.f, 0.f, 1.f}, tgfx::Color{0.f, 0.f, 0.f, 1.f}}, {});
  paint.setShader(shader);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawCircle(100, 100, 100, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, RadialGradient) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><radialGradient id=\"gradient_0\" gradientUnits=\"userSpaceOnUse\" "
      "r=\"50\" cx=\"100\" cy=\"100\"><stop offset=\"0\" stop-color=\"#F00\"/><stop offset=\"0.5\" "
      "stop-color=\"#00F\"/><stop offset=\"1\" stop-color=\"#000\"/></radialGradient></defs><rect "
      "fill=\"url(#gradient_0)\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeRadialGradient(
      center, 50, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()}, {0, 0.5, 1.0});
  paint.setShader(shader);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, UnsupportedGradient) {

  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><radialGradient id=\"gradient_0\" gradientUnits=\"userSpaceOnUse\" "
      "r=\"0\" cx=\"100\" cy=\"100\"><stop offset=\"0\" stop-color=\"#F00\"/><stop offset=\"0.5\" "
      "stop-color=\"#00F\"/><stop offset=\"1\" stop-color=\"#000\"/></radialGradient></defs><rect "
      "fill=\"url(#gradient_0)\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeConicGradient(
      center, 0, 360, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()},
      {0, 0.5, 1.0});
  paint.setShader(shader);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, BlendMode) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#FFF\" width=\"100\" height=\"100\"/><rect fill=\"#F00\" fill-opacity=\"1\" "
      "style=\"mix-blend-mode:difference\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paintBackground;
  paintBackground.setColor(Color::White());

  Paint paint;
  paint.setColor(Color::Red());
  paint.setBlendMode(BlendMode::Difference);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(tgfx::Rect::MakeXYWH(0, 0, 100, 100), paintBackground);
  canvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, StrokeWidth) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#F00\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(5);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, SimpleTextAsText) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"400\" height=\"200\"><text "
      "fill=\"#F00\" transform=\"matrix(1 0 0 1 0 80)\" font-size=\"50\" font-family=\"Noto Serif "
      "SC\" x=\"0, 43, 70, 86, 102, 132, 145, 178, 215, 246, \" y=\"0, \">Hello TGFX</text></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 50.f);
  Paint paint;
  paint.setColor(Color::Red());

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawSimpleText("Hello TGFX", 0, 80, font, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, SimpleTextAsPath) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"400\" height=\"200\"><path "
      "fill=\"#F00\" transform=\"matrix(1 0 0 1 0 80)\" d=\"M5 -0L9.6406 0L9.6406 -17L26.75 "
      "-17L26.75 0L31.3438 0L31.3438 -37L26.75 -37L26.75 -21L9.6406 -21L9.6406 -37L5 -37L5 "
      "0ZM40.5938 0L45.1406 0L45.1406 -27L40.5938 -27L40.5938 0ZM42.8906 -32.9844C44.6875 -32.9844 "
      "45.9375 -34.1875 45.9375 -36.0469C45.9375 -37.7969 44.6875 -39 42.8906 -39C41.0938 -39 "
      "39.8438 -37.7969 39.8438 -36.0469C39.8438 -34.1875 41.0938 -32.9844 42.8906 "
      "-32.9844Z\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  Font font(typeface, 50.f);
  Paint paint;
  paint.setColor(Color::Red());

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter =
      SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 200),
                        SVGExportFlags::ConvertTextToPaths | SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawSimpleText("Hi", 0, 80, font, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, EmojiText) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"400\" height=\"200\"><text "
      "fill=\"#F00\" transform=\"matrix(1 0 0 1 0 80)\" font-size=\"50\" font-family=\"Noto Color "
      "Emoji\" x=\"0, 62.3906, 124.7812, 187.1719, 249.5625, 311.9531, \" y=\"0, "
      "\">🤡👻🐠🤩😃🤪</text></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  Font font(typeface, 50.f);
  Paint paint;
  paint.setColor(Color::Red());

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawSimpleText("🤡👻🐠🤩😃🤪", 0, 80, font, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, EmojiTextFile) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"400\" height=\"200\"><text "
      "fill=\"#F00\" transform=\"matrix(1 0 0 1 0 80)\" font-size=\"50\" font-family=\"Noto Color "
      "Emoji\" x=\"0, 62.3906, 124.7812, 187.1719, 249.5625, 311.9531, \" y=\"0, "
      "\">🤡👻🐠🤩😃🤪</text></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto path = ProjectPath::Absolute("test/out/FileWrite.txt");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  Font font(typeface, 50.f);
  Paint paint;
  paint.setColor(Color::Red());

  auto SVGStream = WriteStream::MakeFromFile(path);
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  canvas->drawSimpleText("🤡👻🐠🤩😃🤪", 0, 80, font, paint);

  exporter->close();

  auto readStream = Stream::MakeFromFile(path);
  EXPECT_TRUE(readStream != nullptr);
  EXPECT_EQ(readStream->size(), 346U);
  Buffer buffer(readStream->size());
  readStream->read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);

  std::filesystem::remove(path);
}

TGFX_TEST(SVGExportTest, ClipState) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"300\" height=\"300\"><clipPath "
      "id=\"clip_0\"><rect width=\"100\" height=\"100\"/></clipPath><g "
      "clip-path=\"url(#clip_0)\"><rect fill=\"#F00\" width=\"200\" height=\"200\"/></g><clipPath "
      "id=\"clip_1\"><circle cx=\"150\" cy=\"150\" r=\"50\"/></clipPath><g "
      "clip-path=\"url(#clip_1)\"><rect fill=\"#0F0\" x=\"100\" y=\"100\" width=\"200\" "
      "height=\"200\"/></g><rect fill=\"#00F\" x=\"200\" y=\"200\" width=\"100\" "
      "height=\"100\"/></svg>";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  {
    Paint paint;
    paint.setColor(Color::Red());
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(0, 0, 100, 100));
    canvas->drawRect(Rect::MakeXYWH(0, 0, 200, 200), paint);
    canvas->restore();

    paint.setColor(Color::Green());
    canvas->save();
    Path path;
    path.addOval(Rect::MakeXYWH(100, 100, 100, 100));
    canvas->clipPath(path);
    canvas->drawRect(Rect::MakeXYWH(100, 100, 200, 200), paint);
    canvas->restore();

    paint.setColor(Color::Blue());
    canvas->save();
    canvas->drawRect(Rect::MakeXYWH(200, 200, 100, 100), paint);
    canvas->restore();
  }

  exporter->close();
  auto SVGString = SVGStream->readString();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, GradientMask) {

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300));
  auto canvas = exporter->getCanvas();

  {
    Paint paint;
    auto shader = tgfx::Shader::MakeLinearGradient(
        tgfx::Point{0.f, 0.f}, tgfx::Point{300.f, 300.f},
        {tgfx::Color{1.f, 1.f, 1.f, 1.f}, tgfx::Color{0.f, 0.f, 0.f, 0.f}}, {});
    auto maskFilter = tgfx::MaskFilter::MakeShader(shader);
    paint.setMaskFilter(maskFilter);
    paint.setColor(Color::Red());

    canvas->drawRect(Rect::MakeWH(300, 300), paint);
  }

  exporter->close();
  auto SVGString = SVGStream->readString();

  auto path = ProjectPath::Absolute("resources/apitest/mask_gradient.svg");
  auto readStream = Stream::MakeFromFile(path);
  EXPECT_TRUE(readStream != nullptr);
  Buffer buffer(readStream->size());
  readStream->read(buffer.data(), buffer.size());
  auto compareString = std::string(static_cast<char*>(buffer.data()), buffer.size());

  EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), SVGString);
}

TGFX_TEST(SVGExportTest, ImageMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300));
  auto canvas = exporter->getCanvas();

  {
    Paint paint;
    auto image = MakeImage("resources/apitest/image_as_mask.png");
    ASSERT_TRUE(image != nullptr);
    auto shader = tgfx::Shader::MakeImageShader(image);
    ASSERT_TRUE(shader != nullptr);
    auto maskFilter = tgfx::MaskFilter::MakeShader(shader);
    ASSERT_TRUE(maskFilter != nullptr);

    paint.setMaskFilter(maskFilter);
    paint.setColor(Color::Red());

    canvas->drawRect(Rect::MakeWH(200, 200), paint);
  }

  exporter->close();
  CompareSVG(SVGStream, "SVGExportTest/ImageMask");
}

TGFX_TEST(SVGExportTest, PictureImageMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300));
  auto canvas = exporter->getCanvas();

  {
    PictureRecorder recorder;
    auto pictureCanvas = recorder.beginRecording();
    {
      tgfx::Paint paint;
      pictureCanvas->drawCircle(50, 50, 50, paint);
      paint.setAlpha(0.5f);
      pictureCanvas->drawCircle(150, 150, 50, paint);
    }
    auto picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(picture != nullptr);
    auto image = tgfx::Image::MakeFrom(picture, 125, 125);
    ASSERT_TRUE(image != nullptr);

    Paint paint;
    auto shader = tgfx::Shader::MakeImageShader(image);
    ASSERT_TRUE(shader != nullptr);
    auto maskFilter = tgfx::MaskFilter::MakeShader(shader);
    ASSERT_TRUE(maskFilter != nullptr);

    paint.setMaskFilter(maskFilter);
    paint.setColor(Color::Red());

    canvas->drawRect(Rect::MakeXYWH(50.0f, 50.0f, 100.f, 100.f), paint);
  }

  exporter->close();
  CompareSVG(SVGStream, "SVGExportTest/PictureImageMask");
}

TGFX_TEST(SVGExportTest, InvertPictureImageMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300));
  auto canvas = exporter->getCanvas();

  {
    PictureRecorder recorder;
    auto pictureCanvas = recorder.beginRecording();
    {
      tgfx::Paint paint;
      pictureCanvas->drawCircle(50, 50, 50, paint);
      paint.setAlpha(0.5f);
      pictureCanvas->drawCircle(150, 150, 50, paint);
    }
    auto picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(picture != nullptr);
    auto image = tgfx::Image::MakeFrom(picture, 200, 200);
    ASSERT_TRUE(image != nullptr);

    Paint paint;
    auto shader = tgfx::Shader::MakeImageShader(image);
    ASSERT_TRUE(shader != nullptr);
    auto maskFilter = tgfx::MaskFilter::MakeShader(shader, true);
    ASSERT_TRUE(maskFilter != nullptr);

    paint.setMaskFilter(maskFilter);
    paint.setColor(Color::Red());

    canvas->drawRect(Rect::MakeXYWH(50.0f, 50.0f, 100.f, 100.f), paint);
  }

  exporter->close();
  CompareSVG(SVGStream, "SVGExportTest/InvertPictureImageMask");
}

TGFX_TEST(SVGExportTest, DrawImageRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();

  int width = 400;
  int height = 400;
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(width, height));
  auto canvas = exporter->getCanvas();

  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  Rect srcRect = Rect::MakeWH(image->width(), image->height());
  Rect dstRect = Rect::MakeXYWH(0, 0, width / 2, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Linear));

  srcRect = Rect::MakeXYWH(20, 20, 60, 60);
  dstRect = Rect::MakeXYWH(width / 2, 0, width / 2, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Nearest));

  srcRect = Rect::MakeXYWH(40, 40, 40, 40);
  dstRect = Rect::MakeXYWH(0, height / 2, width, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Linear));

  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/DrawImageRect"));
}

TGFX_TEST(SVGExportTest, LayerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 400));
  auto canvas = exporter->getCanvas();

  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setMatrix(Matrix::MakeTrans(30, 30));

  auto dropShadowLayer = ShapeLayer::Make();
  auto dropShadowStyle = DropShadowStyle::Make(10, 10, 10, 10, Color::White(), false);
  Path rect;
  rect.addRect(Rect::MakeWH(50, 50));
  dropShadowLayer->setPath(rect);
  dropShadowLayer->setFillStyle(SolidColor::Make(Color::Red()));
  dropShadowLayer->setLayerStyles({dropShadowStyle});
  rootLayer->addChild(dropShadowLayer);

  auto innerShadowLayer = ShapeLayer::Make();
  auto innerShadowStyle = InnerShadowStyle::Make(10, 10, 10, 10, Color::White());
  innerShadowLayer->setMatrix(Matrix::MakeTrans(200, 0));
  innerShadowLayer->setPath(rect);
  innerShadowLayer->setFillStyle(SolidColor::Make(Color::Red()));
  innerShadowLayer->setLayerStyles({innerShadowStyle});
  rootLayer->addChild(innerShadowLayer);

  displayList->root()->addChild(rootLayer);
  displayList->root()->draw(canvas);

  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/LayerDropShadow"));
}

TGFX_TEST(SVGExportTest, DstColorSpace) {
    ContextScope scope;
    auto context = scope.getContext();
    EXPECT_TRUE(context != nullptr);

    auto SVGStream = MemoryWriteStream::Make();
    auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 400), 0, nullptr, ColorSpace::DisplayP3());
    auto canvas = exporter->getCanvas();
    canvas->drawColor(Color::Green(), BlendMode::Difference);
    exporter->close();
    EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/DstColorSpace"));
}
}  // namespace tgfx
