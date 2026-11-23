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

#include "GLSLXfermodeFragmentProcessor.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {
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
      return allocator->make<GLSLXfermodeFragmentProcessor>(std::move(src), std::move(dst), mode);
  }
}

GLSLXfermodeFragmentProcessor::GLSLXfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                                                             PlacementPtr<FragmentProcessor> dst,
                                                             BlendMode mode)
    : XfermodeFragmentProcessor(std::move(src), std::move(dst), mode) {
}

void GLSLXfermodeFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  if (child == XfermodeFragmentProcessor::Child::TwoChild) {
    std::string inputColor = "inputColor";
    auto opaqueEntry = fragBuilder->appendFunction("tgfxOpaqueColor");
    fragBuilder->codeAppendf("vec4 %s;", inputColor.c_str());
    if (opaqueEntry.empty()) {
      fragBuilder->codeAppendf("%s = vec4(%s.rgb, 1.0);", inputColor.c_str(),
                               args.inputColor.c_str());
    } else {
      fragBuilder->appendCall(inputColor, opaqueEntry, {args.inputColor});
    }
    std::string srcColor = "xfer_src";
    emitChild(0, inputColor, &srcColor, args);
    std::string dstColor = "xfer_dst";
    emitChild(1, inputColor, &dstColor, args);
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    AppendBlendFunction(fragBuilder, srcColor, dstColor, args.outputColor, mode);
    // re-multiply the output color by the input color's alpha
    auto mulEntry = fragBuilder->appendFunction("tgfxMulAlpha");
    if (mulEntry.empty()) {
      fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
    } else {
      fragBuilder->appendCall(args.outputColor, mulEntry, {args.outputColor, args.inputColor});
    }
  } else {
    std::string childColor = "child";
    emitChild(0, &childColor, args);
    // emit blend code
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    if (child == XfermodeFragmentProcessor::Child::DstChild) {
      AppendBlendFunction(fragBuilder, args.inputColor, childColor, args.outputColor, mode);
    } else {
      AppendBlendFunction(fragBuilder, childColor, args.inputColor, args.outputColor, mode);
    }
  }
}
}  // namespace tgfx
