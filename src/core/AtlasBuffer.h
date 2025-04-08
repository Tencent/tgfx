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

#include <vector>
#include "core/atlas/Glyph.h"

namespace tgfx {

struct AtlasGeometryData {
  MaskFormat maskFormat;
  uint32_t pageIndex;
  std::shared_ptr<Data> vertices;
  std::shared_ptr<Data> indices;
};

class AtlasBuffer {
 public:
  static std::shared_ptr<AtlasBuffer> MakeFrom(std::vector<AtlasGeometryData>);

  const std::vector<AtlasGeometryData>& geometryData() const {
    return _geometryData;
  }

 private:
  explicit AtlasBuffer(std::vector<AtlasGeometryData> geometryData)
      : _geometryData(std::move(geometryData)) {
  }
  std::vector<AtlasGeometryData> _geometryData;
};

}  // namespace tgfx