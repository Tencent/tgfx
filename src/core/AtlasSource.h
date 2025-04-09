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
#include "AtlasBuffer.h"
#include "DataSource.h"
#include "core/GlyphRunList.h"
#include "core/atlas/AtlasManager.h"

namespace tgfx {

class AtlasSource : public DataSource<AtlasBuffer> {
 public:
  using PageGlyphMap = std::map<uint32_t, std::vector<PlacementPtr<DrawGlyph>>>;

  AtlasSource(AtlasManager* atlasManager, std::shared_ptr<GlyphRunList> glyphRunList, const Matrix& viewMatrix,
              const Stroke* stroke);
  size_t getBufferCount() const {
    return drawGlyphs.size();
  }

  const std::map<MaskFormat, PageGlyphMap>& getDrawGlyphs() const {
    return drawGlyphs;
  }

  std::shared_ptr<AtlasBuffer> getData() const override;


  const Stroke* getStroke() const {
    return stroke;
  }

  std::vector<AtlasGeometryData> makeGeometries() const;

 private:
  AtlasManager* atlasManager;
  Matrix viewMatrix = Matrix::I();
  const Stroke* stroke;
  std::shared_ptr<GlyphRunList> glyphRunList;

  std::map<MaskFormat, PageGlyphMap> drawGlyphs;

  void computeAtlasLocator();
};
}  // namespace tgfx