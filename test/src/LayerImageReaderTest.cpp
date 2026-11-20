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
#include <array>
#include <cmath>
#include <cstdlib>
#include <future>
#include <vector>
#include "layers/DrawArgs.h"
#include "layers/LayerDrawSession.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
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
static constexpr int ExportCoordinateLimit = 1 << 23;

class FailingCancelLayer final : public Layer {
 public:
  LayerImageReader* reader = nullptr;
  int drawCount = 0;

 protected:
  bool drawLayer(const DrawArgs& args, Canvas*, float, BlendMode) override {
    drawCount++;
    args.drawControl->fail(LayerImageErrorCode::DrawFailed);
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

class InterruptTileLayer final : public Layer {
 public:
  LayerImageReader* reader = nullptr;
  bool cancel = false;
  int interruptAt = 0;
  int drawCount = 0;

 protected:
  bool drawLayer(const DrawArgs& args, Canvas*, float, BlendMode) override {
    if (++drawCount == interruptAt) {
      if (cancel) {
        reader->requestCancel();
      } else {
        args.drawControl->fail(LayerImageErrorCode::DrawFailed);
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

static void ExpectUnsupportedRequest(Context* context, const LayerImageRequest& request,
                                     const LayerImageReadOptions& options) {
  LayerImageErrorCode error = LayerImageErrorCode::None;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::UnsupportedContent);
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
  LayerImageErrorCode error = LayerImageErrorCode::None;
  bool bandSequenceValid = true;
  int bandsRead = 0;
};

static ReadResult ReadAllBands(Context* context, LayerImageReader* reader, size_t rowBytes,
                               uint8_t initialByte) {
  ReadResult output = {};
  const auto outputSize = reader->outputSize();
  const auto& templateInfo = reader->bandInfo();
  output.image.pixels.assign(static_cast<size_t>(outputSize.height) * rowBytes, initialByte);
  output.image.width = outputSize.width;
  output.image.height = outputSize.height;
  output.image.rowBytes = rowBytes;
  int nextRow = 0;
  for (;;) {
    int remaining = outputSize.height - nextRow;
    if (remaining <= 0) {
      break;
    }
    auto rows = std::min(templateInfo.height(), remaining);
    auto bandInfo = ImageInfo::Make(outputSize.width, rows, templateInfo.colorType(),
                                    templateInfo.alphaType(), rowBytes, templateInfo.colorSpace());
    auto* bandPixels = output.image.pixels.data() + static_cast<size_t>(nextRow) * rowBytes;
    auto result = reader->readNextBand(context, Pixmap(bandInfo, bandPixels), bandInfo.byteSize());
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
  if (nextRow != outputSize.height || !reader->isDone() ||
      reader->rowsRead() != outputSize.height) {
    output.bandSequenceValid = false;
  }
  return output;
}

static ReadResult ReadTiled(Context* context, const std::shared_ptr<Layer>& target,
                            const ISize& tileSizeLimit, Color clearColor = Color::Transparent()) {
  ReadResult output = {};
  LayerImageRequest request = {};
  request.target = target;
  request.outputSize = {TestWidth, TestHeight};
  request.targetLocalToOutput = Matrix::I();
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  request.colorType = ColorType::RGBA_8888;
  request.alphaType = AlphaType::Premultiplied;
  request.clearColor = clearColor;

  LayerImageReadOptions options = {};
  options.bandRows = 17;
  options.tileSizeLimit = tileSizeLimit;
  auto reader = LayerImageReader::Make(context, request, options, &output.error);
  if (reader == nullptr) {
    return output;
  }
  return ReadAllBands(context, reader.get(), OutputInfo(TestWidth, TestHeight).rowBytes(), 0);
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
    auto tiled = ReadTiled(context, content, {64, 64});
    ASSERT_FALSE(tiled.image.pixels.empty()) << "code=" << static_cast<int>(tiled.error);
    EXPECT_TRUE(tiled.bandSequenceValid);
    EXPECT_GT(tiled.bandsRead, 1);
    EXPECT_EQ(CountDifferentPixels(reference, tiled.image, 0), 0);
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
  auto read = ReadAllBands(context, reader.get(), reference.rowBytes, 0);
  ASSERT_FALSE(read.image.pixels.empty()) << "error=" << static_cast<int>(read.error);
  EXPECT_TRUE(read.bandSequenceValid);
  const auto& output = read.image;
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

  auto smallTiles = ReadTiled(context, content, {64, 64});
  ASSERT_FALSE(smallTiles.image.pixels.empty()) << "code=" << static_cast<int>(smallTiles.error);
  EXPECT_TRUE(smallTiles.bandSequenceValid);
  // A layer filter applied to the enclosing group rasterizes its intermediate image at the tile's
  // clip window, so its fractional output offset lands on a tile-dependent sub-pixel phase. That
  // may move individual edge channels by one 8-bit step, but must never drop or duplicate content.
  EXPECT_EQ(CountDifferentPixels(reference, smallTiles.image, 1), 0);
  EXPECT_LE(CountDifferentPixels(reference, smallTiles.image, 0), TestWidth * TestHeight / 200);

  auto largeTiles = ReadTiled(context, content, {4096, 4096});
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
  request.outputSize = {scaledWidth, scaledHeight};
  request.targetLocalToOutput = Matrix::MakeScale(scale);
  request.renderColorSpace = ColorSpace::SRGB();
  request.outputColorSpace = ColorSpace::SRGB();
  LayerImageReadOptions options = {};
  options.bandRows = 9;
  options.tileSizeLimit = {24, 20};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto output = ReadAllBands(context, reader.get(), referenceInfo.rowBytes(), 0);
  ASSERT_FALSE(output.image.pixels.empty()) << "error=" << static_cast<int>(output.error);
  EXPECT_TRUE(output.bandSequenceValid);
  EXPECT_EQ(CountDifferentPixels(reference, output.image, 0), 0);
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
  auto output = ReadAllBands(context, reader.get(), rowBytes, 0xcc);
  ASSERT_FALSE(output.image.pixels.empty()) << "error=" << static_cast<int>(output.error);
  EXPECT_TRUE(output.bandSequenceValid);
  for (int y = 0; y < TestHeight; ++y) {
    auto* row = output.image.pixels.data() + static_cast<size_t>(y) * rowBytes;
    for (size_t i = 0; i < padding; ++i) {
      EXPECT_EQ(row[info.rowBytes() + i], 0xcc);
    }
  }
  EXPECT_EQ(CountDifferentPixels(reference, output.image, 0), 0);
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

TGFX_TEST(LayerImageReaderTest, DestinationWithoutColorSpaceUsesSRGB) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  LayerImageRequest request = {};
  request.target = Layer::Make();
  request.outputSize = {1, 1};
  request.clearColor = Color::Red();
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_NE(reader, nullptr);
  auto info = reader->bandInfo().makeColorSpace(nullptr);
  ASSERT_EQ(info.colorSpace(), nullptr);
  uint8_t pixels[4] = {};
  auto result = reader->readNextBand(context, Pixmap(info, pixels), sizeof(pixels));
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_EQ(pixels[0], 255);
  EXPECT_EQ(pixels[1], 0);
  EXPECT_EQ(pixels[2], 0);
  EXPECT_EQ(pixels[3], 255);
}

TGFX_TEST(LayerImageReaderTest, ReadsUnpremultipliedColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  LayerImageRequest request = {};
  request.target = Layer::Make();
  request.outputSize = {1, 1};
  request.alphaType = AlphaType::Unpremultiplied;
  request.clearColor = Color::FromRGBA(200, 120, 40, 128);
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  uint8_t pixel[4] = {};
  auto result = reader->readNextBand(context, Pixmap(reader->bandInfo(), pixel), sizeof(pixel));
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << static_cast<int>(result.error);
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
  EXPECT_EQ(result.error, LayerImageErrorCode::ContextMismatch);
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
  auto smallTiles = ReadTiled(context, content, {32, 24});
  ASSERT_FALSE(smallTiles.image.pixels.empty());
  auto largeTiles = ReadTiled(context, content, {4096, 4096});
  ASSERT_FALSE(largeTiles.image.pixels.empty());
  EXPECT_TRUE(smallTiles.bandSequenceValid);
  EXPECT_TRUE(largeTiles.bandSequenceValid);
  EXPECT_EQ(CountDifferentPixels(smallTiles.image, largeTiles.image, 0), 0);
}

TGFX_TEST(LayerImageReaderTest, UsesExactSurfaceSizesForFullAndEdgeTiles) {
  class ColorPatternLayer final : public Layer {
   public:
    std::vector<ISize> surfaceSizes = {};

   protected:
    bool drawLayer(const DrawArgs&, Canvas* canvas, float, BlendMode) override {
      auto surface = canvas->getSurface();
      surfaceSizes.push_back(surface != nullptr ? ISize::Make(surface->width(), surface->height())
                                                : ISize{});
      Paint paint = {};
      paint.setColor(Color::Red());
      canvas->drawRect(Rect::MakeWH(2, 2), paint);
      paint.setColor(Color::Blue());
      canvas->drawRect(Rect::MakeXYWH(12, 6, 1, 1), paint);
      return true;
    }
  };

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto target = std::make_shared<ColorPatternLayer>();
  LayerImageRequest request = {};
  request.target = target;
  request.outputSize = {13, 7};
  LayerImageReadOptions options = {};
  options.bandRows = 7;
  options.tileSizeLimit = {8, 6};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize(), 0x7f);
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_TRUE(result.lastBand);
  const std::vector<ISize> expectedSizes = {{8, 6}, {8, 6}, {3, 6}, {8, 5}, {8, 5}, {3, 5}};
  EXPECT_EQ(target->surfaceSizes, expectedSizes);
  auto row = pixels.data();
  for (int y = 0; y < info.height(); ++y, row += info.rowBytes()) {
    for (int x = 0; x < info.width(); ++x) {
      auto pixel = row + x * 4;
      bool red = x < 2 && y < 2;
      bool blue = x == 12 && y == 6;
      EXPECT_EQ(pixel[0], red ? 255 : 0);
      EXPECT_EQ(pixel[1], 0);
      EXPECT_EQ(pixel[2], blue ? 255 : 0);
      EXPECT_EQ(pixel[3], red || blue ? 255 : 0);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, PlainTilesCullStyledSubtrees) {
  class CountingStyle final : public LayerStyle {
   public:
    int draws = 0;

    LayerStylePosition position() const override {
      return LayerStylePosition::Below;
    }

    Rect filterBounds(const Rect& srcRect, float) override {
      return srcRect;
    }

   protected:
    void onDraw(Canvas*, const LayerStyleInput&, float, BlendMode) override {
      ++draws;
    }
  };

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto root = Layer::Make();
  auto group = Layer::Make();
  auto solid = SolidLayer::Make();
  solid->setWidth(4);
  solid->setHeight(4);
  solid->setColor(Color::Red());
  auto style = std::make_shared<CountingStyle>();
  solid->setLayerStyles({style});
  group->addChild(solid);
  root->addChild(group);
  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {64, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  options.tileSizeLimit = {18, 10};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_NE(reader, nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize());
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_EQ(style->draws, 1);
  EXPECT_EQ(pixels[0], 255);
  EXPECT_EQ(pixels[info.minRowBytes() - 1], 0);
}

TGFX_TEST(LayerImageReaderTest, PassThroughOffscreenRespectsEdgeTileSize) {
  class MeasuringSolidLayer final : public SolidLayer {
   public:
    std::vector<ISize> surfaceSizes = {};

   protected:
    bool drawLayer(const DrawArgs& args, Canvas* canvas, float alpha,
                   BlendMode blendMode) override {
      auto surface = canvas->getSurface();
      surfaceSizes.push_back(surface != nullptr ? ISize::Make(surface->width(), surface->height())
                                                : ISize{});
      return Layer::drawLayer(args, canvas, alpha, blendMode);
    }
  };

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto group = Layer::Make();
  group->setAllowsGroupOpacity(true);
  auto child = std::make_shared<MeasuringSolidLayer>();
  child->setColor(Color::Red());
  child->setWidth(64);
  child->setHeight(64);
  child->setMatrix(Matrix::MakeTrans(-16, -16));
  child->setBlendMode(BlendMode::Multiply);
  group->addChild(child);
  auto displayList = MakeDisplayList(group);

  LayerImageRequest request = {};
  request.target = group;
  request.alpha = 0.5f;
  request.outputSize = {7, 4};
  request.targetLocalToOutput = Matrix::MakeSkew(0.5f, 0.0f);
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  options.tileSizeLimit = {8, 6};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize());
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  const std::vector<ISize> expectedSizes = {{8, 6}, {3, 6}};
  EXPECT_EQ(child->surfaceSizes, expectedSizes);
}

TGFX_TEST(LayerImageReaderTest, EmptySceneDeliversClearColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {40, 30};
  request.clearColor = Color::FromRGBA(200, 10, 20, 255);
  LayerImageReadOptions options = {};
  options.bandRows = 7;
  options.tileSizeLimit = {16, 16};
  LayerImageErrorCode error = LayerImageErrorCode::None;
  auto reader = LayerImageReader::Make(context, request, options, &error);
  ASSERT_TRUE(reader != nullptr);
  ASSERT_EQ(error, LayerImageErrorCode::None);
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
  request.outputSize = {40, 30};
  LayerImageErrorCode error = LayerImageErrorCode::None;

  LayerImageReadOptions options = {};
  options.bandRows = 0;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  options.bandRows = 4;
  options.tileSizeLimit = {32, 0};
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  for (const auto& tileSizeLimit : {ISize{-1, -1}, ISize{-1, 0}, ISize{0, -1}}) {
    options.tileSizeLimit = tileSizeLimit;
    EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
    EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);
  }

  options.tileSizeLimit = {0, 0};
  request.colorType = ColorType::ALPHA_8;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.colorType = ColorType::RGBA_8888;
  request.alphaType = AlphaType::Opaque;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.alphaType = AlphaType::Premultiplied;
  request.renderColorSpace = nullptr;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.renderColorSpace = ColorSpace::SRGB();
  EXPECT_TRUE(LayerImageReader::Make(nullptr, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::ContextUnavailable);

  request.outputSize = {0, 30};
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.outputSize = {40, 30};
  request.targetLocalToOutput = Matrix::MakeScale(0, 1);
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.targetLocalToOutput = Matrix::I();
  request.alpha = 2.0f;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

  request.alpha = 1.0f;
  auto ancestor = Layer::Make();
  ancestor->setMatrix(Matrix::MakeScale(0, 1));
  ancestor->addChild(root);
  auto displayList = MakeDisplayList(ancestor);
  ExpectUnsupportedRequest(context, request, options);
}

TGFX_TEST(LayerImageReaderTest, RejectsReadOnlyAndMismatchedDestination) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
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
  EXPECT_EQ(result.error, LayerImageErrorCode::InvalidInput);
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
  EXPECT_EQ(result.error, LayerImageErrorCode::InvalidInput);
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
  EXPECT_EQ(result.error, LayerImageErrorCode::InvalidInput);
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
  ASSERT_EQ(first.error, LayerImageErrorCode::InvalidInput);
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));

  Context* readContexts[] = {nullptr, context};
  for (auto* readContext : readContexts) {
    pixels.assign(pixels.size(), 0x3f);
    auto result = reader->readNextBand(readContext, Pixmap(info, pixels.data()), pixels.size());
    EXPECT_EQ(result.code, LayerImageReadCode::Failed);
    EXPECT_EQ(result.firstRow, 0);
    EXPECT_EQ(result.rowCount, 0);
    EXPECT_FALSE(result.lastBand);
    EXPECT_EQ(result.error, first.error);
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
  EXPECT_EQ(result.error, LayerImageErrorCode::DrawFailed);
  EXPECT_EQ(reader->rowsRead(), 0);
  EXPECT_FALSE(reader->isDone());
  EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x7f));

  auto next = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  EXPECT_EQ(next.code, LayerImageReadCode::Failed);
  EXPECT_EQ(next.firstRow, result.firstRow);
  EXPECT_EQ(next.rowCount, 0);
  EXPECT_FALSE(next.lastBand);
  EXPECT_EQ(next.error, result.error);
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
  // Interrupt the second tile of a batch, once in the first batch and once after a full batch
  // has been copied to the destination.
  for (int completedTiles : {0, 64}) {
    for (bool cancel : {false, true}) {
      auto root = std::make_shared<InterruptTileLayer>();
      root->cancel = cancel;
      root->interruptAt = completedTiles + 2;
      LayerImageRequest request = {};
      request.target = root;
      request.outputSize = {root->interruptAt * 2, 1};
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
        EXPECT_EQ(result.error, LayerImageErrorCode::DrawFailed);
      }
      EXPECT_EQ(result.firstRow, 0);
      EXPECT_EQ(result.rowCount, 0);
      EXPECT_FALSE(result.lastBand);
      EXPECT_EQ(reader->rowsRead(), 0);
      EXPECT_FALSE(reader->isDone());
      std::vector<uint8_t> partial(info.byteSize(), 0x7f);
      std::fill_n(partial.begin(), static_cast<size_t>(completedTiles) * 2 * info.bytesPerPixel(),
                  255);
      EXPECT_EQ(pixels, partial);
      context->flushAndSubmit(true);
      EXPECT_EQ(pixels, partial);
      pixels.assign(pixels.size(), 0x5a);
      result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
      EXPECT_EQ(result.code, expected);
      EXPECT_EQ(result.rowCount, 0);
      EXPECT_EQ(root->drawCount, root->interruptAt);
      EXPECT_EQ(pixels, std::vector<uint8_t>(pixels.size(), 0x5a));
    }
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
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  LayerImageErrorCode error = LayerImageErrorCode::None;
  auto reader = LayerImageReader::Make(context, request, options, &error);
  EXPECT_TRUE(reader == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::UnsupportedContent);
}

TGFX_TEST(LayerImageReaderTest, RejectsTinyTileLimit) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildTestScene(false);
  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {40, 30};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  options.tileSizeLimit = {2, 2};
  LayerImageErrorCode error = LayerImageErrorCode::None;
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::InvalidInput);

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
    std::unique_ptr<DisplayList> displayList = nullptr;
    if (attached) {
      displayList = MakeDisplayList(content);
    }
    LayerDrawRequest request = {};
    request.target = group;
    request.outputSize = {200, 200};
    DrawControl control = {};
    auto session = LayerDrawSession::Prepare(request, control);
    ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);
    LayerDrawLimits limits = {};
    limits.gpuLimits = *context->gpu()->limits();
    limits.outputSurfaceLimit = {core.width() + 2, core.height() + 2};
    auto plan = session->planRegion(core, limits, control);
    ASSERT_EQ(plan.code, RegionPlanCode::Ready) << static_cast<int>(control.error);
    ASSERT_TRUE(plan.region.captureRect.has_value());
    EXPECT_EQ(plan.region.renderRect.width(), core.width() + 2);
    EXPECT_EQ(plan.region.renderRect.height(), core.height() + 2);
    const auto& captureRect = *plan.region.captureRect;
    EXPECT_GE(captureRect.width(), core.width() + 2 * requiredOutset);
    EXPECT_GE(captureRect.height(), core.height() + 2 * requiredOutset);

    // The draw of one pixel still fits when the capture hits the actual GPU limit.
    limits.gpuLimits.maxTextureDimension2D = 3;
    plan = session->planRegion({20, 20, 21, 21}, limits, control);
    EXPECT_EQ(plan.code, RegionPlanCode::Failed);
    EXPECT_EQ(control.error, LayerImageErrorCode::DeviceLimitExceeded);
  }
}

static LayerRegionPlan MakeRegionWithCapture(const PixelRect& captureRect) {
  LayerRegionPlan region = {};
  region.captureRect = captureRect;
  return region;
}

TGFX_TEST(LayerImageReaderTest, SplitsSharedBackgroundCaptureAtMemoryBudget) {
  auto root = Layer::Make();
  LayerDrawRequest request = {};
  request.target = root.get();
  request.outputSize = {4097, 2048};
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(request, control);
  ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);

  // At this height, a shared capture 4096 pixels wide occupies exactly 32 MiB in RGBA8.
  LayerDrawLimits limits = {};
  limits.gpuLimits.maxTextureDimension2D = 16384;
  auto left = MakeRegionWithCapture({0, 0, 2048, 2048});
  auto atBudget =
      session->planBatch({left, MakeRegionWithCapture({2048, 0, 4096, 2048})}, limits, control);
  ASSERT_EQ(atBudget.code, BatchPlanCode::Ready) << static_cast<int>(control.error);
  ASSERT_TRUE(atBudget.captureRect.has_value());
  EXPECT_EQ(atBudget.captureRect->width(), 4096);
  EXPECT_EQ(atBudget.captureRect->height(), 2048);

  auto right = MakeRegionWithCapture({2048, 0, 4097, 2048});
  EXPECT_EQ(session->planBatch({left, right}, limits, control).code, BatchPlanCode::RequiresSplit);
  EXPECT_EQ(session->planBatch({left}, limits, control).code, BatchPlanCode::Ready);
  EXPECT_EQ(session->planBatch({right}, limits, control).code, BatchPlanCode::Ready);
  EXPECT_FALSE(control.failed()) << static_cast<int>(control.error);
}

TGFX_TEST(LayerImageReaderTest, AppliesMemoryBudgetToIndividualBackgroundCaptures) {
  auto target = SolidLayer::Make();
  target->setColor(Color::White());
  target->setWidth(2048);
  target->setHeight(2048);
  target->setLayerStyles({BackgroundBlurStyle::Make(512, 512)});
  LayerDrawRequest request = {};
  request.target = target.get();
  request.outputSize = {2048, 2048};
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(request, control);
  ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);

  LayerDrawLimits limits = {};
  limits.gpuLimits.maxTextureDimension2D = 16384;
  // The first tile fits the output surface limit and has a 4096 x 4096 background capture.
  EXPECT_EQ(session->planRegion({0, 0, 2046, 2046}, limits, control).code,
            RegionPlanCode::RequiresSubdivision);
  auto smaller = session->planRegion({0, 0, 512, 512}, limits, control);
  EXPECT_EQ(smaller.code, RegionPlanCode::Ready) << static_cast<int>(control.error);
  EXPECT_FALSE(control.failed()) << static_cast<int>(control.error);

  // A larger blur exceeds the capture budget even when the core is a single pixel.
  target->setLayerStyles({BackgroundBlurStyle::Make(768, 768)});
  session = LayerDrawSession::Prepare(request, control);
  ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);
  for (const auto& core : {PixelRect{0, 0, 2046, 2046}, PixelRect{0, 0, 1, 1}}) {
    control = {};
    EXPECT_EQ(session->planRegion(core, limits, control).code, RegionPlanCode::Failed);
    EXPECT_EQ(control.error, LayerImageErrorCode::DeviceLimitExceeded);
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
  request.outputSize = {16, 16};
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  std::vector<uint8_t> pixels(static_cast<size_t>(16) * 4 * 4, 0x7f);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::Failed);
  EXPECT_EQ(result.error, LayerImageErrorCode::DrawFailed);
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
  request.outputSize = {8, 4};
  request.blendMode = BlendMode::PlusDarker;
  LayerImageReadOptions options = {};
  options.bandRows = 4;
  LayerImageErrorCode error = LayerImageErrorCode::None;
  auto reader = LayerImageReader::Make(context, request, options, &error);
  ASSERT_TRUE(reader != nullptr) << static_cast<int>(error);
  EXPECT_EQ(error, LayerImageErrorCode::None);
  std::vector<uint8_t> pixels(static_cast<size_t>(8) * 4 * 4, 0x7f);
  auto info = reader->bandInfo();
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), info.byteSize());
  EXPECT_EQ(result.code, LayerImageReadCode::BandReady);
  EXPECT_TRUE(result.lastBand);
}

TGFX_TEST(LayerImageReaderTest, OutputCoordinateLimit) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  LayerImageRequest request = {};
  request.target = Layer::Make();
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  for (bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    request.outputSize =
        vertical ? ISize::Make(1, ExportCoordinateLimit) : ISize::Make(ExportCoordinateLimit, 1);
    LayerImageErrorCode error = LayerImageErrorCode::None;
    auto reader = LayerImageReader::Make(context, request, options, &error);
    ASSERT_TRUE(reader != nullptr) << static_cast<int>(error);
    EXPECT_EQ(reader->bandInfo().width(), request.outputSize.width);

    request.outputSize = vertical ? ISize::Make(1, ExportCoordinateLimit + 1)
                                  : ISize::Make(ExportCoordinateLimit + 1, 1);
    EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
    EXPECT_EQ(error, LayerImageErrorCode::UnsupportedContent);
  }
}

TGFX_TEST(LayerImageReaderTest, RejectsOutOfRangeOutputTranslation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  LayerImageRequest request = {};
  request.target = Layer::Make();
  request.outputSize = {8, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  constexpr float BeyondLimit = ExportCoordinateLimit + 1;
  for (const auto& matrix :
       {Matrix::MakeTrans(BeyondLimit, 0), Matrix::MakeTrans(-BeyondLimit, 0),
        Matrix::MakeTrans(0, BeyondLimit), Matrix::MakeTrans(0, -BeyondLimit)}) {
    request.targetLocalToOutput = matrix;
    LayerImageErrorCode error = LayerImageErrorCode::None;
    EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
    EXPECT_EQ(error, LayerImageErrorCode::UnsupportedContent);
  }
}

TGFX_TEST(LayerImageReaderTest, ExportsLargeLocalCoordinatesScaledToOutput) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto shape = ShapeLayer::Make();
  constexpr float LocalX = 10000000.0f;
  constexpr float Scale = 1.0f / 128.0f;
  Path path = {};
  path.addRect(Rect::MakeXYWH(LocalX, 0.0f, 4 / Scale, 4 / Scale));
  shape->setPath(path);
  shape->setFillStyle(ShapeStyle::Make(Color::Red()));
  auto matrix = Matrix::MakeScale(Scale, Scale);
  matrix.postTranslate(-LocalX * Scale, 0);
  shape->setMatrix(matrix);
  root->addChild(shape);

  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {8, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 8;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize());
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady);
  const auto* inside = pixels.data() + info.rowBytes() + 4;
  EXPECT_EQ(inside[0], 255);
  EXPECT_EQ(inside[3], 255);
  const auto* outside = pixels.data() + info.rowBytes() * 6 + 24;
  EXPECT_EQ(outside[3], 0);
}

TGFX_TEST(LayerImageReaderTest, RejectsOutOfRangeBackdropTranslation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = Layer::Make();
  auto glass = SolidLayer::Make();
  glass->setWidth(8);
  glass->setHeight(8);
  glass->setColor(Color::FromRGBA(255, 255, 255, 96));
  glass->setMatrix(Matrix::MakeTrans(ExportCoordinateLimit + 1, 0));
  root->addChild(glass);

  LayerImageRequest request = {};
  request.target = glass;
  request.outputSize = {8, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  LayerImageErrorCode error = LayerImageErrorCode::None;
  ASSERT_TRUE(LayerImageReader::Make(context, request, options, &error) != nullptr)
      << static_cast<int>(error);

  glass->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
  ASSERT_TRUE(LayerImageReader::Make(context, request, options, &error) != nullptr)
      << static_cast<int>(error);

  auto displayList = MakeDisplayList(root);
  EXPECT_TRUE(LayerImageReader::Make(context, request, options, &error) == nullptr);
  EXPECT_EQ(error, LayerImageErrorCode::UnsupportedContent);
}

static void CheckTinyTilePixel(Context* context, Layer* layer, int left) {
  LayerDrawRequest request = {};
  request.target = layer;
  request.outputSize = {left + 1, 3};
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(request, control);
  ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);
  LayerDrawLimits limits = {*context->gpu()->limits(), {3, 3}};
  auto plan = session->planRegion({left, 1, left + 1, 2}, limits, control);
  ASSERT_EQ(plan.code, RegionPlanCode::Ready) << static_cast<int>(control.error);
  auto batchPlan = session->planBatch({plan.region}, limits, control);
  ASSERT_EQ(batchPlan.code, BatchPlanCode::Ready);
  auto batch = session->prepareBatch(context, batchPlan.captureRect, control);
  ASSERT_TRUE(batch.has_value());
  const auto& rect = plan.region.renderRect;
  ASSERT_LE(rect.width(), 3);
  ASSERT_LE(rect.height(), 3);
  auto surface = Surface::Make(context, rect.width(), rect.height());
  ASSERT_TRUE(surface != nullptr);
  ASSERT_EQ(session->drawRegion(plan.region, *batch, surface->getCanvas(), control),
            DrawOutcome::Success);
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
  for (int left : {32, ExportCoordinateLimit - 64}) {
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
    auto displayList = MakeDisplayList(scene);

    // Export only the glass. Color variations in the result must come from its sampled backdrop.
    LayerDrawRequest request = {};
    request.target = glass.get();
    request.targetLocalToOutput = glassPlacement;
    request.outputSize = {left + CoreWidth, 8};
    request.clearColor = Color::Black();
    DrawControl control = {};
    auto session = LayerDrawSession::Prepare(request, control);
    ASSERT_TRUE(session != nullptr) << static_cast<int>(control.error);
    // The blur captures seven pixels across, while each core draws into a three-pixel tile.
    // Neighboring cores exercise both odd and even tile origins.
    LayerDrawLimits limits = {*context->gpu()->limits(), {3, 3}};
    auto info = OutputInfo(1, 1);
    std::vector<uint8_t> pixels(CoreWidth * info.byteSize());
    for (int x = 0; x < CoreWidth; ++x) {
      auto column = left + x;
      auto plan = session->planRegion({column, 4, column + 1, 5}, limits, control);
      ASSERT_EQ(plan.code, RegionPlanCode::Ready) << static_cast<int>(control.error);
      ASSERT_TRUE(plan.region.captureRect.has_value());
      EXPECT_EQ(plan.region.captureRect->width(), 7);
      EXPECT_EQ(plan.region.captureRect->height(), 7);
      auto batchPlan = session->planBatch({plan.region}, limits, control);
      ASSERT_EQ(batchPlan.code, BatchPlanCode::Ready);
      auto batch = session->prepareBatch(context, batchPlan.captureRect, control);
      ASSERT_TRUE(batch.has_value()) << static_cast<int>(control.error);
      const auto& rect = plan.region.renderRect;
      EXPECT_EQ(rect.width(), 3);
      EXPECT_EQ(rect.height(), 3);
      auto surface = Surface::Make(context, rect.width(), rect.height());
      ASSERT_TRUE(surface != nullptr);
      ASSERT_EQ(session->drawRegion(plan.region, *batch, surface->getCanvas(), control),
                DrawOutcome::Success)
          << static_cast<int>(control.error);
      ASSERT_TRUE(surface->readPixels(info,
                                      pixels.data() + static_cast<size_t>(x) * info.byteSize(),
                                      column - rect.left, 4 - rect.top));
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

TGFX_TEST(LayerImageReaderTest, TinyTileAtCoordinateLimitPreservesPath) {
  ContextScope scope;
  ASSERT_TRUE(scope.getContext() != nullptr);
  constexpr int Left = ExportCoordinateLimit - 1;
  auto shape = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(Left, 0, 1, 3));
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
  constexpr int Left = ExportCoordinateLimit - 18;
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

static std::shared_ptr<SolidLayer> MakeExportSolid(const Color& color) {
  auto layer = SolidLayer::Make();
  layer->setColor(color);
  layer->setWidth(TestWidth);
  layer->setHeight(TestHeight);
  return layer;
}

static std::shared_ptr<SolidLayer> MakeExportGlass(float x, float y, float size, float blur = 2) {
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setWidth(size);
  glass->setHeight(size);
  glass->setMatrix(Matrix::MakeTrans(x, y));
  glass->setLayerStyles({BackgroundBlurStyle::Make(blur, blur)});
  return glass;
}

static void ExpectExportPixel(const PixelImage& image, int x, int y, const std::array<int, 4>& rgba,
                              int tolerance = 0) {
  const auto* pixel = image.pixels.data() + static_cast<size_t>(y) * image.rowBytes + x * 4;
  for (size_t channel = 0; channel < rgba.size(); ++channel) {
    EXPECT_NEAR(pixel[channel], rgba[channel], tolerance)
        << "pixel (" << x << ", " << y << ") channel " << channel;
  }
}

TGFX_TEST(LayerImageReaderTest, RejectsHiddenTargetBranches) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto ancestor = Layer::Make();
  auto target = MakeExportSolid(Color::Red());
  ancestor->addChild(target);
  auto displayList = MakeDisplayList(ancestor);
  LayerImageRequest request = {};
  request.target = target;
  request.outputSize = {8, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 1;

  for (auto* layer : {static_cast<Layer*>(target.get()), ancestor.get()}) {
    SCOPED_TRACE(layer == target.get() ? "target" : "ancestor");
    layer->setVisible(false);
    ExpectUnsupportedRequest(context, request, options);
    layer->setVisible(true);
    layer->setAlpha(0);
    ExpectUnsupportedRequest(context, request, options);
    layer->setAlpha(1);
  }
  EXPECT_TRUE(LayerImageReader::Make(context, request, options) != nullptr);
}

TGFX_TEST(LayerImageReaderTest, Rejects3DThatParticipatesInSceneDrawing) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto target = MakeExportSolid(Color::Red());
  auto displayList = MakeDisplayList(target);
  auto laterSibling = MakeExportSolid(Color::Blue());
  laterSibling->setMatrix(Matrix::MakeTrans(10000, 0));
  displayList->root()->addChild(laterSibling);

  LayerImageRequest request = {};
  request.target = target;
  request.outputSize = {8, 8};
  LayerImageReadOptions options = {};
  options.bandRows = 1;
  ASSERT_TRUE(LayerImageReader::Make(context, request, options) != nullptr);

  // The later sibling is outside the output and the target does not sample the backdrop.
  laterSibling->setPreserve3D(true);
  ExpectUnsupportedRequest(context, request, options);
  laterSibling->setVisible(false);
  EXPECT_TRUE(LayerImageReader::Make(context, request, options) != nullptr);
  laterSibling->setVisible(true);
  laterSibling->setAlpha(0);
  EXPECT_TRUE(LayerImageReader::Make(context, request, options) != nullptr);
  laterSibling->setAlpha(1);
  laterSibling->setPreserve3D(false);
  auto matrix = Matrix3D::MakeRotate({0, 1, 0}, 45);
  matrix.postTranslate(10000, 0, 0);
  laterSibling->setMatrix3D(matrix);
  ExpectUnsupportedRequest(context, request, options);

  auto maskOwner = MakeExportSolid(Color::Blue());
  displayList->root()->addChild(maskOwner);
  maskOwner->setMask(laterSibling);
  maskOwner->setVisible(false);
  EXPECT_TRUE(LayerImageReader::Make(context, request, options) != nullptr);
  maskOwner->setVisible(true);
  ExpectUnsupportedRequest(context, request, options);
}

TGFX_TEST(LayerImageReaderTest, LaterSiblingFailureDoesNotInvalidateTargetBackdrop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = MakeDisplayList(MakeExportSolid(Color::Red()));
  auto sceneRoot = displayList->root();
  auto group = Layer::Make();
  group->setFilters({BlurFilter::Make(0.5f, 0.5f)});
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  group->addChild(glass);
  sceneRoot->addChild(group);
  auto reference = ReadTiled(context, glass, {64, 64});
  ASSERT_EQ(reference.error, LayerImageErrorCode::None);
  ASSERT_FALSE(reference.image.pixels.empty());
  const auto* center = reference.image.pixels.data() + reference.image.rowBytes * 60 + 80 * 4;
  EXPECT_EQ(center[0], 255);
  EXPECT_NEAR(center[1], 96, 1);
  EXPECT_NEAR(center[2], 96, 1);
  for (auto* parent : {group.get(), sceneRoot}) {
    auto sibling = MakeExportSolid(Color::Blue());
    sibling->setFilters({std::make_shared<FailingFilter>()});
    sibling->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
    parent->addChild(sibling);
  }
  auto output = ReadTiled(context, glass, {64, 64});
  EXPECT_EQ(output.error, LayerImageErrorCode::None);
  EXPECT_EQ(output.image.pixels, reference.image.pixels);
}

TGFX_TEST(LayerImageReaderTest, ScrolledTargetKeepsLocalBackdropOutsideItsViewport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = MakeDisplayList(MakeExportSolid(Color::Red()));
  auto target = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  target->setWidth(12);
  target->setHeight(12);
  target->setMatrix(Matrix::MakeTrans(20, 20));
  target->setScrollRect(Rect::MakeXYWH(2, 2, 4, 4));
  target->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  displayList->root()->addChild(target);

  auto output = ReadTiled(context, target, {16, 16});
  ASSERT_EQ(output.error, LayerImageErrorCode::None);
  ASSERT_FALSE(output.image.pixels.empty());
  // Direct target drawing uses the requested local mapping across the complete target.
  const auto* pixel = output.image.pixels.data() + output.image.rowBytes * 8 + 8 * 4;
  EXPECT_EQ(pixel[0], 255);
  EXPECT_NEAR(pixel[1], 96, 1);
  EXPECT_NEAR(pixel[2], 96, 1);
  EXPECT_EQ(pixel[3], 255);
}

TGFX_TEST(LayerImageReaderTest, AncestorScrollRectClipsBackdropAndPlainTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto ancestor = Layer::Make();
  ancestor->setMatrix(Matrix::MakeTrans(7, 9));
  ancestor->setScrollRect(Rect::MakeXYWH(12, 8, 20, 20));
  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(3, 1));
  auto target = Layer::Make();
  target->setMatrix(Matrix::MakeTrans(7, 3));
  target->addChild(MakeExportSolid(Color::Red()));
  target->addChild(MakeExportGlass(6, 7, 10));
  target->addChild(MakeExportGlass(96, 72, 16, 5000));
  parent->addChild(target);
  ancestor->addChild(parent);
  auto displayList = MakeDisplayList(ancestor);

  for (bool scrolledTarget : {false, true}) {
    SCOPED_TRACE(scrolledTarget);
    if (scrolledTarget) {
      target->setScrollRect(Rect::MakeWH(4, 6));
    }
    auto output = ReadTiled(context, target, {32, 32});
    ASSERT_EQ(output.error, LayerImageErrorCode::None);
    ASSERT_FALSE(output.image.pixels.empty());
    EXPECT_TRUE(output.bandSequenceValid);
    ExpectExportPixel(output.image, 11, 12, {255, 96, 96, 255}, 1);
    ExpectExportPixel(output.image, 18, 19, {255, 0, 0, 255});
    ExpectExportPixel(output.image, 24, 10, {0, 0, 0, 0});
    ExpectExportPixel(output.image, 90, 70, {0, 0, 0, 0});
    ExpectExportPixel(output.image, 10, 100, {0, 0, 0, 0});
  }
}

TGFX_TEST(LayerImageReaderTest, FullyClippedBackdropDoesNotRequireTileSnapshot) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto target = MakeExportSolid(Color::Red());
  target->setScrollRect(Rect::MakeWH(TestWidth, TestHeight));
  auto viewport = Layer::Make();
  viewport->setScrollRect(Rect::MakeWH(32, TestHeight));
  viewport->addChild(MakeExportGlass(6, 7, 10));
  auto hiddenGlass = MakeExportGlass(96, 7, 10);
  hiddenGlass->setPassThroughBackground(false);
  viewport->addChild(hiddenGlass);
  target->addChild(viewport);
  auto ancestor = Layer::Make();
  ancestor->setScrollRect(Rect::MakeWH(TestWidth, TestHeight));
  ancestor->addChild(target);
  auto displayList = MakeDisplayList(ancestor);

  auto output = ReadTiled(context, target, {32, 32});
  ASSERT_EQ(output.error, LayerImageErrorCode::None);
  ASSERT_TRUE(output.bandSequenceValid);
  ExpectExportPixel(output.image, 10, 10, {255, 96, 96, 255}, 1);
  ExpectExportPixel(output.image, 100, 10, {255, 0, 0, 255});
}

TGFX_TEST(LayerImageReaderTest, AncestorAlphaMaskClipsBackdropAndPlainTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto ancestor = Layer::Make();
  ancestor->setMatrix(Matrix::MakeTrans(24, 12));
  auto target = Layer::Make();
  target->setMatrix(Matrix::MakeTrans(6, 8));
  target->addChild(MakeExportSolid(Color::Red()));
  target->addChild(MakeExportGlass(52, 36, 16));
  target->addChild(MakeExportGlass(96, 72, 16));
  ancestor->addChild(target);
  auto displayList = MakeDisplayList(ancestor);
  auto mask = MakeExportSolid(Color::FromRGBA(255, 255, 255, 128));
  mask->setWidth(72);
  mask->setHeight(60);
  mask->setMatrix(Matrix::MakeTrans(38, 29));
  displayList->root()->addChild(mask);
  ancestor->setMask(mask);

  for (bool clearOpaque : {false, true}) {
    SCOPED_TRACE(clearOpaque);
    auto output =
        ReadTiled(context, target, {32, 32}, clearOpaque ? Color::Blue() : Color::Transparent());
    ASSERT_EQ(output.error, LayerImageErrorCode::None);
    ASSERT_FALSE(output.image.pixels.empty());
    EXPECT_TRUE(output.bandSequenceValid);
    auto backgroundBlue = clearOpaque ? 127 : 0;
    auto maskedAlpha = clearOpaque ? 255 : 128;
    const std::array<int, 4> outside = {0, 0, clearOpaque ? 255 : 0, clearOpaque ? 255 : 0};
    ExpectExportPixel(output.image, 12, 20, {128, 0, backgroundBlue, maskedAlpha}, 1);
    ExpectExportPixel(output.image, 62, 43, {128, 48, 48 + backgroundBlue, maskedAlpha}, 1);
    ExpectExportPixel(output.image, 84, 43, outside);
    ExpectExportPixel(output.image, 104, 80, outside);
    ExpectExportPixel(output.image, 10, 100, outside);
  }

  ancestor->setMaskType(LayerMaskType::AlphaInverted);
  auto output = ReadTiled(context, target, {32, 32});
  ASSERT_EQ(output.error, LayerImageErrorCode::None);
  ASSERT_FALSE(output.image.pixels.empty());
  EXPECT_TRUE(output.bandSequenceValid);
  ExpectExportPixel(output.image, 12, 20, {127, 0, 0, 127}, 1);
  ExpectExportPixel(output.image, 104, 80, {255, 96, 96, 255}, 1);
  ExpectExportPixel(output.image, 10, 100, {255, 0, 0, 255});
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
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << static_cast<int>(result.error);
  EXPECT_EQ(pixels[0], 255);
  EXPECT_NEAR(pixels[1], 96, 1);
  EXPECT_NEAR(pixels[2], 96, 1);
  EXPECT_EQ(pixels[3], 255);
}

TGFX_TEST(LayerImageReaderTest, LargeBackgroundBlurFitsDefaultTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr int OutputSize = 8;
  constexpr int Scale = 1024;
  constexpr int CaptureSize = OutputSize + 2 + 2 * Scale;
  if (context->gpu()->limits()->maxTextureDimension2D < CaptureSize) {
    GTEST_SKIP() << "The device cannot allocate the background capture";
  }

  auto root = Layer::Make();
  auto background = MakeExportSolid(Color::Red());
  background->setMatrix(Matrix::MakeTrans(-2, -2));
  root->addChild(background);
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setWidth(1.0f / 64);
  glass->setHeight(1.0f / 64);
  // The output-space blur needs 1024 pixels of background on each side.
  glass->setLayerStyles({BackgroundBlurStyle::Make(0.5f, 0.0f)});
  root->addChild(glass);
  auto displayList = MakeDisplayList(root);

  LayerImageRequest request = {};
  request.target = glass;
  request.outputSize = {OutputSize, OutputSize};
  request.targetLocalToOutput = Matrix::MakeScale(Scale);
  LayerImageReadOptions options = {};
  options.bandRows = OutputSize;
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_TRUE(reader != nullptr);
  auto info = reader->bandInfo();
  std::vector<uint8_t> pixels(info.byteSize());
  auto result = reader->readNextBand(context, Pixmap(info, pixels.data()), pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << static_cast<int>(result.error);
  EXPECT_TRUE(result.lastBand);
  // Only the backdrop can supply red beneath the white layer.
  const auto* center = pixels.data() + (OutputSize / 2) * (info.rowBytes() + info.bytesPerPixel());
  EXPECT_EQ(center[0], 255);
  EXPECT_NEAR(center[1], 96, 1);
  EXPECT_NEAR(center[2], 96, 1);
  EXPECT_EQ(center[3], 255);
}

TGFX_TEST(LayerImageReaderTest, OffscreenBlurDoesNotEnlargeLocalTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto root = BuildNestedBackdropScene();
  auto displayList = MakeDisplayList(root);
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
  auto output = ReadTiled(context, root, {64, 64});
  ASSERT_EQ(output.error, LayerImageErrorCode::None);
  EXPECT_EQ(CountDifferentPixels(reference, output.image, 1), 0);
}

TGFX_TEST(LayerImageReaderTest, WideBandRemainsCompleteAcrossBackgroundBatches) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr int Width = 1024;
  constexpr int Height = 14;
  constexpr int GlassX = 960;
  auto root = Layer::Make();
  auto stripe = MakeExportSolid(Color::Red());
  stripe->setWidth(8);
  stripe->setHeight(Height);
  stripe->setMatrix(Matrix::MakeTrans(GlassX + 4, 0));
  root->addChild(stripe);
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setWidth(24);
  glass->setHeight(Height);
  glass->setMatrix(Matrix::MakeTrans(GlassX, 0));
  glass->setLayerStyles({BackgroundBlurStyle::Make(1, 1)});
  root->addChild(glass);

  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {Width, Height};
  request.clearColor = Color::Black();
  PixelImage reference = {};
  // At the smaller limit the clear prefix takes over 64 cores of 14 pixels, so the glass
  // first requires a background capture after an earlier batch has already been drawn.
  for (const auto& tileLimit : {ISize{2048, 64}, ISize{16, 16}}) {
    LayerImageReadOptions options = {};
    options.bandRows = Height;
    options.tileSizeLimit = tileLimit;
    auto reader = LayerImageReader::Make(context, request, options);
    ASSERT_TRUE(reader != nullptr);
    auto info = reader->bandInfo();
    PixelImage output = {std::vector<uint8_t>(info.byteSize(), 0xcc), Width, Height,
                         info.rowBytes()};
    auto result =
        reader->readNextBand(context, Pixmap(info, output.pixels.data()), output.pixels.size());
    ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << static_cast<int>(result.error);
    EXPECT_EQ(result.firstRow, 0);
    EXPECT_EQ(result.rowCount, Height);
    EXPECT_TRUE(result.lastBand);
    if (reference.pixels.empty()) {
      const auto* center = output.pixels.data() + info.rowBytes() * 7 + (GlassX + 8) * 4;
      EXPECT_GT(center[0], center[1]);
      reference = std::move(output);
    } else {
      EXPECT_EQ(CountDifferentPixels(reference, output, 1), 0);
    }
  }
}

TGFX_TEST(LayerImageReaderTest, SubdividingATileDoesNotRedrawEarlierBatches) {
  class TrackingLayer final : public Layer {
   public:
    int leftmostTileDraws = 0;

   protected:
    bool drawLayer(const DrawArgs& args, Canvas* canvas, float alpha,
                   BlendMode blendMode) override {
      // Only the leftmost output tiles translate x by +1 for the sampling margin.
      if (canvas->getMatrix().getTranslateX() == 1.0f) {
        ++leftmostTileDraws;
      }
      return Layer::drawLayer(args, canvas, alpha, blendMode);
    }
  };

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->gpu()->limits()->maxTextureDimension2D < 2896) {
    GTEST_SKIP() << "The device cannot allocate the subdivided background capture";
  }
  constexpr int Width = 272;
  constexpr int Height = 64;
  constexpr int GlassX = 270;
  constexpr int GlassY = 62;
  auto root = std::make_shared<TrackingLayer>();
  auto glass = MakeExportSolid(Color::FromRGBA(255, 255, 255, 96));
  glass->setWidth(1);
  glass->setHeight(1);
  glass->setMatrix(Matrix::MakeTrans(GlassX, GlassY));
  // The 1440-pixel outset on each side forces a height split even at one pixel wide.
  glass->setLayerStyles({BackgroundBlurStyle::Make(720, 720)});
  root->addChild(glass);

  LayerImageRequest request = {};
  request.target = root;
  request.outputSize = {Width, Height};
  request.clearColor = Color::Red();
  LayerImageReadOptions options = {};
  options.bandRows = Height;
  // A full batch of 64 four-pixel-wide tiles fits before the glass at the end of the strip.
  options.tileSizeLimit = {6, Height + 2};
  auto reader = LayerImageReader::Make(context, request, options);
  ASSERT_NE(reader, nullptr);
  auto info = reader->bandInfo();
  PixelImage output = {std::vector<uint8_t>(info.byteSize(), 0xcc), Width, Height, info.rowBytes()};
  auto result =
      reader->readNextBand(context, Pixmap(info, output.pixels.data()), output.pixels.size());
  ASSERT_EQ(result.code, LayerImageReadCode::BandReady) << static_cast<int>(result.error);
  EXPECT_EQ(result.rowCount, Height);
  EXPECT_TRUE(result.lastBand);
  EXPECT_EQ(root->leftmostTileDraws, 1);
  for (int y = 0; y < Height; ++y) {
    for (int x = 0; x < Width; ++x) {
      if (x == GlassX && y == GlassY) {
        ExpectExportPixel(output, x, y, {255, 96, 96, 255}, 1);
      } else {
        ExpectExportPixel(output, x, y, {255, 0, 0, 255});
      }
    }
  }
}

}  // namespace tgfx
