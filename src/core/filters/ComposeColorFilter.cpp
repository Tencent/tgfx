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

#include "ComposeColorFilter.h"
#include "core/utils/Caster.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<ColorFilter> ColorFilter::Compose(std::shared_ptr<ColorFilter> inner,
                                                  std::shared_ptr<ColorFilter> outer) {
  if (outer == nullptr && inner == nullptr) {
    return nullptr;
  }
  if (outer == nullptr) {
    return inner;
  }
  if (inner == nullptr) {
    return outer;
  }
  return std::make_shared<ComposeColorFilter>(std::move(inner), std::move(outer));
}

ComposeColorFilter::ComposeColorFilter(std::shared_ptr<ColorFilter> inner,
                                       std::shared_ptr<ColorFilter> outer)
    : inner(std::move(inner)), outer(std::move(outer)) {
}

bool ComposeColorFilter::isAlphaUnchanged() const {
  return outer->isAlphaUnchanged() && inner->isAlphaUnchanged();
}

bool ComposeColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto other = Caster::AsComposeColorFilter(colorFilter);
  return other && inner->isEqual(other->inner.get()) && outer->isEqual(other->outer.get());
}

PlacementPtr<FragmentProcessor> ComposeColorFilter::asFragmentProcessor(Context* context) const {
  auto innerProcessor = inner->asFragmentProcessor(context);
  auto outerProcessor = outer->asFragmentProcessor(context);
  return FragmentProcessor::Compose(context->drawingBuffer(), std::move(innerProcessor),
                                    std::move(outerProcessor));
}
}  // namespace tgfx
