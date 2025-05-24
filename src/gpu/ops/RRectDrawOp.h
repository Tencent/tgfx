/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <optional>
#include "DrawOp.h"
#include "gpu/RRectsVertexProvider.h"

namespace tgfx {
class RRectDrawOp : public DrawOp {
 public:
  /**
   * The maximum number of round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 1024;

  /**
   * Create a new RRectDrawOp for a list of RRect records. Note that the returned RRectDrawOp is in
   * the device space.
   */
  static PlacementPtr<RRectDrawOp> Make(Context* context,
                                        PlacementPtr<RRectsVertexProvider> provider,
                                        uint32_t renderFlags);

  void execute(RenderPass* renderPass) override;

 private:
  size_t rectCount = 0;
  bool useScale = false;
  bool hasStroke = false;
  std::optional<Color> commonColor = std::nullopt;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  size_t vertexBufferOffset = 0;

  explicit RRectDrawOp(RRectsVertexProvider* provider);

  friend class BlockBuffer;
};
}  // namespace tgfx
