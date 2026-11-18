/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <vector>
#include "layers/DrawArgs.h"
#include "layers/LayerDrawSession.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Mesh.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlobBuilder.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/LayerImageReader.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

static constexpr int TestWidth = 160;
static constexpr int TestHeight = 120;

class FailingCancelLayer final : public Layer {
 public:
  LayerImageReader* reader = nullptr;
  int drawCount = 0;

 protected:
  bool drawLayer(const DrawArgs& args, Canvas*, float, BlendMode) override {
    drawCount++;
    args.drawControl->fail(DrawFailureCode::RenderingFailure, DrawStage::Rendering, this, {},
                           "test drawing failure before cancellation");
    reader->requestCancel();
    return true;
  }
};

class FailingFilter final : public LayerFilter {
 protected:
  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image>, float, const Rect&,
                                       Point*) override {
    return nullptr;
  }
};

class InterruptSecondTileLayer final : public Layer {
 public:
  LayerImageReader* reader = nullptr;
  bool cancel = false;
  int drawCount = 0;

 protected:
  bool drawLayer(const DrawArgs& args, Canvas*, float, BlendMode) override {
    if (++drawCount == 2) {
      if (cancel) {
        reader->requestCancel();
      } else {
        args.drawControl->fail(DrawFailureCode::RenderingFailure, DrawStage::Rendering, this, {},
                               "test failure on the second tile");
      }
    }
    return true;
  }
};

struct PixelImage {
  std::vector<uint8_t> pixels = {};
  int width = 0;
  int height = 0;
  size_t rowBytes = 0;
};

static std::shared_ptr<Layer> BuildNestedBackdropScene() {
  auto root = Layer::Make();
  auto background = SolidLayer::Make();
  background->setColor(Color::FromRGBA(20, 80, 60, 255));
  background->setWidth(TestWidth);
  background->setHeight(TestHeight);
  root->addChild(background);

  auto stripe = SolidLayer::Make();
  stripe->setColor(Color::FromRGBA(240, 120, 40, 255));
  stripe->setWidth(70);
  stripe->setHeight(40);
  stripe->setMatrix(Matrix::MakeTrans(20, 30));
  root->addChild(stripe);

  auto group = Layer::Make();
  group->setMatrix(Matrix::MakeTrans(12, 10));
  group->setFilters({BlurFilter::Make(0.5f, 0.5f)});
  auto glass = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(78, 52));
  glass->setPath(path);
  glass->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 80)));
  glass->setMatrix(Matrix::MakeTrans(26, 24));
  glass->setLayerStyles({BackgroundBlurStyle::Make(4.0f, 4.0f)});
  group->addChild(glass);
  root->addChild(group);
  return root;
}

static std::shared_ptr<Layer> BuildTestScene(bool withBackgroundBlur) {
  auto root = Layer::Make();
  auto background = SolidLayer::Make();
  background->setColor(Color::FromRGBA(30, 60, 90, 255));
  background->setWidth(TestWidth);
  background->setHeight(TestHeight);
  root->addChild(background);

  auto stripe = SolidLayer::Make();
  stripe->setColor(Color::Red());
  stripe->setWidth(24);
  stripe->setHeight(30);
  stripe->setMatrix(Matrix::MakeTrans(18, 22));
  root->addChild(stripe);

  auto shape = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(50, 36));
  shape->setPath(path);
  shape->setFillStyle(ShapeStyle::Make(Color::FromRGBA(220, 200, 20, 255)));
  shape->setMatrix(Matrix::MakeTrans(80.5f, 60.25f));
  shape->setFilters({BlurFilter::Make(1.5f, 1.5f)});
  root->addChild(shape);

  if (withBackgroundBlur) {
    auto glass = ShapeLayer::Make();
    Path glassPath = {};
    glassPath.addRect(Rect::MakeWH(60, 40));
    glass->setPath(glassPath);
    glass->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 90)));
    glass->setMatrix(Matrix::MakeTrans(50, 40));
    glass->setLayerStyles({BackgroundBlurStyle::Make(3.0f, 3.0f)});
    root->addChild(glass);
  }
  return root;
}

static ImageInfo OutputInfo(int width, int height) {
  return ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied, 0,
                         ColorSpace::SRGB());
}

static PixelImage RenderReference(Context* context, DisplayList* displayList) {
  PixelImage image = {};
  auto surface =
      Surface::Make(context, TestWidth, TestHeight, false, 1, false, 0, ColorSpace::SRGB());
  if (surface == nullptr) {
    return image;
  }
  displayList->render(surface.get());
  auto info = OutputInfo(TestWidth, TestHeight);
  image.pixels.resize(info.byteSize());
  if (!surface->readPixels(info, image.pixels.data())) {
    image.pixels.clear();
    return image;
  }
  image.width = TestWidth;
  image.height = TestHeight;
  image.rowBytes = info.rowBytes();
  return image;
}

static std::unique_ptr<DisplayList> MakeDisplayList(const std::shared_ptr<Layer>& content) {
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(content);
  return displayList;
}

struct ReadResult {
  PixelImage image = {};
  LayerImageError error = {};
  bool bandSequenceValid = true;
  int bandsRead = 0;
};

static ReadResult ReadTiled(Context* context, const std::shared_ptr<Layer>& target,
                            const std::shared_ptr<Layer>& sceneRoot, const ISize& tileSizeLimit) {
  ReadResult output = {};
  LayerImageRequest request = {};
  request.target = target;
  request.sceneRoot = sceneRoot;
  request.outputSize = {TestWidth, TestHeight};
  request.targetLocalToOutput = Matrix::I();
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  request.colorType = ColorType::RGBA_8888;
  request.alphaType = AlphaType::Premultiplied;
  request.clearColor = Color::Transparent();

  LayerImageReadOptions options = {};
  options.bandRows = 17;
  options.tileSizeLimit = tileSizeLimit;
  auto reader = LayerImageReader::Make(context, request, options, &output.error);
  if (reader == nullptr) {
    return output;
  }
  auto fullInfo = OutputInfo(TestWidth, TestHeight);
  output.image.pixels.assign(fullInfo.byteSize(), 0);
  output.image.width = TestWidth;
  output.image.height = TestHeight;
  output.image.rowBytes = fullInfo.rowBytes();
  int nextRow = 0;
  for (;;) {
    int remaining = TestHeight - nextRow;
    if (remaining <= 0) {
      break;
    }
    auto rows = std::min(reader->bandInfo().height(), remaining);
    auto bandInfo = ImageInfo::Make(TestWidth, rows, reader->bandInfo().colorType(),
                                    reader->bandInfo().alphaType(), fullInfo.rowBytes(),
                                    reader->bandInfo().colorSpace());
    auto* bandPixels =
        output.image.pixels.data() + static_cast<size_t>(nextRow) * fullInfo.rowBytes();
    Pixmap pixmap(bandInfo, bandPixels);
    auto result = reader->readNextBand(context, pixmap, bandInfo.byteSize());
    if (result.code == LayerImageReadCode::Failed) {
      output.error = result.error;
      output.image.pixels.clear();
      return output;
    }
    if (result.code == LayerImageReadCode::Cancelled) {
      output.image.pixels.clear();
      return output;
    }
    if (result.code == LayerImageReadCode::Done) {
      break;
    }
    if (result.firstRow != nextRow || result.rowCount != rows) {
      output.bandSequenceValid = false;
    }
    nextRow += result.rowCount;
    output.bandsRead++;
    if (result.lastBand) {
      break;
    }
  }
  if (nextRow != TestHeight || !reader->isDone() || reader->rowsRead() != TestHeight) {
    output.bandSequenceValid = false;
  }
  return output;
}

static int CountDifferentPixels(const PixelImage& a, const PixelImage& b, int tolerance) {
  if (a.pixels.empty() || b.pixels.empty() || a.width != b.width || a.height != b.height) {
    return a.width * a.height;
  }
  int different = 0;
  for (int y = 0; y < a.height; ++y) {
    auto* rowA = a.pixels.data() + static_cast<size_t>(y) * a.rowBytes;
    auto* rowB = b.pixels.data() + static_cast<size_t>(y) * b.rowBytes;
    for (int x = 0; x < a.width; ++x) {
      for (size_t channel = 0; channel < 4; ++channel) {
        auto offset = static_cast<size_t>(x) * 4 + channel;
        auto valueA = static_cast<int>(rowA[offset]);
        auto valueB = static_cast<int>(rowB[offset]);
        if (std::abs(valueA - valueB) > tolerance) {
          different++;
          break;
        }
      }
    }
  }
  return different;
}

static float MaxOutset(const Rect& rect) {
  return std::max(std::max(-rect.left, -rect.top), std::max(rect.right, rect.bottom));
}

static std::shared_ptr<Layer> BuildNestedFilteredBackdropScene(Layer** group) {
  auto root = Layer::Make();
  auto filteredGroup = Layer::Make();
  filteredGroup->setFilters({BlurFilter::Make(4.0f, 4.0f)});
  auto glass = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(30, 30));
  glass->setPath(path);
  glass->setFillStyle(ShapeStyle::Make(Color::White()));
  glass->setLayerStyles({BackgroundBlurStyle::Make(4.0f, 4.0f)});
  filteredGroup->addChild(glass);
  root->addChild(filteredGroup);
  *group = filteredGroup.get();
  return root;
}

}  // namespace

TGFX_TEST(LayerImageReaderTest, TiledMatchesSingleSurface) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  for (bool withBackgroundBlur : {false, true}) {
    auto content = BuildTestScene(withBackgroundBlur);
    auto displayList = MakeDisplayList(content);
    auto reference = RenderReference(context, displayList.get());
    ASSERT_FALSE(reference.pixels.empty());
    auto tiled = ReadTiled(context, content, displayList->root()->shared_from_this(), {64, 64});
    ASSERT_FALSE(tiled.image.pixels.empty())
        << "code=" << static_cast<int>(tiled.error.code)
        << " stage=" << static_cast<int>(tiled.error.stage) << " message=" << tiled.error.message;
    EXPECT_TRUE(tiled.bandSequenceValid);
    EXPECT_GT(tiled.bandsRead, 1);
    EXPECT_EQ(CountDifferentPixels(reference, tiled.image, 0), 0);
  }
}

TGFX_TEST(LayerImageReaderTest, CanvasDefaultKeepsLargePictureCoordinates) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = SolidLayer::Make();
  content->setColor(Color::Red());
  content->setWidth(10);
  content->setHeight(10);
  constexpr float LargeScale = 1073741824.0f;
  PictureRecorder recorder = {};
  auto recording = recorder.beginRecording();
  recording->scale(LargeScale, LargeScale);
  content->draw(recording);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);
  EXPECT_EQ(picture->getBounds(), Rect::MakeWH(10.0f * LargeScale, 10.0f * LargeScale));
  auto surface = Surface::Make(context, 10, 10, false, 1, false, 0, ColorSpace::SRGB());
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->scale(1.0f / LargeScale, 1.0f / LargeScale);
  canvas->drawPicture(picture);
  auto info = OutputInfo(10, 10);
  std::vector<uint8_t> pixels(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, pixels.data()));
  auto offset = static_cast<size_t>(5) * info.rowBytes() + 5 * 4;
  EXPECT_EQ(pixels[offset], 255);
  EXPECT_EQ(pixels[offset + 1], 0);
  EXPECT_EQ(pixels[offset + 2], 0);
  EXPECT_EQ(pixels[offset + 3], 255);
}

TGFX_TEST(LayerImageReaderTest, CanvasDefaultPlanningUsesFloatingPointState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(10, 10));
  content->setPath(path);
  content->setFillStyle(ShapeStyle::Make(Color::White()));
  content->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
  for (auto scale : {2.0f, 1073741824.0f}) {
    LayerDrawRequest request = {};
    request.target = content.get();
    request.captureRoot = content.get();
    request.context = context;
    request.canvasMatrix = Matrix::MakeScale(scale);
    request.canvasDrawBounds = Rect::MakeWH(10, 10);
    auto session = LayerDrawSession::Prepare(context, request, nullptr);
    ASSERT_TRUE(session != nullptr);
    auto result = session->planRegion(PixelRect(), LayerDrawLimits(), nullptr);
    ASSERT_EQ(result.code, RegionPlanCode::Ready);
    EXPECT_TRUE(result.region.coreRect.isEmpty());
    EXPECT_TRUE(result.region.renderRect.isEmpty());
    ASSERT_TRUE(result.region.dependencies != nullptr);
    EXPECT_TRUE(result.region.dependencies->needsBackground);
    EXPECT_TRUE(result.region.dependencies->captureRect.isEmpty());
  }
}

TGFX_TEST(LayerImageReaderTest, CanvasDrawPreservesAlphaModulationAboveOne) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = SolidLayer::Make();
  auto color = Color::Red();
  color.alpha = 0.25f;
  content->setColor(color);
  content->setWidth(8);
  content->setHeight(8);
  auto surface = Surface::Make(context, 8, 8, false, 1, false, 0, ColorSpace::SRGB());
  ASSERT_TRUE(surface != nullptr);
  content->draw(surface->getCanvas(), 2.0f);
  auto info = OutputInfo(8, 8);
  std::vector<uint8_t> pixels(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, pixels.data()));
  auto offset = static_cast<size_t>(4) * info.rowBytes() + 4 * 4;
  EXPECT_NEAR(pixels[offset], 128, 1);
  EXPECT_EQ(pixels[offset + 1], 0);
  EXPECT_EQ(pixels[offset + 2], 0);
  EXPECT_NEAR(pixels[offset + 3], 128, 1);

  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = content;
  request.outputSize = {8, 8};
  request.alpha = 2.0f;
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  LayerImageError error = {};
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);
  EXPECT_EQ(error.stage, LayerImageStage::Validation);
}

TGFX_TEST(LayerImageReaderTest, CanvasDrawMatchesDisplayList) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  for (bool withBackgroundBlur : {false, true}) {
    auto content = BuildTestScene(withBackgroundBlur);
    auto displayList = MakeDisplayList(content);
    auto reference = RenderReference(context, displayList.get());
    ASSERT_FALSE(reference.pixels.empty());

    auto surface =
        Surface::Make(context, TestWidth, TestHeight, false, 1, false, 0, ColorSpace::SRGB());
    ASSERT_TRUE(surface != nullptr);
    auto* canvas = surface->getCanvas();
    canvas->clear();
    content->draw(canvas);

    auto info = OutputInfo(TestWidth, TestHeight);
    PixelImage drawn = {};
    drawn.pixels.resize(info.byteSize());
    ASSERT_TRUE(surface->readPixels(info, drawn.pixels.data()));
    drawn.width = TestWidth;
    drawn.height = TestHeight;
    drawn.rowBytes = info.rowBytes();
    EXPECT_EQ(CountDifferentPixels(reference, drawn, 0), 0);
  }
}

TGFX_TEST(LayerImageReaderTest, MaskedTargetWithAttributesMatchesSingleSurface) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(true);
  content->setAlpha(0.7f);
  content->setBlendMode(BlendMode::Multiply);
  content->setMatrix(Matrix::MakeTrans(6, 4));
  auto displayList = MakeDisplayList(content);
  auto backdrop = Color::FromRGBA(128, 128, 128, 255);
  displayList->setBackgroundColor(backdrop);
  auto mask = ShapeLayer::Make();
  Path maskPath = {};
  maskPath.addRect(Rect::MakeXYWH(4, 6, 130, 96));
  mask->setPath(maskPath);
  mask->setFillStyle(ShapeStyle::Make(Color::White()));
  displayList->root()->addChild(mask);
  content->setMask(mask);

  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());

  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = displayList->root()->shared_from_this();
  request.outputSize = {TestWidth, TestHeight};
  request.targetLocalToOutput = Matrix::MakeTrans(6, 4);
  request.alpha = 0.7f;
  request.blendMode = BlendMode::Multiply;
  request.clearColor = backdrop;
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  LayerImageReadOptions options = {};
  options.bandRows = 13;
  options.tileSizeLimit = {48, 40};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  PixelImage output = {};
  output.pixels.assign(reference.pixels.size(), 0);
  output.width = TestWidth;
  output.height = TestHeight;
  output.rowBytes = reference.rowBytes;
  int nextRow = 0;
  while (nextRow < TestHeight) {
    auto rows = std::min(reader->bandInfo().height(), TestHeight - nextRow);
    auto bandInfo = ImageInfo::Make(TestWidth, rows, reader->bandInfo().colorType(),
                                    reader->bandInfo().alphaType(), reference.rowBytes,
                                    reader->bandInfo().colorSpace());
    auto* bandPixels = output.pixels.data() + static_cast<size_t>(nextRow) * reference.rowBytes;
    auto result = reader->readNextBand(context, Pixmap(bandInfo, bandPixels), bandInfo.byteSize());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady)
        << "firstRow=" << result.firstRow << " error=" << static_cast<int>(result.error.code)
        << " stage=" << static_cast<int>(result.error.stage) << " message=" << result.error.message;
    nextRow += result.rowCount;
  }
  EXPECT_TRUE(reader->isDone());
  // The translated red stripe multiplies the gray backdrop through the opaque part of the mask.
  const auto* inside = output.pixels.data() + 30 * output.rowBytes + 45 * 4;
  EXPECT_NEAR(inside[0], 128, 1);
  EXPECT_LT(inside[1], 128);
  EXPECT_GT(inside[1], 0);
  EXPECT_EQ(inside[1], inside[2]);
  const auto* outside = output.pixels.data() + 30 * output.rowBytes + 145 * 4;
  EXPECT_EQ(outside[0], 128);
  EXPECT_EQ(outside[1], 128);
  EXPECT_EQ(outside[2], 128);
  EXPECT_EQ(outside[3], 255);
  EXPECT_EQ(CountDifferentPixels(reference, output, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, NestedBackdropMatchesAndIsTileInvariant) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildNestedBackdropScene();
  auto displayList = MakeDisplayList(content);
  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());
  auto sceneRoot = displayList->root()->shared_from_this();

  auto smallTiles = ReadTiled(context, content, sceneRoot, {64, 64});
  ASSERT_FALSE(smallTiles.image.pixels.empty())
      << "code=" << static_cast<int>(smallTiles.error.code)
      << " stage=" << static_cast<int>(smallTiles.error.stage)
      << " message=" << smallTiles.error.message;
  EXPECT_TRUE(smallTiles.bandSequenceValid);
  // A layer filter applied to the enclosing group rasterizes its intermediate image at the tile's
  // clip window, so its fractional output offset lands on a tile-dependent sub-pixel phase. That
  // may move individual edge channels by one 8-bit step, but must never drop or duplicate content.
  EXPECT_EQ(CountDifferentPixels(reference, smallTiles.image, 1), 0);
  EXPECT_LE(CountDifferentPixels(reference, smallTiles.image, 0), TestWidth * TestHeight / 200);

  auto largeTiles = ReadTiled(context, content, sceneRoot, {4096, 4096});
  ASSERT_FALSE(largeTiles.image.pixels.empty());
  EXPECT_EQ(CountDifferentPixels(reference, largeTiles.image, 0), 0);
  EXPECT_EQ(CountDifferentPixels(smallTiles.image, largeTiles.image, 1), 0);
}

TGFX_TEST(LayerImageReaderTest, ScaledOutputMatchesCanvasDraw) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(true);
  auto displayList = MakeDisplayList(content);

  constexpr int scaledWidth = 80;
  constexpr int scaledHeight = 60;
  constexpr float scale = 0.5f;

  auto referenceSurface =
      Surface::Make(context, scaledWidth, scaledHeight, false, 1, false, 0, ColorSpace::SRGB());
  ASSERT_TRUE(referenceSurface != nullptr);
  auto* referenceCanvas = referenceSurface->getCanvas();
  referenceCanvas->clear();
  referenceCanvas->scale(scale, scale);
  content->draw(referenceCanvas);
  auto referenceInfo = OutputInfo(scaledWidth, scaledHeight);
  PixelImage reference = {};
  reference.pixels.resize(referenceInfo.byteSize());
  ASSERT_TRUE(referenceSurface->readPixels(referenceInfo, reference.pixels.data()));
  reference.width = scaledWidth;
  reference.height = scaledHeight;
  reference.rowBytes = referenceInfo.rowBytes();

  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = displayList->root()->shared_from_this();
  request.outputSize = {scaledWidth, scaledHeight};
  request.targetLocalToOutput = Matrix::MakeScale(scale);
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  LayerImageReadOptions options = {};
  options.bandRows = 9;
  options.tileSizeLimit = {24, 20};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  PixelImage output = {};
  output.pixels.assign(referenceInfo.byteSize(), 0);
  output.width = scaledWidth;
  output.height = scaledHeight;
  output.rowBytes = referenceInfo.rowBytes();
  int nextRow = 0;
  while (nextRow < scaledHeight) {
    auto rows = std::min(reader->bandInfo().height(), scaledHeight - nextRow);
    auto bandInfo = ImageInfo::Make(scaledWidth, rows, reader->bandInfo().colorType(),
                                    reader->bandInfo().alphaType(), referenceInfo.rowBytes(),
                                    reader->bandInfo().colorSpace());
    auto* bandPixels =
        output.pixels.data() + static_cast<size_t>(nextRow) * referenceInfo.rowBytes();
    auto result = reader->readNextBand(context, Pixmap(bandInfo, bandPixels), bandInfo.byteSize());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
    nextRow += result.rowCount;
  }
  EXPECT_TRUE(reader->isDone());
  EXPECT_EQ(CountDifferentPixels(reference, output, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, PictureCanvasMatchesDisplayList) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(false);
  auto displayList = MakeDisplayList(content);
  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());

  PictureRecorder recorder = {};
  auto* recordingCanvas = recorder.beginRecording();
  content->draw(recordingCanvas);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);

  auto surface =
      Surface::Make(context, TestWidth, TestHeight, false, 1, false, 0, ColorSpace::SRGB());
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawPicture(picture);

  auto info = OutputInfo(TestWidth, TestHeight);
  PixelImage drawn = {};
  drawn.pixels.resize(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, drawn.pixels.data()));
  drawn.width = TestWidth;
  drawn.height = TestHeight;
  drawn.rowBytes = info.rowBytes();
  EXPECT_EQ(CountDifferentPixels(reference, drawn, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, PaddedStrideLeavesPaddingUntouched) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(true);
  auto displayList = MakeDisplayList(content);
  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());

  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = displayList->root()->shared_from_this();
  request.outputSize = {TestWidth, TestHeight};
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  LayerImageReadOptions options = {};
  options.bandRows = 13;
  options.tileSizeLimit = {48, 40};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  auto padding = static_cast<size_t>(16);
  auto rowBytes = info.rowBytes() + padding;
  std::vector<uint8_t> pixels(static_cast<size_t>(TestHeight) * rowBytes, 0xcc);
  auto paddedInfo = ImageInfo::Make(TestWidth, info.height(), info.colorType(), info.alphaType(),
                                    rowBytes, info.colorSpace());
  int nextRow = 0;
  while (nextRow < TestHeight) {
    auto rows = std::min(info.height(), TestHeight - nextRow);
    auto bandInfo = ImageInfo::Make(TestWidth, rows, info.colorType(), info.alphaType(), rowBytes,
                                    info.colorSpace());
    auto* bandPixels = pixels.data() + static_cast<size_t>(nextRow) * rowBytes;
    auto result = reader->readNextBand(context, Pixmap(bandInfo, bandPixels), bandInfo.byteSize());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
    nextRow += result.rowCount;
  }
  EXPECT_TRUE(reader->isDone());
  for (int y = 0; y < TestHeight; ++y) {
    auto* row = pixels.data() + static_cast<size_t>(y) * rowBytes;
    for (size_t i = 0; i < padding; ++i) {
      EXPECT_EQ(row[info.rowBytes() + i], 0xcc);
    }
  }
  PixelImage padded = {};
  padded.pixels.assign(pixels.size(), 0);
  padded.width = TestWidth;
  padded.height = TestHeight;
  padded.rowBytes = rowBytes;
  for (int y = 0; y < TestHeight; ++y) {
    std::copy_n(pixels.data() + static_cast<size_t>(y) * rowBytes,
                static_cast<size_t>(TestWidth) * 4,
                padded.pixels.data() + static_cast<size_t>(y) * rowBytes);
  }
  EXPECT_EQ(CountDifferentPixels(reference, padded, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, BgraOutputSwapsChannels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(false);
  auto displayList = MakeDisplayList(content);
  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());

  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = displayList->root()->shared_from_this();
  request.outputSize = {TestWidth, TestHeight};
  request.colorType = ColorType::BGRA_8888;
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  LayerImageReadOptions options = {};
  options.bandRows = 64;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  ASSERT_EQ(reader->bandInfo().colorType(), ColorType::BGRA_8888);
  std::vector<uint8_t> pixels(reader->bandInfo().byteSize() * 2);
  std::vector<uint8_t> fullOutput(static_cast<size_t>(TestWidth) * TestHeight * 4);
  int nextRow = 0;
  while (nextRow < TestHeight) {
    auto rows = std::min(reader->bandInfo().height(), TestHeight - nextRow);
    auto info = ImageInfo::Make(TestWidth, rows, ColorType::BGRA_8888, AlphaType::Premultiplied,
                                TestWidth * 4, ColorSpace::SRGB());
    auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
    std::copy_n(pixels.data(), info.byteSize(),
                fullOutput.data() + static_cast<size_t>(nextRow) * TestWidth * 4);
    nextRow += result.rowCount;
  }
  int different = 0;
  for (size_t i = 0; i + 3 < reference.pixels.size(); i += 4) {
    if (fullOutput[i] != reference.pixels[i + 2] || fullOutput[i + 1] != reference.pixels[i + 1] ||
        fullOutput[i + 2] != reference.pixels[i] || fullOutput[i + 3] != reference.pixels[i + 3]) {
      different++;
    }
  }
  EXPECT_EQ(different, 0);
}

TGFX_TEST(LayerImageReaderTest, EquivalentColorSpacesAcceptIndependentInstances) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto customP3 = ColorSpace::MakeRGB(NamedTransferFunction::SRGB, NamedGamut::DisplayP3);
  ASSERT_TRUE(customP3 != nullptr);
  for (const auto& outputColorSpace : {ColorSpace::DisplayP3(), customP3}) {
    auto destinationColorSpace =
        ColorSpace::MakeRGB(NamedTransferFunction::SRGB, NamedGamut::DisplayP3);
    ASSERT_TRUE(destinationColorSpace != nullptr);
    ASSERT_NE(outputColorSpace.get(), destinationColorSpace.get());
    ASSERT_TRUE(ColorSpace::Equals(outputColorSpace.get(), destinationColorSpace.get()));
    LayerImageRequest request = {};
    request.target = root;
    request.sceneRoot = root;
    request.outputSize = {8, 4};
    request.outputColorSpace = outputColorSpace;
    request.clearColor = Color::Red();
    LayerImageReadOptions options = {};
    options.bandRows = 4;
    auto reader = LayerImageReader::Make(context, request, options);
    ASSERT_TRUE(reader != nullptr);
    auto info = reader->bandInfo().makeColorSpace(destinationColorSpace);
    std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
    auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
    EXPECT_EQ(result.firstRow, 0);
    EXPECT_EQ(result.rowCount, 4);
    EXPECT_TRUE(result.lastBand);
    EXPECT_EQ(reader->rowsRead(), 4);
    EXPECT_TRUE(reader->isDone());
    // The sRGB red primary encoded in Display P3 is approximately (0.9175, 0.2003, 0.1386).
    for (size_t i = 0; i < pixels.size(); i += 4) {
      EXPECT_NEAR(pixels[i], 234, 1);
      EXPECT_NEAR(pixels[i + 1], 51, 1);
      EXPECT_NEAR(pixels[i + 2], 35, 1);
      EXPECT_EQ(pixels[i + 3], 255);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, ReadsUnpremultipliedColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  LayerImageRequest request = {};
  request.target = Layer::Make();
  request.sceneRoot = request.target;
  request.outputSize = {1, 1};
  request.alphaType = AlphaType::Unpremultiplied;
  request.clearColor = Color::FromRGBA(200, 120, 40, 128);
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  uint8_t pixel[4] = {};
  auto result = reader->readNextBand(context, Pixmap(reader->bandInfo(), pixel), sizeof(pixel));
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << result.error.message;
  EXPECT_NEAR(pixel[0], 200, 1);
  EXPECT_NEAR(pixel[1], 120, 1);
  EXPECT_NEAR(pixel[2], 40, 1);
  EXPECT_EQ(pixel[3], 128);
}

#if defined(TGFX_USE_THREADS) && !defined(TGFX_BUILD_FOR_WEB)
TGFX_TEST(LayerImageReaderTest, RejectsAContextFromAnotherDevice) {
  auto originalDevice = DevicePool::Make();
  ASSERT_TRUE(originalDevice != nullptr);
  // Native device pools cache by thread. Reader operations still run on the scene thread.
  auto otherDevice = std::async(std::launch::async, DevicePool::Make).get();
  ASSERT_TRUE(otherDevice != nullptr);
  if (originalDevice->uniqueID() == otherDevice->uniqueID()) {
    GTEST_SKIP() << "The backend returned the same device";
  }
  LayerImageRequest request = {};
  request.target = Layer::Make();
  request.sceneRoot = request.target;
  request.outputSize = {1, 1};
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  std::unique_ptr<LayerImageReader> reader;
  ImageInfo info = {};
  {
    ContextScope scope;
    ASSERT_TRUE(scope.getContext() != nullptr);
    reader = LayerImageReader::Make(scope.getContext(), request, options);
    ASSERT_TRUE(reader != nullptr);
    info = reader->bandInfo();
  }
  auto otherContext = otherDevice->lockContext();
  ASSERT_TRUE(otherContext != nullptr);
  std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
  auto result = reader->readNextBand(otherContext, Pixmap(info, pixels.data()), pixels.size());
  otherDevice->unlock();
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::ContextMismatch);
  EXPECT_EQ(result.error.stage, LayerImageStage::Rendering);
  EXPECT_EQ(result.rowCount, 0);
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));
}
#endif

TGFX_TEST(LayerImageReaderTest, TileSizeDoesNotChangePixels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = BuildTestScene(true);
  auto displayList = MakeDisplayList(content);
  auto sceneRoot = displayList->root()->shared_from_this();
  auto smallTiles = ReadTiled(context, content, sceneRoot, {32, 24});
  ASSERT_FALSE(smallTiles.image.pixels.empty());
  auto largeTiles = ReadTiled(context, content, sceneRoot, {4096, 4096});
  ASSERT_FALSE(largeTiles.image.pixels.empty());
  EXPECT_TRUE(smallTiles.bandSequenceValid);
  EXPECT_TRUE(largeTiles.bandSequenceValid);
  EXPECT_EQ(CountDifferentPixels(smallTiles.image, largeTiles.image, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, EmptySceneDeliversClearColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  request.clearColor = Color::FromRGBA(200, 10, 20, 255);
  LayerImageReadOptions options = {};
  options.bandRows = 7;
  options.tileSizeLimit = {16, 16};
  LayerImageError error = {};
  auto reader = LayerImageReader::Make(context, request, options, &error);
  ASSERT_TRUE(reader != nullptr);
  ASSERT_EQ(error.code, LayerImageErrorCode::None);
  std::vector<uint8_t> pixels(static_cast<size_t>(40) * 30 * 4);
  int nextRow = 0;
  bool finished = false;
  while (!finished) {
    auto rows = std::min(reader->bandInfo().height(), 30 - nextRow);
    auto info =
        ImageInfo::Make(40, rows, reader->bandInfo().colorType(), reader->bandInfo().alphaType(),
                        40 * 4, reader->bandInfo().colorSpace());
    Pixmap pixmap(info, pixels.data() + static_cast<size_t>(nextRow) * 40 * 4);
    auto result = reader->readNextBand(context, pixmap, info.byteSize());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
    EXPECT_EQ(result.firstRow, nextRow);
    EXPECT_EQ(result.rowCount, rows);
    nextRow += result.rowCount;
    finished = result.lastBand;
  }
  EXPECT_TRUE(reader->isDone());
  auto tailInfo =
      ImageInfo::Make(40, 7, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, ColorSpace::SRGB());
  auto done = reader->readNextBand(context, Pixmap(tailInfo, pixels.data()), tailInfo.byteSize());
  EXPECT_EQ(done.code, LayerImageReadCode::Done);
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 40; ++x) {
      auto* pixel = pixels.data() + (static_cast<size_t>(y) * 40 + static_cast<size_t>(x)) * 4;
      EXPECT_EQ(pixel[0], 200);
      EXPECT_EQ(pixel[1], 10);
      EXPECT_EQ(pixel[2], 20);
      EXPECT_EQ(pixel[3], 255);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, RejectsInvalidOptions) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageError error = {};

  LayerImageReadOptions options = {};
  options.bandRows = 0;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  options.bandRows = 4;
  options.tileSizeLimit = {32, 0};
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  for (const auto& tileSizeLimit : {ISize{-1, -1}, ISize{-1, 0}, ISize{0, -1}}) {
    options.tileSizeLimit = tileSizeLimit;
    EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
    EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);
    EXPECT_EQ(error.stage, LayerImageStage::Validation);
  }

  options.tileSizeLimit = {0, 0};
  request.colorType = ColorType::ALPHA_8;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  request.colorType = ColorType::RGBA_8888;
  request.alphaType = AlphaType::Opaque;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  request.alphaType = AlphaType::Premultiplied;
  request.renderColorSpace = nullptr;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  request.renderColorSpace = ColorSpace::SRGB();
  EXPECT_TRUE(LayerImageReader::Make(nullptr, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::ContextUnavailable);
}

TGFX_TEST(LayerImageReaderTest, RejectsReadOnlyAndMismatchedDestination) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  const std::vector<uint8_t> untouched(static_cast<size_t>(40) * 8 * 4, 0x7f);
  auto pixels = untouched;

  auto readOnlyReader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(readOnlyReader != nullptr);
  auto info = readOnlyReader->bandInfo();
  const Pixmap readOnly(info, static_cast<const void*>(pixels.data()));
  auto result = readOnlyReader->readNextBand(context, readOnly, info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::InvalidInput);
  EXPECT_EQ(result.error.stage, LayerImageStage::Validation);
  EXPECT_EQ(result.firstRow, 0);
  EXPECT_EQ(result.rowCount, 0);
  EXPECT_EQ(readOnlyReader->rowsRead(), 0);
  EXPECT_EQ(pixels, untouched);

  auto narrowReader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(narrowReader != nullptr);
  pixels = untouched;
  auto narrowInfo =
      ImageInfo::Make(39, 8, info.colorType(), info.alphaType(), 39 * 4, info.colorSpace());
  Pixmap narrow(narrowInfo, pixels.data());
  result = narrowReader->readNextBand(context, narrow, narrowInfo.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::InvalidInput);
  EXPECT_EQ(result.error.stage, LayerImageStage::Validation);
  EXPECT_EQ(result.firstRow, 0);
  EXPECT_EQ(result.rowCount, 0);
  EXPECT_EQ(narrowReader->rowsRead(), 0);
  EXPECT_EQ(pixels, untouched);

  auto shortReader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(shortReader != nullptr);
  pixels = untouched;
  Pixmap shortBuffer(info, pixels.data());
  result = shortReader->readNextBand(context, shortBuffer, info.byteSize() - 1);
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::InvalidInput);
  EXPECT_EQ(result.error.stage, LayerImageStage::Validation);
  EXPECT_EQ(result.firstRow, 0);
  EXPECT_EQ(result.rowCount, 0);
  EXPECT_EQ(shortReader->rowsRead(), 0);
  EXPECT_EQ(pixels, untouched);
}

TGFX_TEST(LayerImageReaderTest, FailedReaderKeepsOriginalFailureAndPixels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
  const Pixmap readOnly(info, static_cast<const void*>(pixels.data()));
  const auto first = reader->readNextBand(context, readOnly, pixels.size());
  ASSERT_EQ(first.code, LayerImageReadCode::Failed);
  ASSERT_EQ(first.error.code, LayerImageErrorCode::InvalidInput);
  ASSERT_EQ(first.error.stage, LayerImageStage::Validation);
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));

  Context* readContexts[] = {nullptr, context};
  for (auto* readContext : readContexts) {
    pixels.assign(pixels.size(), 0x3f);
    auto result = reader->readNextBand(readContext, Pixmap(info, pixels.data()), pixels.size());
    EXPECT_EQ(result.code, LayerImageReadCode::Failed);
    EXPECT_EQ(result.firstRow, 0);
    EXPECT_EQ(result.rowCount, 0);
    EXPECT_FALSE(result.lastBand);
    EXPECT_EQ(result.error.code, first.error.code);
    EXPECT_EQ(result.error.stage, first.error.stage);
    EXPECT_EQ(result.error.firstRow, first.error.firstRow);
    EXPECT_EQ(result.error.surfaceSize, first.error.surfaceSize);
    EXPECT_EQ(result.error.message, first.error.message);
    EXPECT_EQ(reader->rowsRead(), 0);
    EXPECT_FALSE(reader->isDone());
    EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x3f));
  }
}

TGFX_TEST(LayerImageReaderTest, DrawingFailureTakesPriorityOverCancellation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = std::make_shared<FailingCancelLayer>();
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {8, 4};
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  root->reader = reader.get();
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(root->drawCount, 1);
  ASSERT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.firstRow, 0);
  EXPECT_EQ(result.rowCount, 0);
  EXPECT_FALSE(result.lastBand);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::DrawFailed);
  EXPECT_EQ(result.error.stage, LayerImageStage::Rendering);
  EXPECT_EQ(result.error.firstRow, 0);
  EXPECT_EQ(result.error.message, "test drawing failure before cancellation");
  EXPECT_EQ(reader->rowsRead(), 0);
  EXPECT_FALSE(reader->isDone());
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));

  auto next = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  EXPECT_EQ(next.code, LayerImageReadCode::Failed);
  EXPECT_EQ(next.firstRow, result.firstRow);
  EXPECT_EQ(next.rowCount, 0);
  EXPECT_FALSE(next.lastBand);
  EXPECT_EQ(next.error.code, result.error.code);
  EXPECT_EQ(next.error.stage, result.error.stage);
  EXPECT_EQ(next.error.firstRow, result.error.firstRow);
  EXPECT_EQ(next.error.surfaceSize, result.error.surfaceSize);
  EXPECT_EQ(next.error.message, result.error.message);
  EXPECT_EQ(root->drawCount, 1);
  EXPECT_EQ(reader->rowsRead(), 0);
  EXPECT_FALSE(reader->isDone());
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));
}

TGFX_TEST(LayerImageReaderTest, CancelledReaderStaysCancelled) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(true);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  reader->requestCancel();
  std::vector<uint8_t> pixels(static_cast<size_t>(40) * 8 * 4);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Cancelled);
  result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Cancelled);
  EXPECT_FALSE(reader->isDone());
}

TGFX_TEST(LayerImageReaderTest, PartialBandWritesAreNeverPublished) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  for (bool cancel : {false, true}) {
    auto root = std::make_shared<InterruptSecondTileLayer>();
    root->cancel = cancel;
    LayerImageRequest request = {};
    request.target = root;
    request.sceneRoot = root;
    request.outputSize = {4, 1};
    request.clearColor = Color::White();
    LayerImageReadOptions options = {};
    options.bandRows = 1;
    options.tileSizeLimit = {4, 3};
    auto reader = LayerImageReader::Make(context, request, options);
    ASSERT_TRUE(reader != nullptr);
    root->reader = reader.get();
    auto info = reader->bandInfo();
    std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
    auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
    auto expected = cancel ? LayerImageReadCode::Cancelled : LayerImageReadCode::Failed;
    ASSERT_EQ(result.code, expected);
    if (!cancel) {
      EXPECT_EQ(result.error.code, LayerImageErrorCode::DrawFailed);
      EXPECT_EQ(result.error.stage, LayerImageStage::Rendering);
    }
    EXPECT_EQ(result.firstRow, 0);
    EXPECT_EQ(result.rowCount, 0);
    EXPECT_FALSE(result.lastBand);
    EXPECT_EQ(reader->rowsRead(), 0);
    EXPECT_FALSE(reader->isDone());
    std::vector<uint8_t> partial(info.byteSize(), 0x7f);
    std::fill_n(partial.begin(), 2 * info.bytesPerPixel(), 255);
    EXPECT_EQ(pixels, partial);
    pixels.assign(pixels.size(), 0x5a);
    result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
    EXPECT_EQ(result.code, expected);
    EXPECT_EQ(result.rowCount, 0);
    EXPECT_EQ(root->drawCount, 2);
    EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x5a));
  }
}

TGFX_TEST(LayerImageReaderTest, RejectsPreserve3DContent) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto child = SolidLayer::Make();
  child->setColor(Color::Red());
  child->setWidth(10);
  child->setHeight(10);
  child->setPreserve3D(true);
  root->addChild(child);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  LayerImageError error = {};
  auto reader = LayerImageReader::Make(context, request, options, &error);
  EXPECT_TRUE(reader == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::UnsupportedContent);
}

TGFX_TEST(LayerImageReaderTest, RejectsTinyTileLimit) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  options.tileSizeLimit = {2, 2};
  LayerImageError error = {};
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error.code, LayerImageErrorCode::InvalidInput);

  options.tileSizeLimit = {0, 0};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  std::vector<uint8_t> pixels(static_cast<size_t>(40) * 8 * 4);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::BandReady);
}

TGFX_TEST(LayerImageReaderTest, BackgroundCaptureIncludesAncestorFilterOutset) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto styleBounds =
      BackgroundBlurStyle::Make(4.0f, 4.0f)->filterBackground(Rect::MakeEmpty(), 1.0f);
  auto filterBounds =
      BlurFilter::Make(4.0f, 4.0f)->filterBounds(Rect::MakeEmpty(), 1.0f, MapDirection::Reverse);
  auto requiredOutset =
      static_cast<int>(std::ceil(MaxOutset(styleBounds) + MaxOutset(filterBounds)));
  ASSERT_GT(requiredOutset, 0);
  const PixelRect core = {20, 20, 60, 60};
  for (bool attached : {false, true}) {
    Layer* group = nullptr;
    auto content = BuildNestedFilteredBackdropScene(&group);
    std::shared_ptr<Layer> sceneRoot = content;
    std::unique_ptr<DisplayList> displayList = nullptr;
    if (attached) {
      displayList = MakeDisplayList(content);
      sceneRoot = displayList->root()->shared_from_this();
    }
    LayerDrawRequest request = {};
    request.target = group;
    request.captureRoot = sceneRoot.get();
    request.context = context;
    request.samplingMode = LayerSamplingMode::ExportFixed;
    request.outputSize = {200, 200};
    DrawControl control = {};
    auto session = LayerDrawSession::Prepare(context, request, &control);
    ASSERT_TRUE(session != nullptr) << control.firstFailure.message;
    LayerDrawLimits limits = {};
    limits.gpuLimits = *context->gpu()->limits();
    auto plan = session->planRegion(core, limits, &control);
    ASSERT_EQ(plan.code, RegionPlanCode::Ready) << control.firstFailure.message;
    ASSERT_TRUE(plan.region.dependencies != nullptr);
    auto captureRect = plan.region.dependencies->captureRect;
    EXPECT_GE(captureRect.width(), core.width() + 2 * requiredOutset);
    EXPECT_GE(captureRect.height(), core.height() + 2 * requiredOutset);
  }
}

TGFX_TEST(LayerImageReaderTest, FilterFailureFailsTheBand) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto content = SolidLayer::Make();
  content->setColor(Color::Red());
  content->setWidth(16);
  content->setHeight(16);
  content->setFilters({std::make_shared<FailingFilter>()});
  root->addChild(content);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {16, 16};
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  std::vector<uint8_t> pixels(static_cast<size_t>(16) * 4 * 4, 0x7f);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error.code, LayerImageErrorCode::DrawFailed);
  EXPECT_EQ(result.error.stage, LayerImageStage::Rendering);
  EXPECT_EQ(result.error.message, "a layer filter produced no image");
  EXPECT_EQ(reader->rowsRead(), 0);
  EXPECT_FALSE(reader->isDone());
}

TGFX_TEST(LayerImageReaderTest, ZeroAlphaKeepsClearedOutput) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = SolidLayer::Make();
  content->setColor(Color::Red());
  content->setWidth(8);
  content->setHeight(8);
  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = content;
  request.outputSize = {8, 8};
  request.alpha = 0.0f;
  request.blendMode = BlendMode::Clear;
  request.clearColor = Color::FromRGBA(10, 200, 30, 255);
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  std::vector<uint8_t> pixels(static_cast<size_t>(8) * 8 * 4, 0x7f);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_TRUE(result.lastBand);
  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
    EXPECT_EQ(pixels[i], 10);
    EXPECT_EQ(pixels[i + 1], 200);
    EXPECT_EQ(pixels[i + 2], 30);
    EXPECT_EQ(pixels[i + 3], 255);
  }
}

TGFX_TEST(LayerImageReaderTest, PlusDarkerBlendModeIsAccepted) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto content = SolidLayer::Make();
  content->setColor(Color::FromRGBA(200, 100, 50, 255));
  content->setWidth(8);
  content->setHeight(4);
  LayerImageRequest request = {};
  request.target = content;
  request.sceneRoot = content;
  request.outputSize = {8, 4};
  request.blendMode = BlendMode::PlusDarker;
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  LayerImageError error = {};
  auto reader = LayerImageReader::Make(context, request, options, &error);
  ASSERT_TRUE(reader != nullptr) << static_cast<int>(error.code);
  EXPECT_EQ(error.code, LayerImageErrorCode::None);
  std::vector<uint8_t> pixels(static_cast<size_t>(8) * 4 * 4, 0x7f);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_TRUE(result.lastBand);
}

TGFX_TEST(LayerImageReaderTest, LargeOutputKeepsIntegerBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // One pixel wider than the largest integer a float holds exactly.
  constexpr int OutputWidth = 16777217;
  auto root = Layer::Make();
  auto marker = SolidLayer::Make();
  marker->setColor(Color::Red());
  marker->setWidth(8);
  marker->setHeight(1);
  marker->setMatrix(Matrix::MakeTrans(static_cast<float>(OutputWidth - 5), 0.0f));
  root->addChild(marker);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {OutputWidth, 1};
  request.clearColor = Color::FromRGBA(0, 0, 0, 255);
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  LayerImageError error = {};
  auto reader = LayerImageReader::Make(context, request, options, &error);
  ASSERT_TRUE(reader != nullptr) << error.message;
  std::vector<uint8_t> pixels(static_cast<size_t>(OutputWidth) * 4, 0);
  auto info = reader->bandInfo();
  ASSERT_EQ(info.width(), OutputWidth);
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady)
      << "code=" << static_cast<int>(result.error.code) << " message=" << result.error.message;
  EXPECT_TRUE(result.lastBand);
  EXPECT_TRUE(reader->isDone());
  auto* last = pixels.data() + static_cast<size_t>(OutputWidth - 1) * 4;
  EXPECT_EQ(last[0], 255);
  EXPECT_EQ(last[1], 0);
  EXPECT_EQ(last[2], 0);
  EXPECT_EQ(last[3], 255);
  auto* first = pixels.data();
  EXPECT_EQ(first[0], 0);
  EXPECT_EQ(first[3], 255);
}

TGFX_TEST(LayerImageReaderTest, LargeTileOriginPreservesSinglePixelAlignment) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // With default tiles, the marker is at a core boundary and the halo starts at an odd pixel.
  constexpr int MarkerX = 8201 * 2046;
  for (bool translateRequest : {false, true}) {
    SCOPED_TRACE(translateRequest);
    auto root = Layer::Make();
    auto marker = SolidLayer::Make();
    marker->setWidth(1);
    marker->setHeight(1);
    marker->setColor(Color::Red());
    auto translation = Matrix::MakeTrans(static_cast<float>(MarkerX), 0);
    if (!translateRequest) {
      marker->setMatrix(translation);
    }
    root->addChild(marker);
    const float pathX = translateRequest ? 4.0f : static_cast<float>(MarkerX + 4);
    auto shape = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(pathX, 0.0f, 2.0f, 1.0f));
    shape->setPath(path);
    shape->setFillStyle(ShapeStyle::Make(Color::Blue()));
    root->addChild(shape);
    LayerImageRequest request = {};
    request.target = root;
    request.sceneRoot = root;
    request.outputSize = {MarkerX + 8, 1};
    if (translateRequest) {
      request.targetLocalToOutput = translation;
    }
    LayerImageReadOptions options = {};
    options.bandRows = 1;
    auto reader = LayerImageReader::Make(context, request, options);
    ASSERT_TRUE(reader != nullptr);
    auto info = reader->bandInfo();
    std::vector<uint8_t> pixels(info.byteSize());
    auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << result.error.message;
    const auto* center = pixels.data() + static_cast<size_t>(MarkerX) * 4;
    EXPECT_EQ(center[-1], 0);
    EXPECT_EQ(center[0], 255);
    EXPECT_EQ(center[3], 255);
    EXPECT_EQ(center[7], 0);
    EXPECT_EQ(center[3 * 4 + 3], 0);
    EXPECT_EQ(center[4 * 4 + 2], 255);
    EXPECT_EQ(center[5 * 4 + 2], 255);
    EXPECT_EQ(center[6 * 4 + 3], 0);
  }
}

static void CheckTinyTilePixel(Context* context, Layer* layer, int left) {
  LayerDrawRequest request = {};
  request.target = layer;
  request.captureRoot = layer;
  request.samplingMode = LayerSamplingMode::ExportFixed;
  request.outputSize = {left + 4, 3};
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(context, request, &control);
  ASSERT_TRUE(session != nullptr) << control.firstFailure.message;
  LayerDrawLimits limits = {*context->gpu()->limits(), {3, 3}};
  auto plan = session->planRegion({left, 1, left + 1, 2}, limits, &control);
  ASSERT_EQ(plan.code, RegionPlanCode::Ready) << control.firstFailure.message;
  auto batchPlan = session->planBatch({plan.region}, limits, &control);
  ASSERT_EQ(batchPlan.code, BatchPlanCode::Ready);
  auto batch = session->prepareBatch(context, *batchPlan.plan, &control);
  ASSERT_TRUE(batch != nullptr);
  const auto& rect = plan.region.renderRect;
  ASSERT_LE(rect.width(), 3);
  ASSERT_LE(rect.height(), 3);
  auto surface = Surface::Make(context, rect.width(), rect.height());
  ASSERT_TRUE(surface != nullptr);
  LayerDrawTarget target = {};
  target.canvas = surface->getCanvas();
  target.clearBeforeDraw = true;
  ASSERT_EQ(session->drawRegion(context, *batch, 0, target, &control), DrawOutcome::Success);
  uint8_t pixel[4] = {};
  ASSERT_TRUE(surface->readPixels(OutputInfo(1, 1), pixel, left - rect.left, 1 - rect.top));
  EXPECT_EQ(pixel[0], 255);
  EXPECT_EQ(pixel[3], 255);
}

TGFX_TEST(LayerImageReaderTest, FarBackdropCaptureMatchesNearPixelGrid) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr int CoreWidth = 16;
  std::vector<uint8_t> reference;
  for (int left : {32, (1 << 24) + 32}) {
    SCOPED_TRACE(left);
    auto scene = Layer::Make();
    for (int offset : {4, 10}) {
      auto stripe = SolidLayer::Make();
      stripe->setColor(offset == 4 ? Color::Red() : Color::Blue());
      stripe->setWidth(2);
      stripe->setHeight(8);
      stripe->setMatrix(Matrix::MakeTrans(static_cast<float>(left + offset), 0));
      scene->addChild(stripe);
    }
    auto glass = SolidLayer::Make();
    glass->setWidth(24);
    glass->setHeight(8);
    glass->setColor(Color::FromRGBA(255, 255, 255, 96));
    glass->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
    auto glassPlacement = Matrix::MakeTrans(static_cast<float>(left - 4), 0);
    glass->setMatrix(glassPlacement);
    scene->addChild(glass);

    // Export only the glass. Color variations in the result must come from its sampled backdrop.
    LayerDrawRequest request = {};
    request.target = glass.get();
    request.captureRoot = scene.get();
    request.samplingMode = LayerSamplingMode::ExportFixed;
    request.targetLocalToOutput = glassPlacement;
    request.outputSize = {left + CoreWidth, 8};
    request.clearColor = Color::Black();
    DrawControl control = {};
    auto session = LayerDrawSession::Prepare(context, request, &control);
    ASSERT_TRUE(session != nullptr) << control.firstFailure.message;
    // A one-pixel core plus the blur's two-pixel radius and one sampling pixel on each side
    // fills a seven-pixel target. Neighboring cores exercise both odd and even tile origins.
    LayerDrawLimits limits = {*context->gpu()->limits(), {7, 7}};
    auto info = OutputInfo(1, 1);
    std::vector<uint8_t> pixels(CoreWidth * info.byteSize());
    for (int x = 0; x < CoreWidth; ++x) {
      auto column = left + x;
      auto plan = session->planRegion({column, 4, column + 1, 5}, limits, &control);
      ASSERT_EQ(plan.code, RegionPlanCode::Ready) << control.firstFailure.message;
      auto batchPlan = session->planBatch({plan.region}, limits, &control);
      ASSERT_EQ(batchPlan.code, BatchPlanCode::Ready);
      auto batch = session->prepareBatch(context, *batchPlan.plan, &control);
      ASSERT_TRUE(batch != nullptr) << control.firstFailure.message;
      const auto& rect = plan.region.renderRect;
      auto surface = Surface::Make(context, rect.width(), rect.height());
      ASSERT_TRUE(surface != nullptr);
      LayerDrawTarget target = {};
      target.canvas = surface->getCanvas();
      target.clearBeforeDraw = true;
      ASSERT_EQ(session->drawRegion(context, *batch, 0, target, &control), DrawOutcome::Success)
          << control.firstFailure.message;
      ASSERT_TRUE(surface->readPixels(info, pixels.data() + x * info.byteSize(), column - rect.left,
                                      4 - rect.top));
    }
    EXPECT_GT(pixels[4 * 4], pixels[4 * 4 + 2]);
    EXPECT_GT(pixels[10 * 4 + 2], pixels[10 * 4]);
    if (reference.empty()) {
      reference = pixels;
    } else {
      EXPECT_EQ(pixels, reference);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, TinyTilePreservesFarPathCoordinates) {
  ContextScope scope;
  ASSERT_TRUE(scope.getContext() != nullptr);
  constexpr int Left = 16779246;
  auto shape = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(Left, 0, 2, 3));
  shape->setPath(path);
  shape->setFillStyle(ShapeStyle::Make(Color::Red()));
  CheckTinyTilePixel(scope.getContext(), shape.get(), Left);
}

TGFX_TEST(LayerImageReaderTest, TinyTilePreservesRecordedGeometryAndClips) {
  class PictureTileLayer final : public Layer {
   public:
    std::shared_ptr<Picture> picture;
    bool drawLayer(const DrawArgs&, Canvas* canvas, float, BlendMode) override {
      canvas->drawPicture(picture);
      return true;
    }
  };
  ContextScope scope;
  ASSERT_TRUE(scope.getContext() != nullptr);
  constexpr int Left = 16779246;
  for (bool translateRecording : {false, true}) {
    SCOPED_TRACE(translateRecording);
    PictureRecorder recorder;
    auto* canvas = recorder.beginRecording();
    canvas->clipRect(Rect::MakeXYWH(Left, 0, 2, 3));
    if (translateRecording) {
      canvas->translate(static_cast<float>(Left), 0);
    }
    Paint paint = {};
    paint.setColor(Color::Red());
    canvas->drawRect(Rect::MakeXYWH(translateRecording ? 0 : Left, 0, 2, 3), paint);
    PictureTileLayer layer;
    layer.picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(layer.picture != nullptr);
    CheckTinyTilePixel(scope.getContext(), &layer, Left);
  }
}

enum class TilePrimitive { VertexMesh, ShapeMesh, ImageRect, Atlas, Glyphs, DefaultTextRun };

static void DrawTilePrimitive(Canvas* canvas, TilePrimitive primitive, float left,
                              const std::shared_ptr<Image>& image, const Font& font) {
  Paint paint = {};
  paint.setColor(Color::Red());
  auto rect = Rect::MakeXYWH(left, 0.0f, 2.0f, 3.0f);
  switch (primitive) {
    case TilePrimitive::VertexMesh: {
      Point points[] = {{rect.left, 0}, {rect.right, 0}, {rect.left, 3}, {rect.right, 3}};
      canvas->drawMesh(Mesh::MakeCopy(MeshTopology::TriangleStrip, 4, points), paint);
      break;
    }
    case TilePrimitive::ShapeMesh: {
      Path path = {};
      path.addRect(rect);
      canvas->drawMesh(Mesh::MakeFromPath(std::move(path), false), paint);
      break;
    }
    case TilePrimitive::ImageRect:
      canvas->drawImageRect(image, rect, SamplingOptions(FilterMode::Nearest));
      break;
    case TilePrimitive::Atlas: {
      auto matrix = Matrix::MakeTrans(left, 0);
      auto source = Rect::MakeWH(1, 3);
      canvas->drawAtlas(image, &matrix, &source, nullptr, 1, SamplingOptions(FilterMode::Nearest));
      break;
    }
    case TilePrimitive::Glyphs: {
      auto glyph = font.getGlyphID('H');
      Point position = {left, 5};
      canvas->drawGlyphs(&glyph, &position, 1, font, paint);
      break;
    }
    case TilePrimitive::DefaultTextRun: {
      TextBlobBuilder builder = {};
      auto& run = builder.allocRun(font, 1, left, 5);
      run.glyphs[0] = font.getGlyphID('H');
      canvas->drawTextBlob(builder.build(), 0, 0, paint);
      break;
    }
  }
}

TGFX_TEST(LayerImageReaderTest, TinyTilePreservesMeshImagesAndGlyphs) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto source = Surface::Make(context, 2, 3);
  ASSERT_TRUE(source != nullptr);
  source->getCanvas()->clear(Color::Red());
  auto image = source->makeImageSnapshot();
  ASSERT_TRUE(image != nullptr);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 12);
  auto info = OutputInfo(3, 3);
  for (auto primitive : {TilePrimitive::VertexMesh, TilePrimitive::ShapeMesh, TilePrimitive::ImageRect,
                         TilePrimitive::Atlas, TilePrimitive::Glyphs, TilePrimitive::DefaultTextRun}) {
    SCOPED_TRACE(static_cast<int>(primitive));
    std::vector<uint8_t> reference = {};
    for (int left : {0, 16779246}) {
      auto surface = Surface::Make(context, 3, 3);
      ASSERT_TRUE(surface != nullptr);
      auto* canvas = surface->getCanvas();
      canvas->clear();
      if (left == 0) {
        canvas->translate(1, 0);
      } else {
        canvas->setMatrixForTile(Matrix::I(), left - 1, 0);
      }
      DrawTilePrimitive(canvas, primitive, static_cast<float>(left), image, font);
      std::vector<uint8_t> pixels(info.byteSize());
      ASSERT_TRUE(surface->readPixels(info, pixels.data()));
      if (left == 0) {
        EXPECT_NE(pixels, std::vector<uint8_t>(pixels.size(), 0));
        reference = std::move(pixels);
      } else {
        EXPECT_EQ(pixels, reference);
      }
    }
  }
}

static std::shared_ptr<SolidLayer> MakeExportSolid(const Color& color) {
  auto layer = SolidLayer::Make();
  layer->setColor(color);
  layer->setWidth(TestWidth);
  layer->setHeight(TestHeight);
  return layer;
}

TGFX_TEST(LayerImageReaderTest, HiddenTargetsKeepTheirExportBackdrop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto background = MakeExportSolid(Color::Red());
  auto displayList = MakeDisplayList(background);
  auto sceneRoot = displayList->root()->shared_from_this();
  auto group = Layer::Make();
  sceneRoot->addChild(group);
  auto sibling = MakeExportSolid(Color::Blue());
  group->addChild(sibling);
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  group->addChild(glass);
  for (bool hideAncestor : {false, true}) {
    sibling->setVisible(!hideAncestor);
    auto reference = ReadTiled(context, glass, sceneRoot, {64, 64});
    ASSERT_EQ(reference.error.code, LayerImageErrorCode::None) << reference.error.message;
    const auto* center = reference.image.pixels.data() + reference.image.rowBytes * 60 + 80 * 4;
    ASSERT_GT(center[hideAncestor ? 0 : 2], center[1]);
    sibling->setVisible(true);
    auto layer = hideAncestor ? group : std::static_pointer_cast<Layer>(glass);
    for (bool useAlpha : {false, true}) {
      SCOPED_TRACE(hideAncestor);
      SCOPED_TRACE(useAlpha);
      layer->setVisible(useAlpha);
      layer->setAlpha(useAlpha ? 0.0f : 1.0f);
      auto output = ReadTiled(context, glass, sceneRoot, {64, 64});
      EXPECT_EQ(output.error.code, LayerImageErrorCode::None) << output.error.message;
      EXPECT_EQ(reference.image.pixels, output.image.pixels);
      layer->setVisible(true);
      layer->setAlpha(1.0f);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, OnlyPreceding3DSiblingsAreBackdropDependencies) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = MakeDisplayList(MakeExportSolid(Color::Red()));
  auto sceneRoot = displayList->root()->shared_from_this();
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  sceneRoot->addChild(glass);
  auto reference = ReadTiled(context, glass, sceneRoot, {64, 64});
  ASSERT_EQ(reference.error.code, LayerImageErrorCode::None) << reference.error.message;
  auto sibling = MakeExportSolid(Color::Blue());
  sibling->setPreserve3D(true);
  sceneRoot->addChild(sibling);
  auto output = ReadTiled(context, glass, sceneRoot, {64, 64});
  EXPECT_EQ(output.error.code, LayerImageErrorCode::None) << output.error.message;
  EXPECT_EQ(output.image.pixels, reference.image.pixels);
  sceneRoot->setChildIndex(sibling, 1);
  sibling->setMatrix(Matrix::MakeTrans(10000, 0));
  output = ReadTiled(context, glass, sceneRoot, {64, 64});
  EXPECT_EQ(output.error.code, LayerImageErrorCode::None) << output.error.message;
  EXPECT_EQ(output.image.pixels, reference.image.pixels);
  sibling->setMatrix(Matrix::I());
  output = ReadTiled(context, glass, sceneRoot, {64, 64});
  EXPECT_EQ(output.error.code, LayerImageErrorCode::UnsupportedContent);
}

TGFX_TEST(LayerImageReaderTest, PrecedingBackdropBlurCanSample3DOutsideTargetInput) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto source = MakeExportSolid(Color::Blue());
  source->setWidth(8);
  source->setHeight(20);
  source->setMatrix(Matrix::MakeTrans(16, 0));
  source->setPreserve3D(true);
  auto displayList = MakeDisplayList(source);
  auto sceneRoot = displayList->root()->shared_from_this();
  auto earlierGlass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  earlierGlass->setWidth(40);
  earlierGlass->setHeight(20);
  earlierGlass->setMatrix(Matrix::MakeTrans(10, 0));
  sceneRoot->addChild(earlierGlass);
  auto target = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  target->setWidth(20);
  target->setHeight(20);
  target->setMatrix(Matrix::MakeTrans(30, 0));
  target->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
  sceneRoot->addChild(target);

  // The target reads from x=28 onward, while the 3D source ends at x=24.
  auto direct = ReadTiled(context, target, sceneRoot, {64, 64});
  ASSERT_EQ(direct.error.code, LayerImageErrorCode::None) << direct.error.message;
  // The preceding blur spreads that source to x=34, inside the target's input.
  earlierGlass->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  auto indirect = ReadTiled(context, target, sceneRoot, {64, 64});
  EXPECT_EQ(indirect.error.code, LayerImageErrorCode::UnsupportedContent);
}

TGFX_TEST(LayerImageReaderTest, PrecedingLayerFilterCanSpread3DIntoTargetInput) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto group = Layer::Make();
  auto source = MakeExportSolid(Color::Blue());
  source->setWidth(6);
  source->setHeight(20);
  source->setMatrix(Matrix::MakeTrans(24, 0));
  source->setPreserve3D(true);
  group->addChild(source);
  auto displayList = MakeDisplayList(group);
  auto sceneRoot = displayList->root()->shared_from_this();
  auto target = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  target->setWidth(20);
  target->setHeight(20);
  target->setMatrix(Matrix::MakeTrans(38, 0));
  target->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
  sceneRoot->addChild(target);

  // The target reads from x=36 onward. Filtering the 3D group extends its right edge from 30 to 42.
  auto direct = ReadTiled(context, target, sceneRoot, {64, 64});
  ASSERT_EQ(direct.error.code, LayerImageErrorCode::None) << direct.error.message;
  group->setFilters({BlurFilter::Make(6, 6)});
  auto indirect = ReadTiled(context, target, sceneRoot, {64, 64});
  EXPECT_EQ(indirect.error.code, LayerImageErrorCode::UnsupportedContent);
}

TGFX_TEST(LayerImageReaderTest, LaterSiblingFailureDoesNotInvalidateTargetBackdrop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = MakeDisplayList(MakeExportSolid(Color::Red()));
  auto sceneRoot = displayList->root()->shared_from_this();
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  sceneRoot->addChild(glass);
  auto reference = ReadTiled(context, glass, sceneRoot, {64, 64});
  ASSERT_EQ(reference.error.code, LayerImageErrorCode::None) << reference.error.message;
  auto sibling = MakeExportSolid(Color::Blue());
  sibling->setFilters({std::make_shared<FailingFilter>()});
  sibling->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  sceneRoot->addChild(sibling);
  auto output = ReadTiled(context, glass, sceneRoot, {64, 64});
  EXPECT_EQ(output.error.code, LayerImageErrorCode::None) << output.error.message;
  EXPECT_EQ(output.image.pixels, reference.image.pixels);
}

TGFX_TEST(LayerImageReaderTest, TinyBackgroundBlurAtHighScaleFitsSmallTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto background = MakeExportSolid(Color::Red());
  root->addChild(background);
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setWidth(1);
  glass->setHeight(1);
  glass->setLayerStyles({BackgroundBlurStyle::Make(1.0f / 1024, 1.0f / 1024)});
  root->addChild(glass);
  LayerImageRequest request = {};
  request.target = root;
  request.sceneRoot = root;
  request.outputSize = {8, 8};
  request.targetLocalToOutput = Matrix::MakeScale(1024);
  request.targetLocalToOutput.postTranslate(-512, -512);
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  options.tileSizeLimit = {32, 32};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize());
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << result.error.message;
  EXPECT_EQ(pixels[0], 255);
  EXPECT_NEAR(pixels[1], 96, 1);
  EXPECT_NEAR(pixels[2], 96, 1);
  EXPECT_EQ(pixels[3], 255);
}

TGFX_TEST(LayerImageReaderTest, OffscreenBlurDoesNotEnlargeLocalTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildNestedBackdropScene();
  auto displayList = MakeDisplayList(root);
  auto sceneRoot = displayList->root()->shared_from_this();
  auto upperGlass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 64));
  upperGlass->setWidth(50);
  upperGlass->setHeight(30);
  upperGlass->setMatrix(Matrix::MakeTrans(44, 42));
  upperGlass->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  root->addChild(upperGlass);
  auto reference = RenderReference(context, displayList.get());
  ASSERT_FALSE(reference.pixels.empty());
  auto distant = MakeExportSolid(Color::Blue());
  distant->setMatrix(Matrix::MakeTrans(10000, 0));
  distant->setLayerStyles({BackgroundBlurStyle::Make(100, 100)});
  root->addChild(distant);
  auto output = ReadTiled(context, root, sceneRoot, {64, 64});
  ASSERT_EQ(output.error.code, LayerImageErrorCode::None) << output.error.message;
  EXPECT_EQ(CountDifferentPixels(reference, output.image, 1), 0);
}

}  // namespace tgfx
