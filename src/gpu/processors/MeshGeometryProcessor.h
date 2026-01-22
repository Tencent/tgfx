/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GeometryProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {

/**
 * Geometry processor for rendering mesh data with optional texture coordinates and vertex colors.
 */
class MeshGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<MeshGeometryProcessor> Make(BlockAllocator* allocator, bool hasTexCoords,
                                                  bool hasColors, PMColor color,
                                                  const Matrix& viewMatrix);

  std::string name() const override {
    return "MeshGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  MeshGeometryProcessor(bool hasTexCoords, bool hasColors, PMColor color, const Matrix& viewMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position = {};
  Attribute texCoord = {};
  Attribute color = {};

  bool hasTexCoords = false;
  bool hasColors = false;
  PMColor commonColor = {};
  Matrix viewMatrix = {};
};

}  // namespace tgfx
