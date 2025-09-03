/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "DrawOp.h"

namespace tgfx {
PlacementPtr<ProgramInfo> DrawOp::createProgramInfo(
    RenderTarget* renderTarget, PlacementPtr<GeometryProcessor> geometryProcessor) {
  auto numColorProcessors = colors.size();
  auto fragmentProcessors = std::move(colors);
  fragmentProcessors.reserve(numColorProcessors + coverages.size());
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(std::move(coverage));
  }
  auto context = renderTarget->getContext();
  return context->drawingBuffer()->make<ProgramInfo>(
      renderTarget, std::move(geometryProcessor), std::move(fragmentProcessors), numColorProcessors,
      std::move(xferProcessor), blendMode);
}
}  // namespace tgfx
