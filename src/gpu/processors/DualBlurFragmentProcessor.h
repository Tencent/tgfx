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

#pragma once

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
enum class DualBlurPassMode { Up = 0, Down = 1 };

class DualBlurFragmentProcessor : public FragmentProcessor {
 public:
  static std::unique_ptr<DualBlurFragmentProcessor> Make(
      DualBlurPassMode passMode, std::unique_ptr<FragmentProcessor> processor, Point blurOffset,
      Point texelSize);

  std::string name() const override {
    return "DualBlurFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  DualBlurFragmentProcessor(DualBlurPassMode passMode, std::unique_ptr<FragmentProcessor> processor,
                            Point blurOffset, Point texelSize);

  bool onIsEqual(const FragmentProcessor& processor) const override;

  DualBlurPassMode passMode;
  Point blurOffset;
  Point texelSize;
};
}  // namespace tgfx
