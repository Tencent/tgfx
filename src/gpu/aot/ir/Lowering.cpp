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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/ir/Lowering.h"
#include <utility>
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

bool LowerToGraph(const std::vector<const FragmentProcessor*>& processors, AOTEffectGraph* graph,
                  std::string* blockingProcessor) {
  if (blockingProcessor != nullptr) {
    blockingProcessor->clear();
  }
  if (graph == nullptr || processors.empty()) {
    return false;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID current = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&current)) {
    return false;
  }
  for (auto processor : processors) {
    if (processor == nullptr) {
      return false;
    }
    AOTNodeID next = AOTNodeID::Invalid();
    if (!processor->lowerToAOT(&builder, current, &next)) {
      auto missingLowering = builder.missingLoweringProcessor();
      if (blockingProcessor != nullptr && missingLowering != nullptr) {
        *blockingProcessor = missingLowering->name();
      }
      return false;
    }
    current = next;
  }
  AOTEffectGraph result = {};
  if (!builder.finish(current, &result)) {
    return false;
  }
  *graph = std::move(result);
  return true;
}

}  // namespace tgfx
