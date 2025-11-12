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

#pragma once

#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class SingleIntervalGradientColorizer : public FragmentProcessor {
 public:
  static PlacementPtr<SingleIntervalGradientColorizer> Make(BlockAllocator* allocator, Color start,
                                                            Color end);

  std::string name() const override {
    return "SingleIntervalGradientColorizer";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  SingleIntervalGradientColorizer(Color start, Color end)
      : FragmentProcessor(ClassID()), start(start), end(end) {
  }

  Color start;
  Color end;
};
}  // namespace tgfx
