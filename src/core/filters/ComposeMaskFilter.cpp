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

#include "ComposeMaskFilter.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<MaskFilter> MaskFilter::Compose(std::shared_ptr<MaskFilter> inner,
                                                std::shared_ptr<MaskFilter> outer) {
  if (outer == nullptr && inner == nullptr) {
    return nullptr;
  }
  if (outer == nullptr) {
    return inner;
  }
  if (inner == nullptr) {
    return outer;
  }
  return std::make_shared<ComposeMaskFilter>(std::move(inner), std::move(outer));
}

ComposeMaskFilter::ComposeMaskFilter(std::shared_ptr<MaskFilter> inner,
                                     std::shared_ptr<MaskFilter> outer)
    : inner(std::move(inner)), outer(std::move(outer)) {
}

std::unique_ptr<FragmentProcessor> ComposeMaskFilter::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto innerProcessor = inner->asFragmentProcessor(args, uvMatrix);
  auto outerProcessor = outer->asFragmentProcessor(args, uvMatrix);
  return FragmentProcessor::Compose(std::move(innerProcessor), std::move(outerProcessor));
}
}  // namespace tgfx
