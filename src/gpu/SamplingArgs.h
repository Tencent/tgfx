/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <optional>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TileMode.h"

namespace tgfx {
class SamplingArgs {
 public:
  SamplingArgs() = default;

  SamplingArgs(TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
               SrcRectConstraint constraint)
      : tileModeX(tileModeX), tileModeY(tileModeY), sampling(sampling), constraint(constraint) {
  }

  TileMode tileModeX = TileMode::Clamp;
  TileMode tileModeY = TileMode::Clamp;
  SamplingOptions sampling = {};
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
  std::optional<Rect> sampleArea = std::nullopt;
};
}  // namespace tgfx
