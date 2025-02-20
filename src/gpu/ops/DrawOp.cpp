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

#include "DrawOp.h"
#include "core/utils/Log.h"
#include "core/utils/Profiling.h"
#include "gpu/Gpu.h"

namespace tgfx {
std::unique_ptr<Pipeline> DrawOp::createPipeline(RenderPass* renderPass,
                                                 std::unique_ptr<GeometryProcessor> gp) {
  TRACE_EVENT_NAME("createPipeline");
  auto numColorProcessors = colors.size();
  std::vector<std::unique_ptr<FragmentProcessor>> fragmentProcessors = {};
  fragmentProcessors.resize(numColorProcessors + coverages.size());
  std::move(colors.begin(), colors.end(), fragmentProcessors.begin());
  std::move(coverages.begin(), coverages.end(),
            fragmentProcessors.begin() + static_cast<int>(numColorProcessors));
  auto format = renderPass->renderTarget()->format();
  auto caps = renderPass->getContext()->caps();
  const auto& swizzle = caps->getWriteSwizzle(format);
  return std::make_unique<Pipeline>(std::move(gp), std::move(fragmentProcessors),
                                    numColorProcessors, std::move(xferProcessor), blendMode,
                                    &swizzle);
}
}  // namespace tgfx
