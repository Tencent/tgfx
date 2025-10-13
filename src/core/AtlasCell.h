/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/AtlasTypes.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
struct AtlasCell {
  BytesKey key = {};
  Point offset = {};
  MaskFormat maskFormat = MaskFormat::A8;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct AtlasCellLocator {
  Point offset = {};  // The cell's offset for render
  AtlasLocator atlasLocator;
};
}  //namespace tgfx
