/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"

namespace tgfx {

AOTDrawFacts ExtractAOTDrawFacts(const ProgramInfo* programInfo) {
  AOTDrawFacts facts = {};
  facts.info = programInfo;
  facts.alphaOnlyTarget = programInfo->getOutputSwizzle() == Swizzle::AAAA();
  facts.colorFPCount = programInfo->numColorFragmentProcessors();
  facts.fragmentCount = programInfo->numFragmentProcessors();
  facts.coverage = ClassifyCoverageTerms(programInfo);
  facts.ir = programInfo->getCarriedIR();
  if (facts.ir != nullptr) {
    facts.geometry = facts.ir->geometry;
    facts.xfer = facts.ir->xfer;
    facts.colorFromPaint = facts.ir->colorFromPaint;
  } else {
    // A draw without a carried IR still has exact facts: the descriptor twins read the made
    // processor and the xfer the same way the record-time forms would have.
    facts.geometry = DescribeGeometryProcessor(programInfo->getGeometryProcessor());
    facts.xfer = ClassifyXferForMatch(programInfo->getXferProcessor());
  }
  return facts;
}

}  // namespace tgfx
