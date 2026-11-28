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
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <optional>
#include "tgfx/core/Point.h"

namespace tgfx {

class TextureProxy;

/// The destination-texture fetch state the xfer uniforms carry (offset into the shared dst
/// texture); a null proxy means the backend's framebuffer fetch supplies the destination.
struct DstTextureInfo {
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  Point offset = {};
};

}  // namespace tgfx
