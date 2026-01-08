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

#include "AtlasManager.h"
#include "core/utils/PixelFormatUtil.h"
#include "tgfx/core/Size.h"

namespace tgfx {
static int MaskFormatToAtlasIndex(MaskFormat format) {
  return static_cast<int>(format);
}

AtlasManager::AtlasManager(Context* context) : context(context) {
}

const std::vector<std::shared_ptr<TextureProxy>>& AtlasManager::getTextureProxies(
    MaskFormat maskFormat) {
  if (this->initAtlas(maskFormat)) {
    return this->getAtlas(maskFormat)->getTextureProxies();
  }
  static std::vector<std::shared_ptr<TextureProxy>> kEmpty;
  return kEmpty;
}

bool AtlasManager::initAtlas(MaskFormat maskFormat) {
  auto index = MaskFormatToAtlasIndex(maskFormat);
  if (atlases[index] != nullptr) {
    return true;
  }
  AtlasConfig atlasConfig(context->gpu()->limits()->maxTextureDimension2D);
  ISize atlasDimensions = atlasConfig.atlasDimensions(maskFormat);
  ISize plotDimensions = AtlasConfig::PlotDimensions();
  auto pixelFormat = MaskFormatToPixelFormat(maskFormat);
  atlases[index] =
      Atlas::Make(context->proxyProvider(), pixelFormat, atlasDimensions.width,
                  atlasDimensions.height, plotDimensions.width, plotDimensions.height, this);
  if (atlases[index] == nullptr) {
    return false;
  }
  return true;
}

Atlas* AtlasManager::getAtlas(MaskFormat maskFormat) const {
  auto atlasIndex = MaskFormatToAtlasIndex(maskFormat);
  DEBUG_ASSERT(atlases[atlasIndex] != nullptr)
  return atlases[atlasIndex].get();
}

bool AtlasManager::addCellToAtlas(const AtlasCell& cell, AtlasToken nextFlushToken,
                                  AtlasLocator* atlasLocator) const {
  return getAtlas(cell.maskFormat)->addToAtlas(cell, nextFlushToken, atlasLocator);
}

bool AtlasManager::hasGlyph(MaskFormat maskFormat, const AtlasGlyph* glyph) const {
  return this->getAtlas(maskFormat)->hasID(glyph->atlasLocator.plotLocator());
}

void AtlasManager::setPlotUseToken(PlotUseUpdater& plotUseUpdater, const PlotLocator& plotLocator,
                                   MaskFormat maskFormat, AtlasToken useToken) const {
  if (plotUseUpdater.add(plotLocator)) {
    getAtlas(maskFormat)->setLastUseToken(plotLocator, useToken);
  }
}

void AtlasManager::postFlush() {
  atlasTokenTracker.advanceToken();
  for (const auto& atlas : atlases) {
    if (atlas) {
      atlas->compact(atlasTokenTracker.nextToken());
    }
  }
}

AtlasToken AtlasManager::nextFlushToken() const {
  return atlasTokenTracker.nextToken();
}

void AtlasManager::releaseAll() {
  for (auto& atlas : atlases) {
    atlas = nullptr;
  }
}

}  // namespace tgfx
