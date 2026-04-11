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
#include "gpu/glsl/GLSLBlend.h"
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

ShaderCallResult XfermodeFragmentProcessor::buildContainerCallStatement(
    const std::string& inputColor, const std::vector<std::string>& childOutputs,
    const MangledUniforms& /*uniforms*/, const MangledSamplers& /*samplers*/,
    const MangledVaryings& /*varyings*/) const {
  ShaderCallResult result;
  std::string code;
  auto input = inputColor.empty() ? std::string("vec4(1.0)") : inputColor;
  if (child == Child::TwoChild) {
    code += "vec4 _xfpResult = TGFX_XfermodeFragmentProcessor(" + input + ", " + childOutputs[0] +
            ", " + childOutputs[1] + ");\n";
  } else if (child == Child::DstChild) {
    code += "vec4 _xfpResult = TGFX_XfermodeFragmentProcessor(" + input + ", " + childOutputs[0] +
            ");\n";
  } else {
    code += "vec4 _xfpResult = TGFX_XfermodeFragmentProcessor(" + input + ", " + childOutputs[0] +
            ");\n";
  }
  result.statement = code;
  result.outputVarName = "_xfpResult";
  return result;
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

bool XfermodeFragmentProcessor::emitContainerCode(FragmentShaderBuilder* fragBuilder,
                                                  UniformHandler* /*uniformHandler*/,
                                                  const std::string& input,
                                                  const std::string& output,
                                                  size_t transformedCoordVarsIdx,
                                                  const EmitChildFunc& emitChild) const {
  std::string coverage = "vec4(1.0)";
  auto inputRef = input.empty() ? std::string("vec4(1.0)") : input;
  if (child == Child::TwoChild) {
    std::string opaqueInput = output + "_opaque";
    fragBuilder->codeAppendf("vec4 %s = vec4(%s.rgb, 1.0);", opaqueInput.c_str(), inputRef.c_str());
    auto srcCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, 0);
    auto srcColor = emitChild(childProcessor(0), srcCoordIdx, opaqueInput, {});
    auto dstCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, 1);
    auto dstColor = emitChild(childProcessor(1), dstCoordIdx, opaqueInput, {});
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    AppendMode(fragBuilder, srcColor, coverage, dstColor, output, mode, false);
    fragBuilder->codeAppendf("%s *= %s.a;", output.c_str(), inputRef.c_str());
  } else {
    auto childCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, 0);
    auto childColor = emitChild(childProcessor(0), childCoordIdx, "", {});
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    if (child == Child::DstChild) {
      AppendMode(fragBuilder, inputRef, coverage, childColor, output, mode, false);
    } else {
      AppendMode(fragBuilder, childColor, coverage, inputRef, output, mode, false);
    }
  }
  return true;
}
}  // namespace tgfx
