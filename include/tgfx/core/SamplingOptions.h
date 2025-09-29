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

#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"

namespace tgfx {
struct SamplingOptions {
  SamplingOptions() = default;

  explicit SamplingOptions(FilterMode filterMode, MipmapMode mipmapMode = MipmapMode::Linear)
      : minFilterMode(filterMode), magFilterMode(filterMode), mipmapMode(mipmapMode) {
  }

  SamplingOptions(FilterMode minFilterMode, FilterMode magFilterMode,
                  MipmapMode mipmapMode = MipmapMode::Linear)
      : minFilterMode(minFilterMode), magFilterMode(magFilterMode), mipmapMode(mipmapMode) {
  }

  friend bool operator==(const SamplingOptions& a, const SamplingOptions& b) {
    return a.magFilterMode == b.magFilterMode && a.minFilterMode == b.minFilterMode &&
           a.mipmapMode == b.mipmapMode;
  }

  friend bool operator!=(const SamplingOptions& a, const SamplingOptions& b) {
    return !(a == b);
  }

  FilterMode minFilterMode = FilterMode::Linear;
  FilterMode magFilterMode = FilterMode::Linear;
  MipmapMode mipmapMode = MipmapMode::Linear;
};
}  // namespace tgfx
