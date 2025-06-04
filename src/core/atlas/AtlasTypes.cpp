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
#include "AtlasTypes.h"

namespace tgfx {
Plot::Plot(uint32_t pageIndex, uint32_t plotIndex, AtlasGenerationCounter* generationCounter,
           int offsetX, int offsetY, int width, int height, int bytesPerPixel)
    : generationCounter(generationCounter), _pageIndex(pageIndex), _plotIndex(plotIndex),
      _genID(generationCounter->next()), width(width), height(height),
      _pixelOffset(Point::Make(offsetX * width, offsetY * height)), bytesPerPixel(bytesPerPixel),
      rectPack(width, height), _plotLocator(pageIndex, plotIndex, _genID) {
  dirtyRect.setEmpty();
  cachedRect.setEmpty();
}

Plot::~Plot() {
  delete[] data;
}

bool Plot::addSubImage(int imageWidth, int imageHeight, const void* image,
                       AtlasLocator& atlasLocator) const {
  if (data == nullptr) {
    return false;
  }
  DEBUG_ASSERT(imageWidth < width && imageHeight < height);
  Point location = {atlasLocator.getLocation().left, atlasLocator.getLocation().top};
  auto rect = Rect::MakeXYWH(location.x, location.y, (float)imageWidth, (float)imageHeight);

  auto left = static_cast<int>(rect.left);
  auto top = static_cast<int>(rect.top);
  auto rowBytes = static_cast<size_t>(imageWidth * bytesPerPixel);
  auto imagePtr = static_cast<const uint8_t*>(image);
  auto dataPtr = data;
  dataPtr += bytesPerPixel * width * top;
  dataPtr += bytesPerPixel * left;

  for (auto i = 0; i < imageHeight; ++i) {
    memcpy(dataPtr, imagePtr, rowBytes);
    dataPtr += bytesPerPixel * width;
    imagePtr += rowBytes;
  }

  return true;
}

bool Plot::addRect(int imageWidth, int imageHeight, AtlasLocator& atlasLocator) {
  DEBUG_ASSERT(imageWidth + 2 * padding < width && imageHeight + 2 * padding < height);
  auto widthWithPadding = imageWidth + 2 * padding;
  auto heightWithPadding = imageHeight + 2 * padding;
  Point location;
  if (!rectPack.addRect(widthWithPadding, heightWithPadding, location)) {
    return false;
  }

  if (data == nullptr) {
    auto size = static_cast<size_t>(this->width * this->height * bytesPerPixel);
    data = new (std::nothrow) uint8_t[size];
    if (data == nullptr) {
      return false;
    }
    memset(data, 0, size);
  }

  auto rectX = static_cast<int>(location.x) + padding;
  auto rectY = static_cast<int>(location.y) + padding;
  auto rect = Rect::MakeXYWH(rectX, rectY, imageWidth, imageHeight);
  rect.offset(_pixelOffset.x, _pixelOffset.y);
  atlasLocator.updateRect(rect);
  atlasLocator.setPlotLocator(_plotLocator);
  dirtyRect.join(rect);
  return true;
}

void Plot::resetRects() {
  rectPack.reset();
  _genID = generationCounter->next();
  _plotLocator =
      PlotLocator(static_cast<uint32_t>(_pageIndex), static_cast<uint32_t>(_plotIndex), _genID);
  _lastUseToken = AtlasToken::InvalidToken();
  if (data) {
    memset(data, 0, static_cast<size_t>(bytesPerPixel * width * height));
  }
  dirtyRect.setEmpty();
  cachedRect.setEmpty();
}

std::tuple<const void*, Rect, size_t> Plot::prepareForUpload() {
  if (data == nullptr || dirtyRect.isEmpty()) {
    return {nullptr, Rect::MakeEmpty(), 0};
  }
  auto rect = dirtyRect;
  dirtyRect.setEmpty();
  auto rowBytes = static_cast<int>(width * bytesPerPixel);
  auto dataPtr = data;
  auto top = static_cast<int>(rect.top);
  auto left = static_cast<int>(rect.left);
  dataPtr += rowBytes * top;
  dataPtr += bytesPerPixel * left;
  return {dataPtr, rect, rowBytes};
}

}  // namespace tgfx