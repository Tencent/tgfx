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

#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TileMode.h"

namespace tgfx {
class SamplerState {
 public:
  SamplerState() = default;

  explicit SamplerState(const SamplingOptions& sampling)
      : minFilterMode(sampling.minFilterMode), magFilterMode(sampling.magFilterMode),
        mipmapMode(sampling.mipmapMode) {
  }

  SamplerState(TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling)
      : tileModeX(tileModeX), tileModeY(tileModeY), minFilterMode(sampling.minFilterMode),
        magFilterMode(sampling.magFilterMode), mipmapMode(sampling.mipmapMode) {
  }

  SamplerState(TileMode tileModeX, TileMode tileModeY, FilterMode filterMode = FilterMode::Linear,
               MipmapMode mipmapMode = MipmapMode::None)
      : tileModeX(tileModeX), tileModeY(tileModeY), minFilterMode(filterMode),
        magFilterMode(filterMode), mipmapMode(mipmapMode) {
  }

  SamplerState(TileMode tileModeX, TileMode tileModeY, FilterMode minFilterMode,
               FilterMode magFilterMode, MipmapMode mipmapMode = MipmapMode::None)
      : tileModeX(tileModeX), tileModeY(tileModeY), minFilterMode(minFilterMode),
        magFilterMode(magFilterMode), mipmapMode(mipmapMode) {
  }

  TileMode tileModeX = TileMode::Clamp;
  TileMode tileModeY = TileMode::Clamp;
  FilterMode minFilterMode = FilterMode::Linear;
  FilterMode magFilterMode = FilterMode::Linear;
  MipmapMode mipmapMode = MipmapMode::None;
};
}  // namespace tgfx
