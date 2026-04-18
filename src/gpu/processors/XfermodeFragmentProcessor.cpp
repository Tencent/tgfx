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
#include "gpu/processors/ConstColorProcessor.h"

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

void XfermodeFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(mode) | (static_cast<uint32_t>(child) << 16));
}

ShaderCallManifest XfermodeFragmentProcessor::buildContainerCallStatement(
    const std::string& inputColor, const std::vector<std::string>& childOutputs,
    const MangledUniforms& /*uniforms*/, const MangledSamplers& /*samplers*/,
    const MangledVaryings& /*varyings*/) const {
  auto input = inputColor.empty() ? std::string("vec4(1.0)") : inputColor;
  int blendModeInt = static_cast<int>(mode);
  int childModeInt = static_cast<int>(child);

  std::string childSrc;
  std::string childDst;
  if (child == Child::TwoChild) {
    childSrc = childOutputs[0];
    childDst = childOutputs[1];
  } else if (child == Child::DstChild) {
    childSrc = "vec4(0.0)";
    childDst = childOutputs[0];
  } else {
    childSrc = childOutputs[0];
    childDst = "vec4(0.0)";
  }

  ShaderCallManifest manifest;
  manifest.functionName = "TGFX_XfermodeFragmentProcessor";
  manifest.outputVarName = "_xfpResult";
  manifest.argExpressions = {input, childSrc, childDst, std::to_string(blendModeInt),
                             std::to_string(childModeInt)};
  return manifest;
}

std::vector<FragmentProcessor::ChildEmitInfo> XfermodeFragmentProcessor::getChildEmitPlan(
    const std::string& parentInput) const {
  auto input = parentInput.empty() ? std::string("vec4(1.0)") : parentInput;
  if (child == Child::TwoChild) {
    auto opaqueExpr = "vec4(" + input + ".rgb, 1.0)";
    return {{0, opaqueExpr}, {1, opaqueExpr}};
  }
  return {{0, ""}};
}
}  // namespace tgfx
