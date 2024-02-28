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

#include "tgfx/core/ColorFilter.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
std::unique_ptr<FragmentProcessor> ColorFilter::onFilterImage(
    std::shared_ptr<Image> source, const DrawArgs& args, TileMode tileModeX, TileMode tileModeY,
    const SamplingOptions& sampling, const tgfx::Matrix* localMatrix) const {
  auto imageProcessor =
      FragmentProcessor::Make(std::move(source), args, tileModeX, tileModeY, sampling, localMatrix);
  if (imageProcessor == nullptr) {
    return nullptr;
  }
  auto colorProcessor = asFragmentProcessor();
  return FragmentProcessor::Compose(std::move(imageProcessor), std::move(colorProcessor));
}
}  // namespace tgfx