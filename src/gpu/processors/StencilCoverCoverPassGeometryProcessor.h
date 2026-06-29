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
#include "tgfx/core/Matrix.h"

namespace tgfx {
/**
 * StencilCoverCoverPassGeometryProcessor drives the cover pass of the stencil-and-cover render
 * path. It draws a single quad sized to the device-space bounds of the shape; the configured
 * stencil test (typically NotEqual + ref=0) keeps only the pixels the stencil pass marked,
 * and those surviving fragments are shaded with the brush colour.
 *
 * The Loop-Blinn implicit-curve test does *not* run here — it has already been evaluated by
 * the stencil pass and its result is locked into the stencil buffer. The cover pass only
 * needs per-pixel position and the brush colour.
 *
 * The processor owns the final colour uniform because when the brush has no shader the brush
 * colour is the only input to the cover pass; shader / colour-filter / mask-filter FPs from
 * OpsCompositor::addDrawOp still sit upstream and composite against this colour through the
 * standard ProgramBuilder FP chain.
 */
class StencilCoverCoverPassGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<StencilCoverCoverPassGeometryProcessor> Make(BlockAllocator* allocator,
                                                                   PMColor color,
                                                                   const Matrix& viewMatrix);

  std::string name() const override {
    return "StencilCoverCoverPassGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  StencilCoverCoverPassGeometryProcessor(PMColor color, const Matrix& viewMatrix);

  Attribute position;

  PMColor color = PMColor::Transparent();
  Matrix viewMatrix = {};
};
}  // namespace tgfx
