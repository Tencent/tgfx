/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "AlphaThresholdFragmentProcessor.h"
#include "gpu/aot/ir/EffectGraph.h"

namespace tgfx {

bool AlphaThresholdFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                                 AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  AOTAlphaThresholdParameters parameters = {threshold};
  return builder->addAlphaThreshold(input, parameters, output);
}

}  // namespace tgfx
