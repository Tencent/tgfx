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

#include "Atlas.h"
#include <algorithm>
#include "core/PixelRef.h"
#include "core/images/TextureImage.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"
#include "gpu/tasks/TextureClearTask.h"
namespace tgfx {

static constexpr auto kPlotRecentlyUsedCount = 32;
static constexpr auto kAtlasRecentlyUsedCount = 128;

std::unique_ptr<Atlas> Atlas::Make(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width,
                                   int height, int plotWidth, int plotHeight,
                                   AtlasGenerationCounter* generationCounter) {
  return std::unique_ptr<Atlas>(new Atlas(proxyProvider, pixelFormat, width, height, plotWidth,
                                          plotHeight, generationCounter));
}

Atlas::Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height,
             int plotWidth, int plotHeight, AtlasGenerationCounter* generationCounter)
    : proxyProvider(proxyProvider), pixelFormat(pixelFormat), generationCounter(generationCounter),
      _atlasGeneration(generationCounter->next()), previousFlushToken(AtlasToken::InvalidToken()),
      flushesSinceLastUse(0),
      bytesPerPixel(static_cast<int>(PixelFormatBytesPerPixel(pixelFormat))), textureWidth(width),
      textureHeight(height), plotWidth(plotWidth), plotHeight(plotHeight) {
  int numPlotX = width / plotWidth;
  int numPlotY = height / plotHeight;
  DEBUG_ASSERT(plotWidth * numPlotX == textureWidth);
  DEBUG_ASSERT(plotHeight * numPlotY == textureHeight);
  numPlots = static_cast<uint32_t>(numPlotX * numPlotY);
}

Atlas::ErrorCode Atlas::addToAtlas(const AtlasCell& cell, AtlasToken nextFlushToken,
                                   AtlasLocator& atlasLocator) {
  for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
    if (addToPage(cell, pageIndex, atlasLocator)) {
      return ErrorCode::Succeeded;
    }
  }
  auto pageCount = pages.size();
  if (pageCount == PlotLocator::kMaxResidentPages) {
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
      Plot* plot = pages[pageIndex].plotList.back();
      DEBUG_ASSERT(plot != nullptr);
      if (plot->lastUseToken() < nextFlushToken) {
        evictionPlot(plot);
        if (plot->addRect(cell.width(), cell.height(), atlasLocator)) {
          cellLocators[cell.key()] = {cell.matrix(), atlasLocator};
          return ErrorCode::Succeeded;
        }
        return ErrorCode::Error;
      }
    }
  }
  activateNewPage();
  if (!addToPage(cell, pages.size() - 1, atlasLocator)) {
    return ErrorCode::Error;
  }
  return ErrorCode::Succeeded;
}

bool Atlas::addToPage(const AtlasCell& cell, size_t pageIndex, AtlasLocator& atlasLocator) {
  auto& page = pages[pageIndex];
  auto& plotList = page.plotList;
  for (auto& plot : plotList) {
    if (plot->addRect(cell.width(), cell.height(), atlasLocator)) {
      cellLocators[cell.key()] = {cell.matrix(), atlasLocator};
      return true;
    }
  }
  return false;
}

bool Atlas::activateNewPage() {
  int numPlotX = textureWidth / plotWidth;
  int numPlotY = textureHeight / plotHeight;
  Page page;
  page.plotArray =
      std::make_unique<std::unique_ptr<Plot>[]>(static_cast<size_t>(numPlotX * numPlotY));

  auto pageIndex = static_cast<uint32_t>(pages.size());
  auto currentPlot = page.plotArray.get();
  for (int y = numPlotY - 1, r = 0; y >= 0; --y, ++r) {
    for (int x = numPlotX - 1, c = 0; x >= 0; --x, ++c) {
      auto plotIndex = static_cast<uint32_t>(r * numPlotX + c);
      *currentPlot = std::make_unique<Plot>(pageIndex, plotIndex, generationCounter, x, y,
                                            plotWidth, plotHeight);
      page.plotList.push_front(currentPlot->get());
      ++currentPlot;
    }
  }
  pages.push_back(std::move(page));
  auto proxy = proxyProvider->createTextureProxy(UniqueKey::Make(), textureWidth, textureHeight,
                                                 pixelFormat);
  if (proxy == nullptr) {
    return false;
  }

  // In OpenGL, calling glTexImage2D with data == nullptr allocates texture memory
  // for the given width and height, but the contents are undefined.
  // This means OpenGL does not guarantee the texture will be zero-initialized.
  auto context = proxy->getContext();
  auto task =
      proxy->getContext()->drawingBuffer()->make<TextureClearTask>(UniqueKey::Make(), proxy);
  context->drawingManager()->addResourceTask(std::move(task));
  textureProxies.push_back(std::move(proxy));
  return true;
}

bool Atlas::getCellLocator(const BytesKey& cellKey, AtlasCellLocator& cellLocator) const {
  auto it = cellLocators.find(cellKey);
  if (it == cellLocators.end()) {
    return false;
  }
  cellLocator = it->second;
  const auto& atlasLocator = cellLocator.atlasLocator;
  auto page = atlasLocator.pageIndex();
  auto plot = atlasLocator.plotIndex();
  if (page >= pages.size() || plot >= numPlots) {
    return false;
  }
  auto plotGeneration = pages[page].plotArray[plot]->genID();
  auto locatorGeneration = atlasLocator.genID();
  return plotGeneration == locatorGeneration;
}

void Atlas::setLastUseToken(const PlotLocator& plotLocator, AtlasToken token) {
  auto plotIndex = plotLocator.plotIndex();
  DEBUG_ASSERT(plotIndex < numPlots);
  auto pageIndex = plotLocator.pageIndex();
  DEBUG_ASSERT(pageIndex < pages.size());
  auto plot = pages[pageIndex].plotArray[plotIndex].get();
  DEBUG_ASSERT(plot != nullptr);
  makeMRU(plot, pageIndex);
  plot->setLastUseToken(token);
}

void Atlas::makeMRU(Plot* plot, uint32_t pageIndex) {
  if (pages[pageIndex].plotList.front() == plot) {
    return;
  }
  auto& plotList = pages[pageIndex].plotList;
  if (auto iter = std::find(plotList.begin(), plotList.end(), plot); iter != plotList.end()) {
    plotList.splice(plotList.begin(), plotList, iter);
  }
}

void Atlas::evictionPlot(Plot* plot) {
  for (auto evictor : evictionCallbacks) {
    evictor->evict(plot->plotLocator());
  }
  auto pageIndex = plot->pageIndex();
  auto generation = plot->genID();
  auto plotIndex = plot->plotIndex();
  for (const auto& [key, cellLocator] : cellLocators) {
    auto& locator = cellLocator.atlasLocator;
    if (locator.pageIndex() == pageIndex && locator.plotIndex() == plotIndex &&
        locator.genID() == generation) {
      expiredKeys.insert(key);
    }
  }
  _atlasGeneration = generationCounter->next();
  plot->resetRects();
  evictionPlots[plot->pageIndex()].insert(plot);
}

void Atlas::deactivateLastPage() {
  DEBUG_ASSERT(!pages.empty());
  auto pageIndex = pages.size() - 1;
  pages.pop_back();
  textureProxies.pop_back();
  for (auto it = evictionPlots.begin(); it != evictionPlots.end();) {
    if (it->first == pageIndex) {
      it = evictionPlots.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& [key, cellLocator] : cellLocators) {
    if (cellLocator.atlasLocator.pageIndex() == pageIndex) {
      expiredKeys.insert(key);
    }
  }
}

void Atlas::compact(AtlasToken startTokenForNextFlush) {
  if (pages.empty()) {
    previousFlushToken = startTokenForNextFlush;
    cellLocators.clear();
    return;
  }

  bool atlasUsedThisFlush = false;
  for (auto& page : pages) {
    for (auto& plot : page.plotList) {
      //Reset number of flushes since used
      if (plot->lastUseToken().isInterval(previousFlushToken, startTokenForNextFlush)) {
        plot->resetFlushesSinceLastUsed();
        atlasUsedThisFlush = true;
      }
    }
  }
  if (atlasUsedThisFlush) {
    flushesSinceLastUse = 0;
  } else {
    ++flushesSinceLastUse;
  }

  //For all plots before last page. update number os flushes since used.
  // and check to see if there are any in the first pages that the last page can safely upload to
  if (atlasUsedThisFlush || flushesSinceLastUse > kAtlasRecentlyUsedCount) {
    std::vector<Plot*> availablePlots;
    auto lastPageIndex = pages.size() - 1;
    for (uint32_t pageIndex = 0; pageIndex < lastPageIndex; ++pageIndex) {
      for (auto& plot : pages[pageIndex].plotList) {
        if (!plot->lastUseToken().isInterval(previousFlushToken, startTokenForNextFlush)) {
          plot->increaseFlushesSinceLastUsed();
        }

        if (plot->flushesSinceLastUsed() > kPlotRecentlyUsedCount) {
          availablePlots.push_back(plot);
        }
      }  //end for plotLists
    }

    //Check last page and evict any that are no longer in use
    uint32_t usedPlots = 0;
    for (auto& plot : pages[lastPageIndex].plotList) {
      if (!plot->lastUseToken().isInterval(previousFlushToken, startTokenForNextFlush)) {
        plot->increaseFlushesSinceLastUsed();
      }

      if (plot->flushesSinceLastUsed() <= kPlotRecentlyUsedCount) {
        ++usedPlots;
      } else if (plot->lastUseToken() != AtlasToken::InvalidToken()) {
        evictionPlot(plot);
      }
    }

    if (!availablePlots.empty() && usedPlots > 0 && usedPlots < numPlots / 4) {
      for (auto& plot : pages[lastPageIndex].plotList) {
        if (plot->flushesSinceLastUsed() > kPlotRecentlyUsedCount) {
          continue;
        }

        if (!availablePlots.empty()) {
          evictionPlot(plot);
          evictionPlot(availablePlots.back());
          --usedPlots;
          availablePlots.pop_back();
        }
        if (usedPlots == 0 || availablePlots.empty()) {
          break;
        }
      }
    }  //end if

    if (!usedPlots) {
      deactivateLastPage();
      flushesSinceLastUse = 0;
    }
  }
  previousFlushToken = startTokenForNextFlush;
}

void Atlas::clearEvictionPlotTexture(Context* context) {
  if (evictionPlots.empty() || context == nullptr || textureProxies.empty()) {
    return;
  }
  auto pixelRef = PixelRef::Make(plotWidth, plotHeight, bytesPerPixel == 1);
  pixelRef->clear();
  auto pixels = pixelRef->lockPixels();
  if (pixels == nullptr) {
    return;
  }
  for (auto& [pageIndex, plots] : evictionPlots) {
    if (pageIndex >= textureProxies.size()) {
      continue;
    }
    auto textureProxy = textureProxies[pageIndex];
    if (textureProxy == nullptr) {
      continue;
    }
    auto texture = textureProxy->getTexture();
    if (texture == nullptr) {
      continue;
    }
    for (auto& plot : plots) {
      if (plot == nullptr) {
        continue;
      }
      auto rect = Rect::MakeXYWH(plot->pixelOffset().x, plot->pixelOffset().y,
                                 static_cast<float>(plotWidth), static_cast<float>(plotHeight));
      context->gpu()->writePixels(texture->getSampler(), rect, pixels, pixelRef->info().rowBytes());
    }
  }
  pixelRef->unlockPixels();
  evictionPlots.clear();
}

void Atlas::removeExpiredKeys() {
  constexpr size_t kMaxKeys = 20000;
  if (cellLocators.size() < kMaxKeys || expiredKeys.empty()) {
    return;
  }
  for (auto it = cellLocators.begin(); it != cellLocators.end();) {
    if (expiredKeys.find(it->first) != expiredKeys.end()) {
      it = cellLocators.erase(it);
    } else {
      ++it;
    }
  }
  expiredKeys.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////

AtlasConfig::AtlasConfig(int maxTextureSize) : maxTextureSize(kMaxTextureSize) {
  RGBADimensions.set(std::min(2048, maxTextureSize), std::min(1024, maxTextureSize));
}

ISize AtlasConfig::atlasDimensions(MaskFormat maskFormat) const {
  if (maskFormat == MaskFormat::A8) {
    return {std::min(2 * RGBADimensions.width, maxTextureSize),
            std::min(2 * RGBADimensions.height, maxTextureSize)};
  } else {
    return RGBADimensions;
  }
}

ISize AtlasConfig::plotDimensions(MaskFormat maskFormat) const {
  auto atlasDimensions = this->atlasDimensions(maskFormat);
  auto plogtWidth = atlasDimensions.width >= 2048 ? 512 : 256;
  auto plotHeight = atlasDimensions.height >= 2048 ? 512 : 256;
  return {plogtWidth, plotHeight};
}
}  // namespace tgfx