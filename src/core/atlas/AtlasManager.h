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
#include "Atlas.h"
#include "AtlasTypes.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/PlacementBuffer.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
class AtlasManager : public AtlasGenerationCounter {
 public:
  explicit AtlasManager(Context* context);

  ~AtlasManager();

  const std::shared_ptr<TextureProxy>* getTextureProxy(MaskFormat maskFormat,
                                                       unsigned int* numActiveProxies);

  void releaseAll();
  bool hasGlyph(MaskFormat, const BytesKey& key) const;

  bool getGlyphLocator(MaskFormat, const BytesKey& key, AtlasLocator& locator) const;

  Atlas::ErrorCode addGlyphToAtlasWithoutFillImage(PlacementPtr<Glyph> glyph) const;

  bool fillGlyphImage(MaskFormat, AtlasLocator& locator, void* image) const;

  //Atlas::ErrorCode addGlyphToAtlas(const Glyph& glyph, int srcPadding, ResourceProvider);

  Context* getContext() const {
    return context;
  }

  PlacementBuffer* glyphCacheBuffer() const {
    return _glyphCacheBuffer;
  }

  void uploadToTexture();

 private:
  bool initAtlas(MaskFormat format);

  Atlas* getAtlas(MaskFormat format) const;
  Context* context = nullptr;
  std::unique_ptr<Atlas> atlases[kMaskFormatCount];
  PlacementBuffer* _glyphCacheBuffer = nullptr;
};
}  // namespace tgfx