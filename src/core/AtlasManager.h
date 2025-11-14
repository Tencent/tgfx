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

#pragma once

#include <memory>
#include "Atlas.h"
#include "AtlasTypes.h"

namespace tgfx {
class AtlasManager : public AtlasGenerationCounter {
 public:
  explicit AtlasManager(Context* context);

  // This function must be called first, before other functions which use the atlas.
  // if it returns empty, the client must not try to use other functions.
  const std::vector<std::shared_ptr<TextureProxy>>& getTextureProxies(MaskFormat maskFormat);

  bool getCellLocator(MaskFormat, const BytesKey& key, AtlasCellLocator& locator) const;

  bool addCellToAtlas(const AtlasCell& cell, AtlasToken nextFlushToken, AtlasLocator&) const;

  void setPlotUseToken(PlotUseUpdater&, const PlotLocator&, MaskFormat, AtlasToken) const;

  void preFlush();

  void postFlush();

  AtlasToken nextFlushToken() const;

  // Resets atlas, does not destroy the underlying texture.
  void reset();

  // Releases all atlas resources,
  // including their underlying textures.
  void freeAll();

 private:
  bool initAtlas(MaskFormat format);

  Atlas* getAtlas(MaskFormat format) const;

  Context* context = nullptr;
  std::unique_ptr<Atlas> atlases[MaskFormatCount];
  AtlasTokenTracker atlasTokenTracker = {};
};
}  // namespace tgfx
