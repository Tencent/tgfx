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

#include "gpu/resources/BufferResource.h"
#include "tgfx/core/PathTypes.h"

namespace tgfx {
/**
 * Stores a stencil-cover vertex stream together with the fill rule used to interpret it.
 */
class StencilCoverPathBuffer final : public BufferResource {
 public:
  /**
   * Finds or creates a vertex buffer in a dedicated scratch pool and sets its fill rule. The caller
   * must upload the matching vertices before publishing the resource through a proxy.
   */
  static std::shared_ptr<StencilCoverPathBuffer> FindOrCreate(Context* context, size_t size,
                                                            PathFillType fillType);

  /**
   * Returns the fill rule associated with the current vertex stream.
   */
  PathFillType fillType() const {
    return pathFillType;
  }

 private:
  PathFillType pathFillType = PathFillType::Winding;

  StencilCoverPathBuffer(std::shared_ptr<GPUBuffer> buffer, PathFillType fillType)
      : BufferResource(std::move(buffer)), pathFillType(fillType) {
  }
};
}  // namespace tgfx
