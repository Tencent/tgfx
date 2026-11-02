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

#include "RectEffect.h"
#include "gpu/aot/EffectGraph.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"

namespace tgfx {

// Only needTransform is keyed. The transform path adds a per-fragment matrix multiply, so a
// dedicated program that skips this for the identity case is worth the extra program. The AA branch
// adds only a bounded amount of work that does not grow with the input, so keeping antiAlias a
// runtime uniform and sharing one program across AA and non-AA draws is a better trade-off than
// doubling the program count.
void RectEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(_needTransform ? 1 : 0));
}

bool RectEffect::onBindPrecompiled(ParamWriter* writer) const {
  AnalyticClipModule::Bind(this, writer);
  return true;
}

bool RectEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  const auto& matrix = deviceToLocal();
  if (!antiAlias) {
    // Reuse the rrect op's runtime antiAlias switch for a hard-edged rect. Zero radii make its
    // interior distance test identical to RectEffect's inclusive non-AA rectangle.
    AOTRRectCoverageParameters parameters = {};
    parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
    parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                matrix[5], matrix[6], matrix[7], matrix[8]};
    parameters.antiAlias = 0.0f;
    return builder->addRRectCoverage(input, parameters, output);
  }
  AOTRectCoverageParameters parameters = {};
  parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
  parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                              matrix[5], matrix[6], matrix[7], matrix[8]};
  return builder->addRectCoverage(input, parameters, output);
}
}  // namespace tgfx
