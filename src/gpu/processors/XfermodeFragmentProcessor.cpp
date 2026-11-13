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
#include "gpu/UniformData.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/BlendParams.h"

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
  AOTNodeID whiteInput = AOTNodeID::Invalid();
  // A single-child xfer feeds its child white (the child's own input color defaults to
  // vec4(1.0)), never the xfer's own input. Lower the child from an explicit white input node so
  // the whole child subtree sees the same input environment the runtime gives it, instead of the
  // builder guessing which descendant should sample raw (the old whiteInputOperand marking only
  // covered the direct child, so a Compose-shaped child leaked the xfer input to the texture
  // deeper inside).
  auto whiteInputNode = [&]() {
    if (!whiteInput.isValid()) {
      if (!builder->addGeometryWhiteInput(&whiteInput)) {
        return false;
      }
    }
    return true;
  };
  switch (child) {
    case Child::DstChild:
      if (numChildProcessors() != 1) {
        return false;
      }
      src = input;
      if (!whiteInputNode() || !childProcessor(0)->lowerToAOT(builder, whiteInput, &dst)) {
        return false;
      }
      break;
    case Child::SrcChild:
      if (numChildProcessors() != 1) {
        return false;
      }
      dst = input;
      if (!whiteInputNode() || !childProcessor(0)->lowerToAOT(builder, whiteInput, &src)) {
        return false;
      }
      break;
    case Child::TwoChild: {
      if (numChildProcessors() != 2) {
        return false;
      }
      // The runtime feeds both children vec4(inputColor.rgb, 1.0) and re-multiplies the output
      // by the input's alpha. The alpha override is idempotent, so when this xfer's input is
      // already the opaque-alpha geometry input the children reuse it; the white input (a
      // single-child operand's child environment) already carries opaque alpha and an identity
      // alpha multiply, so it passes through unchanged. A computed input C materializes both
      // halves explicitly: an InputOpaque node gives the children vec4(C.rgb, 1.0), and the
      // epilogue's multiply by C.a becomes an explicit MulAlpha instruction over the blend.
      inputIsPlainGeometryColor = input == AOTNodeID(0);
      AOTNodeID childInput = input;
      bool inputIsComputed = false;
      if (inputIsPlainGeometryColor) {
        if (!builder->addGeometryColorOpaqueInput(&childInput)) {
          return false;
        }
      } else if (!builder->isGeometryColorOpaqueInput(input) &&
                 !builder->isGeometryWhiteInput(input)) {
        if (!builder->addInputOpaque(input, &childInput)) {
          return false;
        }
        inputIsComputed = true;
      }
      if (!childProcessor(0)->lowerToAOT(builder, childInput, &src)) {
        return false;
      }
      if (!childProcessor(1)->lowerToAOT(builder, childInput, &dst)) {
        return false;
      }
      if (inputIsComputed) {
        AOTBlendParameters computedParameters = {};
        computedParameters.blendMode = static_cast<int>(mode);
        computedParameters.childType = static_cast<int>(child);
        computedParameters.multiplyInputAlpha = false;
        AOTNodeID blended = AOTNodeID::Invalid();
        if (!builder->addBlend(src, dst, computedParameters, &blended)) {
          return false;
        }
        return builder->addMulAlpha(blended, input, output);
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

PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromTwoProcessors(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> src,
    PlacementPtr<FragmentProcessor> dst, BlendMode mode) {
  if (src == nullptr && dst == nullptr) {
    return nullptr;
  }
  switch (mode) {
    case BlendMode::Clear:
      return ConstColorProcessor::Make(allocator, PMColor::Transparent(), InputMode::Ignore);
    case BlendMode::Src:
      return src;
    case BlendMode::Dst:
      return dst;
    default:
      return allocator->make<XfermodeFragmentProcessor>(std::move(src), std::move(dst), mode);
  }
}

}  // namespace tgfx
