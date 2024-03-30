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

namespace tgfx {
enum class FilterMode {
  /**
   * Single sample point (the nearest neighbor)
   */
  Nearest,

  /**
   * Interpolate between 2x2 sample points (bi-linear interpolation)
   */
  Linear,
};

enum class MipmapMode {
  /**
   * ignore mipmap levels, sample from the "base"
   */
  None,
  /**
   * Sample from the nearest level
   */
  Nearest,
  /**
   * Interpolate between the two nearest levels
   */
  Linear,
};

struct SamplingOptions {
  SamplingOptions() = default;

  explicit SamplingOptions(FilterMode filterMode, MipmapMode mipmapMode = MipmapMode::None)
      : filterMode(filterMode), mipmapMode(mipmapMode) {
  }

  friend bool operator==(const SamplingOptions& a, const SamplingOptions& b) {
    return a.filterMode == b.filterMode && a.mipmapMode == b.mipmapMode;
  }

  friend bool operator!=(const SamplingOptions& a, const SamplingOptions& b) {
    return !(a == b);
  }

  FilterMode filterMode = FilterMode::Linear;
  MipmapMode mipmapMode = MipmapMode::None;
};
}  // namespace tgfx
