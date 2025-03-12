/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLXfermodeFragmentProcessor.h"
#include "gpu/opengl/GLBlend.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromTwoProcessors(
    PlacementBuffer* buffer, PlacementPtr<FragmentProcessor> src,
    PlacementPtr<FragmentProcessor> dst, BlendMode mode) {
  if (src == nullptr && dst == nullptr) {
    return nullptr;
  }
  switch (mode) {
    case BlendMode::Clear:
      return ConstColorProcessor::Make(buffer, Color::Transparent(), InputMode::Ignore);
    case BlendMode::Src:
      return src;
    case BlendMode::Dst:
      return dst;
    default:
      return buffer->make<GLXfermodeFragmentProcessor>(std::move(src), std::move(dst), mode);
  }
}

GLXfermodeFragmentProcessor::GLXfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                                                         PlacementPtr<FragmentProcessor> dst,
                                                         BlendMode mode)
    : XfermodeFragmentProcessor(std::move(src), std::move(dst), mode) {
}

void GLXfermodeFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  if (child == XfermodeFragmentProcessor::Child::TwoChild) {
    std::string inputColor = "inputColor";
    fragBuilder->codeAppendf("vec4 inputColor = vec4(%s.rgb, 1.0);", args.inputColor.c_str());
    std::string srcColor = "xfer_src";
    emitChild(0, inputColor, &srcColor, args);
    std::string dstColor = "xfer_dst";
    emitChild(1, inputColor, &dstColor, args);
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    AppendMode(fragBuilder, srcColor, dstColor, args.outputColor, mode);
    // re-multiply the output color by the input color's alpha
    fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
  } else {
    std::string childColor = "child";
    emitChild(0, &childColor, args);
    // emit blend code
    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(mode));
    if (child == XfermodeFragmentProcessor::Child::DstChild) {
      AppendMode(fragBuilder, args.inputColor, childColor, args.outputColor, mode);
    } else {
      AppendMode(fragBuilder, childColor, args.inputColor, args.outputColor, mode);
    }
  }
}
}  // namespace tgfx
