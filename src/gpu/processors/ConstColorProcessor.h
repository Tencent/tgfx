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
enum class InputMode { Ignore = 0, ModulateRGBA = 1, ModulateA = 2 };

class ConstColorProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<ConstColorProcessor> Make(BlockAllocator* allocator, PMColor color,
                                                InputMode inputMode);

  std::string name() const override {
    return "ConstColorProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ConstColorProcessor(PMColor color, InputMode mode)
      : FragmentProcessor(ClassID()), color(color), inputMode(mode) {
  }

  PMColor color;
  InputMode inputMode;
};
}  // namespace tgfx
