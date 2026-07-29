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

#include "gpu/WebColorSpace.h"

namespace tgfx {
WebNamedColorSpace ToWebNamedColorSpace(const std::shared_ptr<ColorSpace>& colorSpace) {
  if (colorSpace == nullptr) {
    return WebNamedColorSpace::None;
  }
  if (ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    return WebNamedColorSpace::SRGB;
  }
  if (ColorSpace::Equals(colorSpace.get(), ColorSpace::DisplayP3().get())) {
    return WebNamedColorSpace::DisplayP3;
  }
  return WebNamedColorSpace::Others;
}
}  // namespace tgfx
