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

#include "ComposeFragmentProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> ComposeFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> first,
    PlacementPtr<FragmentProcessor> second) {
  if (first == nullptr && second == nullptr) {
    return nullptr;
  }
  if (first == nullptr) {
    return second;
  }
  if (second == nullptr) {
    return first;
  }
  std::vector<PlacementPtr<FragmentProcessor>> processors = {};
  if (first->classID() == ComposeFragmentProcessor::ClassID()) {
    auto& children = static_cast<ComposeFragmentProcessor*>(first.get())->childProcessors;
    processors = std::move(children);
  } else {
    processors.push_back(std::move(first));
  }
  if (second->classID() == ComposeFragmentProcessor::ClassID()) {
    auto& children = static_cast<ComposeFragmentProcessor*>(second.get())->childProcessors;
    processors.insert(processors.end(), std::make_move_iterator(children.begin()),
                      std::make_move_iterator(children.end()));
  } else {
    processors.push_back(std::move(second));
  }
  return Make(allocator, std::move(processors));
}

ComposeFragmentProcessor::ComposeFragmentProcessor(
    std::vector<PlacementPtr<FragmentProcessor>> processors)
    : FragmentProcessor(ClassID()) {
  for (auto& processor : processors) {
    registerChildProcessor(std::move(processor));
  }
}
}  // namespace tgfx
