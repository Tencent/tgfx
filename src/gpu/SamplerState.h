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

#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TileMode.h"

namespace tgfx {
/**
 * Represents the tile modes used to access a texture.
 */
class SamplerState {
 public:
  enum class WrapMode {
    Clamp,
    Repeat,
    MirrorRepeat,
    ClampToBorder,
  };

  SamplerState() = default;

  explicit SamplerState(TileMode tileMode);

  SamplerState(TileMode tileModeX, TileMode tileModeY, SamplingOptions sampling);

  SamplerState(WrapMode wrapModeX, WrapMode wrapModeY, FilterMode filterMode = FilterMode::Linear,
               MipmapMode mipmapMode = MipmapMode::None)
      : wrapModeX(wrapModeX), wrapModeY(wrapModeY), filterMode(filterMode), mipmapMode(mipmapMode) {
  }

  explicit SamplerState(SamplingOptions sampling)
      : filterMode(sampling.filterMode), mipmapMode(sampling.mipmapMode) {
  }

  bool mipmapped() const {
    return mipmapMode != MipmapMode::None;
  }

  friend bool operator==(const SamplerState& a, const SamplerState& b);

  WrapMode wrapModeX = WrapMode::Clamp;
  WrapMode wrapModeY = WrapMode::Clamp;
  FilterMode filterMode = FilterMode::Linear;
  MipmapMode mipmapMode = MipmapMode::None;
};
}  // namespace tgfx
