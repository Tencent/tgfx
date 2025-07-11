/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "DefaultTextureProxy.h"

namespace tgfx {
DefaultTextureProxy::DefaultTextureProxy(int width, int height, bool mipmapped, bool isAlphaOnly,
                                         ImageOrigin origin)
    : _width(width), _height(height) {
  bitFields.origin = origin;
  bitFields.mipmapped = mipmapped;
  bitFields.isAlphaOnly = isAlphaOnly;
}
}  // namespace tgfx
