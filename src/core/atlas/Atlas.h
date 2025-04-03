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
#include <map>
#include <set>
#include "core/atlas/AtlasTypes.h"
#include "core/atlas/Glyph.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/ProxyProvider.h"
#include "gpu/proxies/TextureProxy.h"
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

  ErrorCode addToAtlasWithoutFillImage(PlacementPtr<Glyph> glyph);

  bool getGlyphLocator(const BytesKey& glyphKey, AtlasLocator& atlasLocator) const;

  bool fillGlyphImage(AtlasLocator& locator, void* image) const;

  const std::shared_ptr<TextureProxy>* getTextureProxy() const {
    return textureProxies;
  }

  uint64_t atlasGeneration() const {
    return _atlasGeneration;
  }

  bool hasGlyph(const BytesKey& glyphKey) const {
    return glyphs.find(glyphKey) != glyphs.end();
  }

  uint32_t numActivePages() const {
    return _numActivePages;
  }

  void compact(AtlasToken) {
  }

  uint32_t maxPages() const {
    return _maxPages;
  }

  void uploadToTexture(Context* context);

 private:
  Atlas(ProxyProvider* proxyProvider, PixelFormat pixelFormat, int width, int height, int plotWidth,
        int plotHeight, AtlasGenerationCounter* generationCounter, std::string_view label);

  bool createPages(AtlasGenerationCounter*);

  std::shared_ptr<TextureProxy>* getTextureProxies() {
    return textureProxies;
  }

  bool activateNewPage();
  bool addToPageWithoutFillImage(PlacementPtr<Glyph> glyph, int pageIndex);

  struct Page {
    std::unique_ptr<std::unique_ptr<Plot>[]> plotArray;
    PlotList plotList;
  };

  ProxyProvider* proxyProvider = nullptr;
  PixelFormat pixelFormat;
  std::shared_ptr<TextureProxy> textureProxies[PlotLocator::kMaxMultitexturePages];
  Page pages[PlotLocator::kMaxMultitexturePages];
  uint32_t _maxPages;
  uint32_t _numActivePages;
  uint64_t _atlasGeneration;
  uint32_t numPlots;
  int bytesPerPixel;
  int textureWidth;
  int textureHeight;
  int plotWidth;
  int plotHeight;
  const std::string label;

  std::map<uint32_t,std::set<Plot*>> dirtyPlots;

  BytesKeyMap<PlacementPtr<Glyph>> glyphs;
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