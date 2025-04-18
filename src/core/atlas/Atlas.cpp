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
#include "core/images/TextureImage.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"
namespace tgfx {

static constexpr auto kPlotRecentlyUsedCount = 32;
static constexpr auto kAtlasRecentlyUsedCount = 128;

std::unique_ptr<Atlas> Atlas::Make(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width,
                                   int height, int plotWidth, int plotHeight,
                                   AtlasGenerationCounter* generationCounter,
                                   std::string_view label) {
  return std::unique_ptr<Atlas>(new Atlas(proxyProvider, pixelFormat, width, height, plotWidth,
                                          plotHeight, generationCounter, label));
}

Atlas::Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height,
             int plotWidth, int plotHeight, AtlasGenerationCounter* generationCounter,
             std::string_view label)
    : proxyProvider(proxyProvider), pixelFormat(pixelFormat), generationCounter(generationCounter),
      previousFlushToken(AtlasToken::InvalidToken()), flushesSinceLastUse(0),
      _atlasGeneration(generationCounter->next()),
      bytesPerPixel(static_cast<int>(PixelFormatBytesPerPixel(pixelFormat))), textureWidth(width),
      textureHeight(height), plotWidth(plotWidth), plotHeight(plotHeight), label(label) {
  int numPlotX = width / plotWidth;
  int numPlotY = height / plotHeight;
  DEBUG_ASSERT(numPlotX * numPlotY <= PlotLocator::kMaxPlots);
  DEBUG_ASSERT(plotWidth * numPlotX == textureWidth);
  DEBUG_ASSERT(plotHeight * numPlotY == textureHeight);
  numPlots = static_cast<uint32_t>(numPlotX * numPlotY);
}

Atlas::ErrorCode Atlas::addToAtlasWithoutFillImage(const Glyph& glyph, AtlasToken nextFlushToken,
                                                   AtlasLocator& atlasLocator) {
  for (auto pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
    if (addToPageWithoutFillImage(glyph, pageIndex, atlasLocator)) {
      return ErrorCode::Succeeded;
    }
  }

  auto pageCount = pages.size();
  if (pageCount == PlotLocator::kMaxResidentPages) {
    for (auto pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
      Plot* plot = pages[pageIndex].plotList.back();
      DEBUG_ASSERT(plot != nullptr);
      if (plot->lastUseToken() < nextFlushToken) {
        evictionPlot(plot);
        if (plot->addRect(glyph.width(), glyph.height(), atlasLocator)) {
          locatorMap[glyph.key()] = atlasLocator;
          return ErrorCode::Succeeded;
        }
        return ErrorCode::Error;
      }
    }
  }

  activateNewPage();

  if (!addToPageWithoutFillImage(glyph, static_cast<int>(pages.size() - 1), atlasLocator)) {
    return ErrorCode::Error;
  }

  return ErrorCode::Succeeded;
}

bool Atlas::addToPageWithoutFillImage(const Glyph& glyph, int pageIndex,
                                      AtlasLocator& atlasLocator) {
  auto& page = pages[pageIndex];
  auto& plotList = page.plotList;
  for (auto& plot : plotList) {
    if (plot->addRect(glyph.width(), glyph.height(), atlasLocator)) {
      locatorMap[glyph.key()] = atlasLocator;
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

  auto pageIndex = static_cast<int>(pages.size());
  auto currentPlot = page.plotArray.get();
  for (int y = numPlotY - 1, r = 0; y >= 0; --y, ++r) {
    for (int x = numPlotX - 1, c = 0; x >= 0; --x, ++c) {
      int plotIndex = r * numPlotX + c;
      currentPlot->reset(new Plot(pageIndex, plotIndex, generationCounter, x, y, plotWidth,
                                  plotHeight, bytesPerPixel));
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
  textureProxies.push_back(std::move(proxy));
  images.push_back(TextureImage::Wrap(textureProxies.back()));
  return true;
}

bool Atlas::getGlyphLocator(const BytesKey& glyphKey, AtlasLocator& atlasLocator) const {
  auto it = locatorMap.find(glyphKey);
  if (it == locatorMap.end()) {
    return false;
  }
  atlasLocator = it->second;
  return true;
}

bool Atlas::fillGlyphImage(AtlasLocator& locator, void* image) const {
  if (image == nullptr) {
    return false;
  }
  auto plotIndex = locator.plotIndex();
  auto pageIndex = locator.pageIndex();

  if (pageIndex >= maxPages()) {
    return false;
  }
  auto& page = pages[pageIndex];
  if (plotIndex >= page.plotList.size()) {
    return false;
  }
  auto plot = page.plotArray[plotIndex].get();
  if (plot->plotIndex() == static_cast<int>(plotIndex)) {
    auto width = static_cast<int>(locator.getLocation().width());
    auto height = static_cast<int>(locator.getLocation().height());
    plot->addSubImage(width, height, image, locator);
  } else {
    LOGE("error");
  }

  return true;
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
  _atlasGeneration = generationCounter->next();
  plot->resetRects();
  evictionPlots[plot->pageIndex()].insert(plot);
}

void Atlas::deactivateLastPage() {
  DEBUG_ASSERT(!pages.empty());
  pages.pop_back();
  textureProxies.pop_back();
  images.pop_back();
}

bool Atlas::hasGlyph(const BytesKey& glyphKey) const {
  auto iter = locatorMap.find(glyphKey);
  if (iter == locatorMap.end()) {
    return false;
  }
  auto& Locator = iter->second;
  auto pageIndex = iter->second.pageIndex();
  auto plotIndex = iter->second.plotIndex();

  if (pageIndex >= pages.size() || plotIndex >= numPlots) {
    return false;
  }
  auto locatorGeneration = iter->second.genID();
  auto plotGeneration = pages[pageIndex].plotArray[plotIndex]->genID();
  return plotGeneration == locatorGeneration;
}

void Atlas::compact(AtlasToken startTokenForNextFlush) {
  if (pages.empty()) {
    previousFlushToken = startTokenForNextFlush;
    locatorMap.clear();
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
    for (auto pageIndex = 0; pageIndex < lastPageIndex; ++pageIndex) {
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
    unsigned usedPlots = 0;
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
  if (evictionPlots.empty() || context == nullptr) {
    return;
  }

  auto gpu = context->gpu();
  if (gpu == nullptr) {
    return;
  }

  auto size = plotWidth * plotHeight * bytesPerPixel;
  auto data = new (std::nothrow) uint8_t[size];
  if (data == nullptr) {
    return;
  }
  memset(data, 0, size);
  for (auto& [pageIndex, plots] : evictionPlots) {
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
      auto& plotLocator = plot->plotLocator();
      auto rect = Rect::MakeXYWH(plot->pixelOffset().x, plot->pixelOffset().y, 1.f * plotWidth,
                                 1.f * plotHeight);
      gpu->writePixels(texture->getSampler(), rect, data, plotWidth * bytesPerPixel);
    }
  }
  delete[] data;
  evictionPlots.clear();
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
  if (maskFormat == MaskFormat::A8) {
    auto atlasDimensions = this->atlasDimensions(maskFormat);
    auto plogtWidth = atlasDimensions.width >= 2048 ? 512 : 256;
    auto plotHeight = atlasDimensions.height >= 2048 ? 512 : 256;
    return {plogtWidth, plotHeight};
  } else {
    return {256, 256};
  }
}

}  // namespace tgfx