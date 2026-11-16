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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/LayerRasterizer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {
std::vector<uint8_t> ReadRaster(Context* context, const std::shared_ptr<Layer>& layer,
                                LayerRasterizeOptions options, int tileSize) {
  options.maxTileSize = tileSize;
  std::vector<uint8_t> pixels(static_cast<size_t>(options.size.width) *
                              static_cast<size_t>(options.size.height) * 4U);
  int nextY = options.firstRow;
  auto result = LayerRasterizer::ReadRows(context, layer, options, [&](int y, const Pixmap& rows) {
    EXPECT_EQ(y, nextY);
    EXPECT_EQ(rows.width(), options.size.width);
    std::memcpy(pixels.data() + static_cast<size_t>(y) * rows.rowBytes(), rows.pixels(),
                static_cast<size_t>(rows.height()) * rows.rowBytes());
    nextY += rows.height();
    return true;
  });
  EXPECT_EQ(result, RasterizeResult::Success);
  EXPECT_EQ(nextY,
            options.rowCount == 0 ? options.size.height : options.firstRow + options.rowCount);
  return pixels;
}

void ExpectPixelsNear(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual,
                      int tolerance) {
  ASSERT_EQ(expected.size(), actual.size());
  int maxDifference = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    maxDifference = std::max(maxDifference, std::abs(int(expected[i]) - int(actual[i])));
  }
  EXPECT_LE(maxDifference, tolerance);
}

std::shared_ptr<SolidLayer> AddSolid(Layer* parent, float x, float y, float width, float height,
                                     const Color& color) {
  auto layer = SolidLayer::Make();
  layer->setWidth(width);
  layer->setHeight(height);
  layer->setColor(color);
  layer->setMatrix(Matrix::MakeTrans(x, y));
  parent->addChild(layer);
  return layer;
}
}  // namespace

TGFX_TEST(LayerRasterizerTest, FractionalScaleAndFilterAcrossBothTileAxes) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_NE(context, nullptr);
  DisplayList list;
  auto target = Layer::Make();
  list.root()->addChild(target);
  AddSolid(target.get(), -10, -10, 160, 160, Color::White());
  auto shape = ShapeLayer::Make();
  Path path;
  path.moveTo(3.25f, 11.5f);
  path.lineTo(120.5f, 35.75f);
  path.lineTo(53.5f, 111.25f);
  path.close();
  shape->setPath(path);
  shape->setFillStyle(ShapeStyle::Make(Color::Blue()));
  shape->setFilters({BlurFilter::Make(1, 2)});
  target->addChild(shape);
  LayerRasterizeOptions options;
  options.size = ISize::Make(157, 149);
  options.matrix = Matrix::MakeScale(1.125f, 1.125f);
  options.matrix.postTranslate(-0.25f, 0.5f);
  const auto tiled = ReadRaster(context, target, options, 64);

  auto reference = Surface::Make(context, options.size.width, options.size.height);
  ASSERT_NE(reference, nullptr);
  reference->getCanvas()->clear();
  reference->getCanvas()->setMatrix(options.matrix);
  target->draw(reference->getCanvas());
  auto info = ImageInfo::Make(options.size.width, options.size.height, ColorType::RGBA_8888,
                              AlphaType::Premultiplied);
  std::vector<uint8_t> expected(info.byteSize());
  ASSERT_TRUE(reference->readPixels(info, expected.data()));
  ExpectPixelsNear(expected, tiled, 2);
}

TGFX_TEST(LayerRasterizerTest, BackdropFromEarlierSiblingAcrossTileSeams) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_NE(context, nullptr);
  DisplayList list;
  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(17.5f, 23.25f));
  list.root()->addChild(parent);
  AddSolid(parent.get(), -20, -20, 230, 230, Color::White());
  for (int x = -10; x < 180; x += 11) {
    AddSolid(parent.get(), static_cast<float>(x), -10, 4, 200, Color::Blue());
  }
  auto target = AddSolid(parent.get(), 0, 0, 160, 180, Color::FromRGBA(255, 255, 255, 40));
  target->setLayerStyles({BackgroundBlurStyle::Make(2, 2)});
  LayerRasterizeOptions options;
  options.size = ISize::Make(160, 180);
  auto full = ReadRaster(context, target, options, 512);
  auto tiled = ReadRaster(context, target, options, 64);
  ExpectPixelsNear(full, tiled, 2);
  // A missing backdrop must not let two empty renders pass the seam comparison.
  ASSERT_FALSE(full.empty());
  EXPECT_GT(full[3], 0);
  EXPECT_LT(full[0], full[2]);

  // Reading an interior band must preserve both global coordinates and backdrop context.
  // Unrequested rows remain untouched, and the requested pixels match a complete render.
  options.firstRow = 37;
  options.rowCount = 109;
  auto band = ReadRaster(context, target, options, 64);
  const size_t rowBytes = static_cast<size_t>(options.size.width) * 4U;
  auto expectedBand = full;
  const size_t firstByte = static_cast<size_t>(options.firstRow) * rowBytes;
  const size_t endByte = static_cast<size_t>(options.firstRow + options.rowCount) * rowBytes;
  for (size_t i = 0; i < expectedBand.size(); ++i) {
    if (i < firstByte || i >= endByte) expectedBand[i] = 0;
  }
  ExpectPixelsNear(expectedBand, band, 2);
}

TGFX_TEST(LayerRasterizerTest, CancellationStopsBeforeNextBand) {
  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_NE(context, nullptr);
  DisplayList list;
  auto target = AddSolid(list.root(), 0, 0, 97, 201, Color::Blue());
  LayerRasterizeOptions options;
  options.size = ISize::Make(97, 201);
  options.maxTileSize = 64;
  int calls = 0;
  auto result = LayerRasterizer::ReadRows(context, target, options, [&](int y, const Pixmap& rows) {
    ++calls;
    EXPECT_EQ(y, 0);
    EXPECT_EQ(rows.width(), 97);
    return false;
  });
  EXPECT_EQ(result, RasterizeResult::Cancelled);
  EXPECT_EQ(calls, 1);
}
}  // namespace tgfx
