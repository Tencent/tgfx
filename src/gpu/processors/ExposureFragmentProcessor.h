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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class ExposureFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<ExposureFragmentProcessor> Make(BlockAllocator* allocator, float exposure);

  std::string name() const override {
    return "ExposureFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit ExposureFragmentProcessor(float exposure)
      : FragmentProcessor(ClassID()), exposure(exposure) {
  }

  float exposure = 0.0f;
};
}  // namespace tgfx
