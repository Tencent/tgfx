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

#include "GLSLComposeFragmentProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> ComposeFragmentProcessor::Make(
    BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> processors) {
  if (processors.empty()) {
    return nullptr;
  }
  return allocator->make<GLSLComposeFragmentProcessor>(std::move(processors));
}

GLSLComposeFragmentProcessor::GLSLComposeFragmentProcessor(
    std::vector<PlacementPtr<FragmentProcessor>> processors)
    : ComposeFragmentProcessor(std::move(processors)) {
}

void GLSLComposeFragmentProcessor::emitCode(EmitArgs& args) const {
  // The first guy's input might be nil.
  std::string temp = "out0";
  emitChild(0, args.inputColor, &temp, args);
  std::string input = temp;
  for (size_t i = 1; i < numChildProcessors() - 1; ++i) {
    temp = "out" + std::to_string(i);
    emitChild(i, input, &temp, args);
    input = temp;
  }
  // The last guy writes to our output variable.
  emitChild(numChildProcessors() - 1, input, args);
}
}  // namespace tgfx
