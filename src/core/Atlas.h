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

#include <memory>
#include <set>
#include "core/AtlasCell.h"
#include "core/AtlasTypes.h"
#include "gpu/ProxyProvider.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Size.h"

namespace tgfx {
class Atlas {
 public:
  static constexpr int MaxCellSize = 256;

  static std::unique_ptr<Atlas> Make(ProxyProvider* proxyProvider, PixelFormat format, int width,
                                     int height, int plotWidth, int plotHeight,
                                     AtlasGenerationCounter* generationCounter);

  bool addToAtlas(const AtlasCell& cell, AtlasToken nextFlushToken, AtlasLocator& atlasLocator);

  bool getCellLocator(const BytesKey& cellKey, AtlasCellLocator& cellLocator) const;

  const std::vector<std::shared_ptr<TextureProxy>>& getTextureProxies() const {
    return textureProxies;
  }

  void compact(AtlasToken);

  //To ensure the atlas does not evict a given entry, the client must set the use token
  void setLastUseToken(const PlotLocator& plotLocator, AtlasToken token);

  void removeExpiredKeys();

 private:
  Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height, int plotWidth,
        int plotHeight, AtlasGenerationCounter* generationCounter);

  void makeMRU(Plot* plot, uint32_t pageIndex);

  bool activateNewPage();

  bool addToPage(const AtlasCell& cell, size_t pageIndex, AtlasLocator& atlasLocator);

  void evictionPlot(Plot* plot);

  void deactivateLastPage();

  struct Page {
    std::unique_ptr<std::unique_ptr<Plot>[]> plotArray;
    PlotList plotList;
  };

  ProxyProvider* proxyProvider = nullptr;
  PixelFormat pixelFormat = PixelFormat::Unknown;
  AtlasGenerationCounter* const generationCounter;
  std::vector<std::shared_ptr<TextureProxy>> textureProxies = {};
  std::vector<Page> pages = {};
  AtlasToken previousFlushToken = AtlasToken::InvalidToken();
  uint32_t flushesSinceLastUse = 0;
  uint32_t numPlots = 0;
  int textureWidth = 2028;
  int textureHeight = 2048;
  int plotWidth = 512;
  int plotHeight = 512;

  BytesKeyMap<AtlasCellLocator> cellLocators;
  std::set<BytesKey> expiredKeys;
};

class AtlasConfig {
 public:
  explicit AtlasConfig(int maxTextureSize);

  ISize atlasDimensions(MaskFormat maskFormat) const;

  ISize plotDimensions(MaskFormat maskFormat) const;

 private:
  static constexpr int MaxTextureSize = 2048;
  ISize RGBADimensions = {MaxTextureSize, MaxTextureSize};
};
}  //namespace tgfx
