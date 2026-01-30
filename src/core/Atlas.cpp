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

#include "Atlas.h"
#include "core/PixelRef.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
namespace tgfx {

static constexpr uint32_t PlotRecentlyUsedCount = 32;
static constexpr uint32_t AtlasRecentlyUsedCount = 128;

std::unique_ptr<Atlas> Atlas::Make(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width,
                                   int height, int plotWidth, int plotHeight,
                                   AtlasGenerationCounter* generationCounter) {
  return std::unique_ptr<Atlas>(new Atlas(proxyProvider, pixelFormat, width, height, plotWidth,
                                          plotHeight, generationCounter));
}

Atlas::Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height,
             int plotWidth, int plotHeight, AtlasGenerationCounter* generationCounter)
    : proxyProvider(proxyProvider), pixelFormat(pixelFormat), generationCounter(generationCounter),
      textureWidth(width), textureHeight(height), plotWidth(plotWidth), plotHeight(plotHeight) {
  int numPlotX = width / plotWidth;
  int numPlotY = height / plotHeight;
  DEBUG_ASSERT(plotWidth * numPlotX == textureWidth);
  DEBUG_ASSERT(plotHeight * numPlotY == textureHeight);
  numPlots = static_cast<uint32_t>(numPlotX * numPlotY);
}

bool Atlas::addToAtlas(const AtlasCell& cell, AtlasToken nextFlushToken,
                       AtlasLocator* atlasLocator) {
  for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
    if (addToPage(cell, pageIndex, atlasLocator)) {
      return true;
    }
  }
  auto pageCount = pages.size();
  if (pageCount == PlotLocator::MaxResidentPages) {
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
      Plot* plot = pages[pageIndex].plotList.back();
      DEBUG_ASSERT(plot != nullptr);
      if (plot->lastUseToken() < nextFlushToken) {
        evictionPlot(plot);
        if (plot->addRect(cell.width, cell.height, atlasLocator)) {
          return true;
        }
        return false;
      }
    }
  }
  activateNewPage();
  if (!addToPage(cell, pages.size() - 1, atlasLocator)) {
    return false;
  }
  return true;
}

bool Atlas::addToPage(const AtlasCell& cell, size_t pageIndex, AtlasLocator* atlasLocator) {
  auto& page = pages[pageIndex];
  auto& plotList = page.plotList;
  for (auto& plot : plotList) {
    if (plot->addRect(cell.width, cell.height, atlasLocator)) {
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
  std::shared_ptr<TextureProxy> proxy = nullptr;
  auto hardwareBuffer =
      HardwareBufferAllocate(textureWidth, textureHeight, pixelFormat == PixelFormat::ALPHA_8);
  if (hardwareBuffer != nullptr) {
    proxy = proxyProvider->createTextureProxy(hardwareBuffer);
    HardwareBufferRelease(hardwareBuffer);
  }
  if (proxy == nullptr) {
    proxy = proxyProvider->createTextureProxy({}, textureWidth, textureHeight, pixelFormat);
  }
  if (proxy == nullptr) {
    return false;
  }
  textureProxies.push_back(std::move(proxy));
  return true;
}

bool Atlas::hasCell(const PlotLocator& plotLocator) const {
  if (!plotLocator.isValid()) {
    return false;
  }
  auto page = plotLocator.pageIndex();
  auto plot = plotLocator.plotIndex();
  if (page >= pages.size() || plot >= numPlots) {
    return false;
  }
  auto plotGeneration = pages[page].plotArray[plot]->genID();
  auto locatorGeneration = plotLocator.genID();
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
  plot->resetRects();
}

void Atlas::deactivateLastPage() {
  DEBUG_ASSERT(!pages.empty());
  pages.pop_back();
  textureProxies.pop_back();
}

void Atlas::compact(AtlasToken startTokenForNextFlush) {
  if (pages.empty()) {
    previousFlushToken = startTokenForNextFlush;
    return;
  }

  // For all plots, reset number of flushes if used this frame.
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

  // Compact if the atlas was used in the recently completed flush or
  // hasn't been used in a long time.
  if (atlasUsedThisFlush || flushesSinceLastUse > AtlasRecentlyUsedCount) {
    std::vector<Plot*> availablePlots;
    auto lastPageIndex = pages.size() - 1;
    for (uint32_t pageIndex = 0; pageIndex < lastPageIndex; ++pageIndex) {
      for (auto& plot : pages[pageIndex].plotList) {
        if (!plot->lastUseToken().isInterval(previousFlushToken, startTokenForNextFlush)) {
          plot->increaseFlushesSinceLastUsed();
        }
        if (plot->flushesSinceLastUsed() > PlotRecentlyUsedCount) {
          availablePlots.push_back(plot);
        }
      }
    }

    //Check last page and evict any that are no longer in use
    uint32_t usedPlots = 0;
    for (auto& plot : pages[lastPageIndex].plotList) {
      if (!plot->lastUseToken().isInterval(previousFlushToken, startTokenForNextFlush)) {
        plot->increaseFlushesSinceLastUsed();
      }
      if (plot->flushesSinceLastUsed() <= PlotRecentlyUsedCount) {
        ++usedPlots;
      } else if (plot->lastUseToken() != AtlasToken::InvalidToken()) {
        evictionPlot(plot);
      }
    }

    // Evict a plot from the last page, and the availablePlots is also reduced by one,
    // which is equivalent to moving a plot from the last page to a previous page
    if (!availablePlots.empty() && usedPlots > 0 && usedPlots < numPlots / 4) {
      for (auto& plot : pages[lastPageIndex].plotList) {
        if (plot->flushesSinceLastUsed() > PlotRecentlyUsedCount) {
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

    //All plot can be moved to previous page, so we can deactivate the last page
    if (usedPlots == 0) {
      deactivateLastPage();
      flushesSinceLastUse = 0;
    }
  }
  previousFlushToken = startTokenForNextFlush;
}

AtlasConfig::AtlasConfig(int maxTextureSize) {
  A8Dimensions.set(std::min(MaxAtlasSize, maxTextureSize), std::min(MaxAtlasSize, maxTextureSize));
}

ISize AtlasConfig::atlasDimensions(AtlasFormat atlasFormat) const {
  if (atlasFormat == AtlasFormat::A8) {
    return A8Dimensions;
  }
  return {A8Dimensions.width, A8Dimensions.height / 2};
}

ISize AtlasConfig::PlotDimensions() {
  static ISize dimensions{PlotSize, PlotSize};
  return dimensions;
}
}  // namespace tgfx
