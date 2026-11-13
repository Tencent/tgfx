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

#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/aot/ir/EffectGraph.h"

namespace tgfx {
bool ConstColorProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                     AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  AOTConstColorParameters parameters = {};
  parameters.color = {color.red, color.green, color.blue, color.alpha};
  parameters.inputMode = static_cast<int>(inputMode);
  return builder->addConstColor(input, parameters, output);
}

PlacementPtr<ConstColorProcessor> ConstColorProcessor::Make(BlockAllocator* allocator,
                                                            PMColor color, InputMode mode) {
  return allocator->make<ConstColorProcessor>(color, mode);
}

}  // namespace tgfx
