/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ComposeMaskFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<MaskFilter> MaskFilter::Compose(std::shared_ptr<MaskFilter> inner,
                                                std::shared_ptr<MaskFilter> outer) {
  if (inner == nullptr) {
    return outer;
  }
  if (outer == nullptr) {
    return inner;
  }
  return std::make_shared<ComposeMaskFilter>(std::move(inner), std::move(outer));
}

std::shared_ptr<MaskFilter> ComposeMaskFilter::makeWithMatrix(const Matrix& viewMatrix) const {
  auto newInner = inner->makeWithMatrix(viewMatrix);
  auto newOuter = outer->makeWithMatrix(viewMatrix);
  DEBUG_ASSERT(newInner != nullptr && newOuter != nullptr);
  return MaskFilter::Compose(std::move(newInner), std::move(newOuter));
}

bool ComposeMaskFilter::isEqual(const MaskFilter* maskFilter) const {
  auto type = Types::Get(maskFilter);
  if (type != Types::MaskFilterType::Compose) {
    return false;
  }
  auto other = static_cast<const ComposeMaskFilter*>(maskFilter);
  return inner->isEqual(other->inner.get()) && outer->isEqual(other->outer.get());
}

PlacementPtr<FragmentProcessor> ComposeMaskFilter::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto innerProcessor = inner->asFragmentProcessor(args, uvMatrix);
  auto outerProcessor = outer->asFragmentProcessor(args, uvMatrix);
  return FragmentProcessor::Compose(args.context->drawingAllocator(), std::move(innerProcessor),
                                    std::move(outerProcessor));
}
}  // namespace tgfx
