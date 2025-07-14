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

#include "DefaultTextureProxy.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
int GetApproximateLength(int length) {
  // Map 'value' to a larger multiple of 2. Values <= 'kMagicTol' will pop up to
  // the next power of 2. Those above 'kMagicTol' will only go up half the floor power of 2.

  constexpr int kMinApproxSize = 16;
  constexpr int kMagicTol = 1024;

  length = std::max(kMinApproxSize, length);

  if (IsPow2(length)) {
    return length;
  }

  int ceilPow2 = NextPow2(length);
  if (length <= kMagicTol) {
    return ceilPow2;
  }

  int floorPow2 = ceilPow2 >> 1;
  int mid = floorPow2 + (floorPow2 >> 1);

  if (length <= mid) {
    return mid;
  }
  return ceilPow2;
}

DefaultTextureProxy::DefaultTextureProxy(int width, int height, PixelFormat pixelFormat,
                                         bool mipmapped, ImageOrigin origin, BackingFit backingFit)
    : _width(width), _height(height), _format(pixelFormat), _mipmapped(mipmapped), _origin(origin) {
  if (backingFit == BackingFit::Approximate) {
    _backingStoreWidth = GetApproximateLength(width);
    _backingStoreHeight = GetApproximateLength(height);
  } else {
    _backingStoreWidth = width;
    _backingStoreHeight = height;
  }
}

}  // namespace tgfx
