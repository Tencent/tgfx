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
}  // namespace tgfx
