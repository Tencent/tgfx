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

#include <algorithm>
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
#include "tgfx/core/Surface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/svg/SVGExporter.h"
#include "tgfx/svg/SVGPathParser.h"
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

  // The writer must be destroyed before reading. On Windows, fopen() holds an exclusive file lock
  // that prevents a second fopen() on the same path until fclose() is called in the destructor.
  {
    auto SVGStream = WriteStream::MakeFromFile(path);
    auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                      SVGExportFlags::DisablePrettyXML);
    auto canvas = exporter->getCanvas();

    canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);

    exporter->close();
  }

  {
    auto readStream = Stream::MakeFromFile(path);
    EXPECT_TRUE(readStream != nullptr);
    EXPECT_EQ(readStream->size(), 211U);
    Buffer buffer(readStream->size());
    readStream->read(buffer.data(), buffer.size());
    EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);
  }

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

  // The writer must be destroyed before reading. On Windows, fopen() holds an exclusive file lock
  // that prevents a second fopen() on the same path until fclose() is called in the destructor.
  {
    auto SVGStream = WriteStream::MakeFromFile(path);
    auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                      SVGExportFlags::DisablePrettyXML);
    auto canvas = exporter->getCanvas();

    canvas->drawCircle(100, 100, 100, paint);

    exporter->close();
  }

  {
    auto readStream = Stream::MakeFromFile(path);
    EXPECT_TRUE(readStream != nullptr);
    EXPECT_EQ(readStream->size(), 219U);
    Buffer buffer(readStream->size());
    readStream->read(buffer.data(), buffer.size());
    EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);
  }

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
      "style=\"mix-blend-mode:difference;\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

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

  // The writer must be destroyed before reading. On Windows, fopen() holds an exclusive file lock
  // that prevents a second fopen() on the same path until fclose() is called in the destructor.
  {
    auto SVGStream = WriteStream::MakeFromFile(path);
    auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(400, 200),
                                      SVGExportFlags::DisablePrettyXML);
    auto canvas = exporter->getCanvas();

    canvas->drawSimpleText("🤡👻🐠🤩😃🤪", 0, 80, font, paint);

    exporter->close();
  }

  {
    auto readStream = Stream::MakeFromFile(path);
    EXPECT_TRUE(readStream != nullptr);
    EXPECT_EQ(readStream->size(), 346U);
    Buffer buffer(readStream->size());
    readStream->read(buffer.data(), buffer.size());
    EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), compareString);
  }

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
  // Only this code path reads a baseline SVG file from disk, which may contain '\r\n' line endings
  // on Windows due to Git checkout CRLF conversion. Strip '\r' so it matches the runtime-generated
  // SVG string that uses '\n' only. Other SVG tests compare against in-memory strings and are not
  // affected by CRLF conversion.
  compareString.erase(std::remove(compareString.begin(), compareString.end(), '\r'),
                      compareString.end());

  EXPECT_EQ(compareString, SVGString);
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
  dropShadowLayer->setFillStyle(ShapeStyle::Make(Color::Red()));
  dropShadowLayer->setLayerStyles({dropShadowStyle});
  rootLayer->addChild(dropShadowLayer);

  auto innerShadowLayer = ShapeLayer::Make();
  auto innerShadowStyle = InnerShadowStyle::Make(10, 10, 10, 10, Color::White());
  innerShadowLayer->setMatrix(Matrix::MakeTrans(200, 0));
  innerShadowLayer->setPath(rect);
  innerShadowLayer->setFillStyle(ShapeStyle::Make(Color::Red()));
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
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  image = image->makeScaled(400, 400);
  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(2048, 2048), 0, nullptr,
                                    ColorSpace::DisplayP3());
  auto canvas = exporter->getCanvas();
  Paint paint;
  paint.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(20, 20, 100, 100), paint);
  canvas->drawRRect(RRect::MakeOval(Rect::MakeXYWH(140, 20, 100, 100)), paint);
  canvas->drawImageRect(image, Rect::MakeXYWH(260, 20, 100, 100), {}, &paint);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 50.f);
  canvas->drawSimpleText("TGFX", 400, 80, font, paint);
  paint.setShader(Shader::MakeColorShader(Color::Red()));
  canvas->drawRect(Rect::MakeXYWH(580, 20, 100, 100), paint);
  paint.setShader(Shader::MakeLinearGradient({720, 20}, {820, 20}, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(720, 20, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({70, 190}, 50, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(20, 140, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({190, 190}, 50, {Color::Green(), Color::Red()}));
  paint.setColorFilter(ColorFilter::Blend(Color::White(), BlendMode::Difference));
  canvas->drawRect(Rect::MakeXYWH(140, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::DropShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(260, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::InnerShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(400, 140, 100, 100), paint);
  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/DstColorSpace"));
}

TGFX_TEST(SVGExportTest, AssignColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  image = image->makeScaled(400, 400);
  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(2048, 2048), 0, nullptr,
                                    nullptr, ColorSpace::SRGB());
  auto canvas = exporter->getCanvas();
  Paint paint;
  paint.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(20, 20, 100, 100), paint);
  canvas->drawRRect(RRect::MakeOval(Rect::MakeXYWH(140, 20, 100, 100)), paint);
  canvas->drawImageRect(image, Rect::MakeXYWH(260, 20, 100, 100), {}, &paint);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 50.f);
  canvas->drawSimpleText("TGFX", 400, 80, font, paint);
  paint.setShader(Shader::MakeColorShader(Color::Red()));
  canvas->drawRect(Rect::MakeXYWH(580, 20, 100, 100), paint);
  paint.setShader(Shader::MakeLinearGradient({720, 20}, {820, 20}, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(720, 20, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({70, 190}, 50, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(20, 140, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({190, 190}, 50, {Color::Green(), Color::Red()}));
  paint.setColorFilter(ColorFilter::Blend(Color::White(), BlendMode::Difference));
  canvas->drawRect(Rect::MakeXYWH(140, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::DropShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(260, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::InnerShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(400, 140, 100, 100), paint);
  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/AssignColorSpace"));
}

TGFX_TEST(SVGExportTest, DstAssignColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  image = image->makeScaled(400, 400);
  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(2048, 2048), 0, nullptr,
                                    ColorSpace::DisplayP3(), ColorSpace::SRGB());
  auto canvas = exporter->getCanvas();
  Paint paint;
  paint.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(20, 20, 100, 100), paint);
  canvas->drawRRect(RRect::MakeOval(Rect::MakeXYWH(140, 20, 100, 100)), paint);
  canvas->drawImageRect(image, Rect::MakeXYWH(260, 20, 100, 100), {}, &paint);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 50.f);
  canvas->drawSimpleText("TGFX", 400, 80, font, paint);
  paint.setShader(Shader::MakeColorShader(Color::Red()));
  canvas->drawRect(Rect::MakeXYWH(580, 20, 100, 100), paint);
  paint.setShader(Shader::MakeLinearGradient({720, 20}, {820, 20}, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(720, 20, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({70, 190}, 50, {Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(20, 140, 100, 100), paint);
  paint.setShader(Shader::MakeRadialGradient({190, 190}, 50, {Color::Green(), Color::Red()}));
  paint.setColorFilter(ColorFilter::Blend(Color::White(), BlendMode::Difference));
  canvas->drawRect(Rect::MakeXYWH(140, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::DropShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(260, 140, 100, 100), paint);
  paint.setImageFilter(ImageFilter::InnerShadow(10, 10, 10, 10, Color::Green()));
  canvas->drawRect(Rect::MakeXYWH(400, 140, 100, 100), paint);
  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/DstAssignColorSpace"));
}

TGFX_TEST(SVGExportTest, LayerMaskBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Original SVG content bounds: x[-7.18, 23], y[-12.13, 23]. Width ~30, height ~35.
  // Scale 8x → 240x280. Surface 340x380 gives ~50px margin on each side.
  int width = 340;
  int height = 380;

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(width, height));
  auto canvas = exporter->getCanvas();

  auto displayList = std::make_unique<DisplayList>();

  // Parse the star path from the reference SVG (Mask group.svg).
  auto starPathPtr = SVGPathParser::FromSVGString(
      "M-1.7841 -11.6211C-1.5051 -12.1263 -0.7648 -12.1263 -0.6313 -11.6211"
      "L-0.1258 -9.7081C0.0372 -9.0911 0.4725 -8.6028 1.0762 -8.3595"
      "L2.9484 -7.605C3.4427 -7.4058 3.3396 -6.6908 2.7878 -6.4916"
      "L0.6983 -5.7371C0.0245 -5.4938 -0.5515 -5.0055 -0.8924 -4.3885"
      "L-1.9493 -2.4755C-2.2283 -1.9703 -2.9686 -1.9703 -3.1021 -2.4755"
      "L-3.6077 -4.3885C-3.7707 -5.0055 -4.206 -5.4938 -4.8097 -5.7371"
      "L-6.6817 -6.4916C-7.1761 -6.6908 -7.0731 -7.4058 -6.5213 -7.605"
      "L-4.4318 -8.3595C-3.7579 -8.6028 -3.1819 -9.0911 -2.8411 -9.7081"
      "L-1.7841 -11.6211Z"
      "M9.7681 -7.4933C9.6346 -7.9985 8.8944 -7.9985 8.6152 -7.4933"
      "L5.9887 -2.7391C5.1367 -1.1969 3.6967 0.024 2.0121 0.6323"
      "L-3.1806 2.5072C-3.7324 2.7064 -3.8354 3.4213 -3.341 3.6207"
      "L1.3112 5.4956C2.8205 6.1039 3.9087 7.3248 4.3163 8.8671"
      "L5.5726 13.6211C5.7061 14.1263 6.4464 14.1263 6.7255 13.6211"
      "L9.3519 8.8671C10.204 7.3248 11.6441 6.1039 13.3286 5.4956"
      "L18.5212 3.6207C19.0731 3.4213 19.1761 2.7064 18.6818 2.5072"
      "L14.0294 0.6323C12.5202 0.024 11.432 -1.1969 11.0244 -2.7391"
      "L9.7681 -7.4933Z");
  ASSERT_TRUE(starPathPtr != nullptr);
  auto starPath = *starPathPtr;
  starPath.setFillType(PathFillType::EvenOdd);

  // Mask layer: a ShapeLayer with the star path (in original SVG coordinates).
  auto maskLayer = ShapeLayer::Make();
  maskLayer->setPath(starPath);
  maskLayer->setFillStyle(ShapeStyle::Make(Color::Black()));

  // Group layer that will be masked.
  auto groupLayer = Layer::Make();
  groupLayer->setMask(maskLayer);

  // Circle 1: cyan (#07D6DD), cx=11.5 cy=11.5 r=11.5 in original SVG coordinates.
  auto circle1 = ShapeLayer::Make();
  Path circlePath1;
  circlePath1.addOval(Rect::MakeXYWH(0, 0, 23, 23));
  circle1->setPath(circlePath1);
  circle1->setFillStyle(ShapeStyle::Make(Color{0.027f, 0.839f, 0.867f, 1.0f}));
  circle1->setFilters({BlurFilter::Make(3, 3)});
  groupLayer->addChild(circle1);

  // Circle 2: blue (#24A2F7) with 80% opacity, translate(2,5) cx=9 cy=9 r=9.
  auto circle2 = ShapeLayer::Make();
  Path circlePath2;
  circlePath2.addOval(Rect::MakeXYWH(2, 5, 18, 18));
  circle2->setPath(circlePath2);
  circle2->setFillStyle(ShapeStyle::Make(Color{0.141f, 0.635f, 0.969f, 1.0f}));
  circle2->setAlpha(0.8f);
  circle2->setFilters({BlurFilter::Make(3, 3)});
  groupLayer->addChild(circle2);

  // Apply uniform transform: scale 8x, then center in the surface.
  // Content center ~(8, 5.5). Offset = surface_center - content_center * scale.
  auto layerMatrix = Matrix::MakeScale(8);
  layerMatrix.postTranslate(static_cast<float>(width) / 2 - 8 * 8,
                            static_cast<float>(height) / 2 - 5.5f * 8);
  groupLayer->setMatrix(layerMatrix);
  maskLayer->setMatrix(layerMatrix);

  displayList->root()->addChild(groupLayer);
  displayList->root()->draw(canvas);

  exporter->close();
  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/LayerMaskBlur"));
}

TGFX_TEST(SVGExportTest, ClipWithMatrixTransform) {
  // Bug: applyClip received content bounds in local coordinates while the clip path was in
  // parent coordinates. When a matrix scaled or translated the content, the coordinate mismatch
  // caused contains() to return a wrong result, either skipping a needed clip or applying an
  // unnecessary one.

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200));
  auto canvas = exporter->getCanvas();

  Paint backgroundPaint;
  backgroundPaint.setColor(Color::White());
  canvas->drawRect(Rect::MakeWH(200, 200), backgroundPaint);

  Paint paint;
  paint.setColor(Color::Red());

  // A small rect (20x20) at origin that is fully inside the clip in local coordinates,
  // but after a scale(10,10) transform it becomes 200x200 and overflows the clip.
  canvas->save();
  canvas->clipRect(Rect::MakeXYWH(80, 80, 40, 40));
  canvas->translate(90, 90);
  canvas->drawRect(Rect::MakeXYWH(0, 0, 20, 20), paint);
  canvas->restore();

  exporter->close();

  EXPECT_TRUE(CompareSVG(SVGStream, "SVGExportTest/ClipWithMatrixTransform"));
}

/**
 * Complex RRect (per-corner different radii) cannot be represented by SVG <rect> because
 * <rect> only supports uniform rx/ry. Verify the exporter falls back to <path>.
 */
TGFX_TEST(SVGExportTest, ComplexRRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  tgfx::Paint paint;
  paint.setColor(Color::Blue());
  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto canvas = exporter->getCanvas();

  auto complexRRect = RRect::MakeRectRadii(Rect::MakeXYWH(20, 20, 160, 160),
                                           {{{30, 30}, {10, 10}, {20, 20}, {5, 5}}});
  ASSERT_EQ(complexRRect.type(), RRect::Type::Complex);
  canvas->drawRRect(complexRRect, paint);

  exporter->close();
  auto SVGString = SVGStream->readString();
  // Complex RRect must be exported as <path>, not <rect>.
  ASSERT_NE(SVGString.find("<path"), std::string::npos);
  ASSERT_EQ(SVGString.find("<rect"), std::string::npos);
}
}  // namespace tgfx
