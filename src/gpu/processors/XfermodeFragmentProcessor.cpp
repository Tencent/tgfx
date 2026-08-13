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

#include "XfermodeFragmentProcessor.h"
#include <algorithm>
#include "gpu/AOTEffect.h"
#include "gpu/UniformData.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromSrcProcessor(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> src, BlendMode mode) {
  return MakeFromTwoProcessors(allocator, std::move(src), nullptr, mode);
}

PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromDstProcessor(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> dst, BlendMode mode) {
  return MakeFromTwoProcessors(allocator, nullptr, std::move(dst), mode);
}

std::string XfermodeFragmentProcessor::name() const {
  switch (child) {
    case Child::TwoChild:
      return "XfermodeFragmentProcessor - two";
    case Child::DstChild:
      return "XfermodeFragmentProcessor - dst";
    case Child::SrcChild:
      return "XfermodeFragmentProcessor - src";
  }
  return "XfermodeFragmentProcessor";
}

XfermodeFragmentProcessor::XfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                                                     PlacementPtr<FragmentProcessor> dst,
                                                     BlendMode mode)
    : FragmentProcessor(ClassID()), mode(mode) {
  if (src && dst) {
    child = Child::TwoChild;
    registerChildProcessor(std::move(src));
    registerChildProcessor(std::move(dst));
  } else if (src) {
    child = Child::SrcChild;
    registerChildProcessor(std::move(src));
  } else {
    child = Child::DstChild;
    registerChildProcessor(std::move(dst));
  }
}

void XfermodeFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  if (fragmentUniformData->hasField("BlendModeValue")) {
    int modeInt = static_cast<int>(mode);
    fragmentUniformData->setArrayElement("BlendModeValue", 0, modeInt);
  }
  // Selects how the operands map onto the blend's src/dst in the fused chain kernel. The
  // single-child roles (DstChild/SrcChild) share one compiled variant and are told apart here, so
  // this must be written on every draw rather than relying on the uniform buffer's prior contents.
  if (fragmentUniformData->hasField("ChildType")) {
    int childTypeValue = static_cast<int>(child);
    fragmentUniformData->setData("ChildType", childTypeValue);
  }
  if (fragmentUniformData->hasField("HasClip")) {
    int hasClip = 0;
    fragmentUniformData->setData("HasClip", hasClip);
  }
  // Populate the per-child subset uniforms for the precompiled blend path. Each plain
  // TextureEffect child that requires subset clamping writes its normalized subset rect into a
  // dedicated uniform (Child0Subset for child[0], Child1Subset for child[1]). Distinct names are
  // required because the precompiled path strips uniform name suffixes: if we relied on the
  // children's own "Subset" emission, the two texture children would collide on a single field and
  // the last writer would overwrite the first. On the ProgramBuilder path these fields do not
  // exist, so hasField() returns false and the writes are skipped.
  static const char* const subsetFieldNames[] = {"Child0Subset", "Child1Subset"};
  static const char* const alphaOnlyFieldNames[] = {"Child0AlphaOnly", "Child1AlphaOnly"};
  size_t childCount = std::min<size_t>(numChildProcessors(), 2);
  for (size_t i = 0; i < childCount; i++) {
    auto childFP = childProcessor(i);
    if (childFP == nullptr || childFP->name() != "TextureEffect") {
      continue;
    }
    auto childTE = static_cast<const TextureEffect*>(childFP);
    if (fragmentUniformData->hasField(subsetFieldNames[i])) {
      float rect[4];
      childTE->computeSubsetRect(rect);
      fragmentUniformData->setData(subsetFieldNames[i], rect);
    }
    // Alpha-only stays a runtime semantic flag so the same compiled variant handles
    // RGBA and R8 operands. ProgramBuilder layouts do not declare this field, hence the optional
    // write; the precompiled layout receives the actual value on every draw.
    fragmentUniformData->setDataOptional(alphaOnlyFieldNames[i], childTE->isAlphaOnly() ? 1 : 0);
  }
}

void XfermodeFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(mode) | (static_cast<uint32_t>(child) << 16));
}

bool XfermodeFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                           AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  // Map the two blend operands onto builder nodes. The child registration order (see the
  // constructor) is: DstChild -> child(0)=dst, src=input color; SrcChild -> child(0)=src, dst=input
  // color; TwoChild -> child(0)=src, child(1)=dst, both fed the input color. Any child without an
  // AOT lowering aborts the whole tree, falling back to the plain route.
  AOTNodeID src = AOTNodeID::Invalid();
  AOTNodeID dst = AOTNodeID::Invalid();
  bool inputIsPlainGeometryColor = false;
  switch (child) {
    case Child::DstChild:
      if (numChildProcessors() != 1) {
        return false;
      }
      src = input;
      if (!childProcessor(0)->lowerToAOT(builder, input, &dst)) {
        return false;
      }
      break;
    case Child::SrcChild:
      if (numChildProcessors() != 1) {
        return false;
      }
      dst = input;
      if (!childProcessor(0)->lowerToAOT(builder, input, &src)) {
        return false;
      }
      break;
    case Child::TwoChild: {
      if (numChildProcessors() != 2) {
        return false;
      }
      // The runtime feeds both children vec4(inputColor.rgb, 1.0) and re-multiplies the output
      // by the input's alpha. The alpha override is idempotent, so when this xfer's input is
      // already the opaque-alpha geometry input the children reuse it; otherwise the input must
      // be the geometry color itself. Any other input keeps the plain route.
      inputIsPlainGeometryColor = input == AOTNodeID(0);
      AOTNodeID childInput = input;
      if (inputIsPlainGeometryColor) {
        if (!builder->addGeometryColorOpaqueInput(&childInput)) {
          return false;
        }
      } else if (!builder->isGeometryColorOpaqueInput(input)) {
        return false;
      }
      if (!childProcessor(0)->lowerToAOT(builder, childInput, &src)) {
        return false;
      }
      if (!childProcessor(1)->lowerToAOT(builder, childInput, &dst)) {
        return false;
      }
      break;
    }
  }
  AOTBlendParameters parameters = {};
  parameters.blendMode = static_cast<int>(mode);
  parameters.childType = static_cast<int>(child);
  // The output epilogue multiplies the xfer input's alpha: the plain geometry color's alpha
  // when the input is geometry, or 1.0 when it is already the opaque-alpha input (a no-op).
  parameters.multiplyInputAlpha = child == Child::TwoChild && inputIsPlainGeometryColor;
  return builder->addBlend(src, dst, parameters, output);
}
}  // namespace tgfx
