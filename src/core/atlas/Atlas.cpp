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
#include "core/utils/PixelFormatUtil.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"
namespace tgfx {

std::unique_ptr<Atlas> Atlas::Make(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width,
                                   int height, int plotWidth, int plotHeight,
                                   AtlasGenerationCounter* generationCounter,
                                   std::string_view label) {
  auto atlas = std::unique_ptr<Atlas>(new Atlas(proxyProvider, pixelFormat, width, height,
                                                plotWidth, plotHeight, generationCounter, label));
  if (!atlas->createPages(generationCounter)) {
    return nullptr;
  }
  return atlas;
}

Atlas::Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height,
             int plotWidth, int plotHeight, AtlasGenerationCounter* generationCounter,
             std::string_view label)
    : proxyProvider(proxyProvider), pixelFormat(pixelFormat),
      _maxPages(PlotLocator::kMaxMultitexturePages), _numActivePages(0),
      _atlasGeneration(generationCounter->next()),
      bytesPerPixel(static_cast<int>(PixelFormatBytesPerPixel(pixelFormat))), textureWidth(width),
      textureHeight(height), plotWidth(plotWidth), plotHeight(plotHeight), label(label) {
  int numPlotX = width / plotWidth;
  int numPlotY = height / plotHeight;
  DEBUG_ASSERT(numPlotX * numPlotY <= PlotLocator::kMaxPlot);
  DEBUG_ASSERT(plotWidth * numPlotX == textureWidth);
  DEBUG_ASSERT(plotHeight * numPlotY == textureHeight);
  numPlots = static_cast<uint32_t>(numPlotX * numPlotY);
}

bool Atlas::createPages(AtlasGenerationCounter* generationCounter) {
  if (generationCounter == nullptr) {
    return false;
  }

  int numPlotX = textureWidth / plotWidth;
  int numPlotY = textureHeight / plotHeight;
  for (uint32_t i = 0; i < _maxPages; i++) {
    pages[i].plotArray =
        std::make_unique<std::unique_ptr<Plot>[]>(static_cast<size_t>(numPlotX * numPlotY));

    auto currentPlot = pages[i].plotArray.get();
    for (int y = numPlotY - 1, r = 0; y >= 0; --y, ++r) {
      for (int x = numPlotX - 1, c = 0; x >= 0; --x, ++c) {
        int plotIndex = r * numPlotX + c;
        currentPlot->reset(new Plot(static_cast<int>(i), plotIndex, generationCounter, x, y,
                                    plotWidth, plotHeight, bytesPerPixel));
        pages[i].plotList.push_front(currentPlot->get());
        ++currentPlot;
      }
    }
  }
  return true;
}

Atlas::ErrorCode Atlas::addToAtlasWithoutFillImage(PlacementPtr<Glyph> glyph) {
  if (_numActivePages == maxPages()) {
    return ErrorCode::TryAgain;
  }

  for (auto pageIndex = 0; pageIndex < static_cast<int>(_numActivePages); ++pageIndex) {
    if (addToPageWithoutFillImage(std::move(glyph), pageIndex)) {
      return ErrorCode::Succeeded;
    }
  }

  if (_numActivePages == _maxPages) {
    return ErrorCode::TryAgain;
  }

  activateNewPage();

  if (!addToPageWithoutFillImage(std::move(glyph), static_cast<int>(_numActivePages - 1))) {
    return ErrorCode::Error;
  }

  return ErrorCode::Succeeded;
}

bool Atlas::addToPageWithoutFillImage(PlacementPtr<Glyph> glyph, int pageIndex) {
  auto& page = pages[pageIndex];
  auto& plotList = page.plotList;
  for (auto& plot : plotList) {
    AtlasLocator atlasLocator;
    if (plot->addRect(glyph->width(), glyph->height(), atlasLocator)) {
      glyph->setAtlasLocator(atlasLocator);
      glyphs[glyph->key()] = std::move(glyph);
      dirtyPlots[atlasLocator.pageIndex()].insert(plot);
      return true;
    }
  }
  return false;
}

bool Atlas::activateNewPage() {
  DEBUG_ASSERT(_numActivePages < _maxPages);
  auto proxy = proxyProvider->createTextureProxy(UniqueKey::Make(), textureWidth, textureHeight,
                                                 pixelFormat);
  if (proxy == nullptr) {
    return false;
  }
  textureProxies[_numActivePages] = std::move(proxy);
  ++_numActivePages;
  return true;
}

bool Atlas::getGlyphLocator(const BytesKey& glyphKey, AtlasLocator& atlasLocator) const {
  auto it = glyphs.find(glyphKey);
  if (it == glyphs.end()) {
    return false;
  }
  atlasLocator = it->second->locator();
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

void Atlas::uploadToTexture(Context* context) {
  if (dirtyPlots.empty() || context == nullptr) {
    return;
  }

  auto gpu = context->gpu();
  if (gpu == nullptr) {
    return;
  }

  for (auto& [pageIndex, plots] : dirtyPlots) {
    DEBUG_ASSERT(pageIndex < _maxPages);
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
      auto [pixels, rect, rowBytes] = plot->prepareForUpload();
      gpu->writePixels(texture->getSampler(), rect, pixels, rowBytes);
    }
  }

  dirtyPlots.clear();
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