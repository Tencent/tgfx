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

#include "tgfx/gpu/RenderFlags.h"

namespace tgfx {
/**
 * Describes properties and constraints of a given Surface. The rendering engine can parse these
 * during drawing, and can sometimes optimize its performance (e.g., disabling an expensive feature).
 */
class SurfaceOptions {
 public:
  SurfaceOptions() = default;

  SurfaceOptions(uint32_t renderFlags) : _renderFlags(renderFlags) {
  }

  uint32_t renderFlags() const {
    return _renderFlags;
  }

  bool cacheDisabled() const {
    return _renderFlags & RenderFlags::DisableCache;
  }

  bool asyncTaskDisabled() const {
    return _renderFlags & RenderFlags::DisableAsyncTask;
  }

  bool operator==(const SurfaceOptions& that) const {
    return _renderFlags == that._renderFlags;
  }

 private:
  uint32_t _renderFlags = 0;
};

}  // namespace tgfx
