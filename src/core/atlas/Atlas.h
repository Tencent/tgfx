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

#pragma once

#include <map>
#include <memory>
#include <set>
#include "core/atlas/AtlasTypes.h"
#include "core/atlas/Glyph.h"
#include "gpu/ProxyProvider.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Size.h"

namespace tgfx {
class Atlas {
 public:
  static std::unique_ptr<Atlas> Make(ProxyProvider* proxyProvider, PixelFormat format, int width,
                                     int height, int plotWidth, int plotHeight,
                                     AtlasGenerationCounter* generationCounter,
                                     std::string_view label);
  enum class ErrorCode { Error, Succeeded, TryAgain };

  ErrorCode addToAtlas(ResourceProvider*, int width, int height, const void* image,
                       AtlasLocator* atlasLocator);

  ErrorCode addToAtlasWithoutFillImage(const Glyph& glyph, AtlasToken nextFlushToken,
                                       AtlasLocator& atlasLocator);

  bool getGlyphLocator(const BytesKey& glyphKey, AtlasLocator& atlasLocator) const;

  bool fillGlyphImage(AtlasLocator& locator, void* image) const;

  const std::vector<std::shared_ptr<TextureProxy>>& getTextureProxies() const {
    return textureProxies;
  }

  const std::vector<std::shared_ptr<Image>>& getImages() const {
    return images;
  }

  uint64_t atlasGeneration() const {
    return _atlasGeneration;
  }

  bool hasGlyph(const BytesKey& glyphKey) const;

  void compact(AtlasToken);

  uint32_t maxPages() const {
    return static_cast<uint32_t>(pages.size());
  }

  //To ensure the atlas does not evict a given entry,the client must set the  use token
  void setLastUseToken(const PlotLocator& plotLocator, AtlasToken token) {
    auto plotIndex = plotLocator.plotIndex();
    DEBUG_ASSERT(plotIndex < numPlots);
    auto pageIndex = plotLocator.pageIndex();
    DEBUG_ASSERT(pageIndex < pages.size());
    auto plot = pages[pageIndex].plotArray[plotIndex].get();
    DEBUG_ASSERT(plot != nullptr);
    makeMRU(plot, pageIndex);
    plot->setLastUseToken(token);
  }

  void clearEvictionPlotTexture(Context* context);

 private:
  Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height, int plotWidth,
        int plotHeight, AtlasGenerationCounter* generationCounter, std::string_view label);

  void makeMRU(Plot* plot, uint32_t pageIndex);

  bool activateNewPage();
  bool addToPageWithoutFillImage(const Glyph& glyph, int pageIndex, AtlasLocator& atlasLocator);

  void evictionPlot(Plot* plot);
  void deactivateLastPage();

  struct Page {
    std::unique_ptr<std::unique_ptr<Plot>[]> plotArray;
    PlotList plotList;
  };

  ProxyProvider* proxyProvider = nullptr;
  PixelFormat pixelFormat;
  AtlasGenerationCounter* const generationCounter;
  std::vector<std::shared_ptr<TextureProxy>> textureProxies;
  std::vector<std::shared_ptr<Image>> images;
  std::vector<Page> pages;
  uint64_t _atlasGeneration;
  AtlasToken previousFlushToken;
  int flushesSinceLastUse;
  uint32_t numPlots;
  int bytesPerPixel;
  int textureWidth;
  int textureHeight;
  int plotWidth;
  int plotHeight;
  const std::string label;

  std::vector<PlotEvictionCallback*> evictionCallbacks;

  BytesKeyMap<AtlasLocator> locatorMap;
  std::map<uint32_t, std::set<Plot*>> evictionPlots;
};

class AtlasConfig {
 public:
  explicit AtlasConfig(int maxTextureSize);

  ISize atlasDimensions(MaskFormat maskFormat) const;

  ISize plotDimensions(MaskFormat maskFormat) const;

  void init(int maxTextureSize);

 private:
  inline static constexpr int kMaxTextureSize = 2048;
  ISize RGBADimensions = ISize::Make(0, 0);
  int maxTextureSize;
};
}  //namespace tgfx