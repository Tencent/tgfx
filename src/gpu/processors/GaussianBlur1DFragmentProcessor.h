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

enum class GaussianBlurDirection { Horizontal, Vertical };

class GaussianBlur1DFragmentProcessor : public FragmentProcessor {
 public:
  static std::unique_ptr<GaussianBlur1DFragmentProcessor> Make(
      std::unique_ptr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
      float stepLength);

  std::string name() const override {
    return "GaussianBlur1DFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GaussianBlur1DFragmentProcessor(std::unique_ptr<FragmentProcessor> processor, float sigma,
                                  GaussianBlurDirection direction, float stepLength);

  float sigma = 0.f;
  GaussianBlurDirection direction = GaussianBlurDirection::Horizontal;
  float stepLength = 1.f;
};
}  // namespace tgfx