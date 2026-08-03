/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/SamplerState.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/ImageOrigin.h"

namespace tgfx {

enum class TiledTextureShaderMode {
  None,
  Clamp,
  RepeatNearestNone,
  RepeatLinearNone,
  RepeatLinearMipmap,
  RepeatNearestMipmap,
  MirrorRepeat,
  ClampToBorderNearest,
  ClampToBorderLinear,
};

/** Immutable result of resolving a tiled texture's backend-dependent sampling state. */
struct TiledTextureSampling {
  SamplerState hardwareSampler = {};
  TiledTextureShaderMode shaderModeX = TiledTextureShaderMode::None;
  TiledTextureShaderMode shaderModeY = TiledTextureShaderMode::None;
  Rect shaderSubset = {};
  Rect shaderClamp = {};
  Point shaderDimensions = {};
  bool usesShaderDimensions = false;
  bool strict = false;
  Matrix coordMatrix = {};
  bool hasPerspective = false;
  bool alphaOnly = false;
  ImageOrigin textureOrigin = ImageOrigin::TopLeft;
};

}  // namespace tgfx
