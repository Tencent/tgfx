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

#include "AtlasManager.h"
#include <tgfx/core/Size.h>
#include "core/utils/PixelFormatUtil.h"

namespace tgfx {

static int MaskFromatToAtlasIndex(MaskFormat format) {
  return static_cast<int>(format);
}

AtlasManager::AtlasManager(Context* context) : context(context) {
  _glyphCacheBuffer = new PlacementBuffer(1 << 14);
}

AtlasManager::~AtlasManager() {
  delete _glyphCacheBuffer;
}

void AtlasManager::releaseAll() {
  for (int i = 0; i < kMaskFormatCount; i++) {
    atlases[i] = nullptr;
  }
}

const std::shared_ptr<TextureProxy>* AtlasManager::getTextureProxy(MaskFormat maskFormat,
                                                                   unsigned int* numActiveProxies) {
  if (this->initAtlas(maskFormat)) {
    *numActiveProxies = this->getAtlas(maskFormat)->numActivePages();
    return this->getAtlas(maskFormat)->getTextureProxy();
  }
  *numActiveProxies = 0;
  return nullptr;
}

// bool AtlasManager::hasGlyph(MaskFormat format, Glyph* glyph) {
//   return this->getAtlas(format)->hasGlyph(glyph);
// }

// Atlas::ErrorCode AtlasManager::addGlyphToAtlas(const Glyph& glyph, int srcPadding,
//                                                ResourceProvider) {
//   return Atlas::ErrorCode::Succeeded;
// }

bool AtlasManager::initAtlas(MaskFormat maskFormat) {
  int index = MaskFromatToAtlasIndex(maskFormat);
  AtlasConfig atlasConfig(context->caps()->maxTextureSize);
  if (atlases[index] == nullptr) {
    ISize atlasDimensions = atlasConfig.atlasDimensions(maskFormat);
    ISize plotDimensions = atlasConfig.plotDimensions(maskFormat);
    auto pixelFormat = MaskFormatToPixelFormat(maskFormat);
    atlases[index] = Atlas::Make(context->proxyProvider(), pixelFormat, atlasDimensions.width,
                                 atlasDimensions.height, plotDimensions.width,
                                 plotDimensions.height, this, "TextAtlas");
    if (atlases[index] == nullptr) {
      return false;
    }
  }
  return true;
}

Atlas* AtlasManager::getAtlas(MaskFormat maskFormat) const {
  auto atlasIndex = MaskFromatToAtlasIndex(maskFormat);
  DEBUG_ASSERT(atlases[atlasIndex])
  return atlases[atlasIndex].get();
}

bool AtlasManager::hasGlyph(MaskFormat maskFormat, const BytesKey& key) const {
  return this->getAtlas(maskFormat)->hasGlyph(key);
}

Atlas::ErrorCode AtlasManager::addGlyphToAtlasWithoutFillImage(PlacementPtr<Glyph> glyph) const {
  auto maskFormat = glyph->maskFormat();
  return getAtlas(maskFormat)->addToAtlasWithoutFillImage(std::move(glyph));
}

bool AtlasManager::getGlyphLocator(MaskFormat maskFormat, const BytesKey& key,
                                   AtlasLocator& locator) const {
  return this->getAtlas(maskFormat)->getGlyphLocator(key, locator);
}

bool AtlasManager::fillGlyphImage(MaskFormat maskFormat, AtlasLocator& locator, void* image) const {
  return this->getAtlas(maskFormat)->fillGlyphImage(locator, image);
}

void AtlasManager::uploadToTexture(){
  for (int i = 0; i < kMaskFormatCount; i++) {
    if (atlases[i] != nullptr) {
      atlases[i]->uploadToTexture(context);
    }
  }
}


}  // namespace tgfx
