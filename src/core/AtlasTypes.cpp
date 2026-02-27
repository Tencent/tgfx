/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
bool PlotUseUpdater::add(const PlotLocator& plotLocator) {
  auto pageIndex = plotLocator.pageIndex();
  auto plotIndex = plotLocator.plotIndex();
  if (find(pageIndex, plotIndex)) {
    return false;
  }
  set(pageIndex, plotIndex);
  return true;
}

bool PlotUseUpdater::find(uint32_t pageIndex, uint32_t plotIndex) const {
  DEBUG_ASSERT(plotIndex < PlotLocator::MaxPlots);
  if (pageIndex >= plotAlreadyUpdated.size()) {
    return false;
  }
  return static_cast<bool>((plotAlreadyUpdated[pageIndex] >> plotIndex) & 1);
}

void PlotUseUpdater::set(uint32_t pageIndex, uint32_t plotIndex) {
  DEBUG_ASSERT(!find(pageIndex, plotIndex));
  if (pageIndex >= plotAlreadyUpdated.size()) {
    auto count = pageIndex - plotAlreadyUpdated.size() + 1;
    for (size_t i = 0; i < count; ++i) {
      plotAlreadyUpdated.emplace_back(0);
    }
  }
  plotAlreadyUpdated[pageIndex] |= (1 << plotIndex);
}

Plot::Plot(uint32_t pageIndex, uint32_t plotIndex, AtlasGenerationCounter* generationCounter,
           int offsetX, int offsetY, int width, int height)
    : generationCounter(generationCounter), _pageIndex(pageIndex), _plotIndex(plotIndex),
      _genID(generationCounter->next()),
      _pixelOffset(Point::Make(offsetX * width, offsetY * height)), rectPack(width, height),
      _plotLocator(pageIndex, plotIndex, _genID) {
}

bool Plot::addRect(int imageWidth, int imageHeight, AtlasLocator* atlasLocator) {
  auto widthWithPadding = imageWidth + 2 * CellPadding;
  auto heightWithPadding = imageHeight + 2 * CellPadding;
  Point location;
  if (!rectPack.addRect(widthWithPadding, heightWithPadding, location)) {
    return false;
  }

  auto rectX = static_cast<int>(location.x) + CellPadding;
  auto rectY = static_cast<int>(location.y) + CellPadding;
  auto rect = Rect::MakeXYWH(rectX, rectY, imageWidth, imageHeight);
  rect.offset(_pixelOffset.x, _pixelOffset.y);
  atlasLocator->updateRect(rect);
  atlasLocator->setPlotLocator(_plotLocator);
  return true;
}

void Plot::resetRects() {
  rectPack.reset();
  _genID = generationCounter->next();
  _plotLocator =
      PlotLocator(static_cast<uint32_t>(_pageIndex), static_cast<uint32_t>(_plotIndex), _genID);
  _lastUseToken = AtlasToken::InvalidToken();
}
}  // namespace tgfx
