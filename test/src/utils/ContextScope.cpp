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

#include "ContextScope.h"
#include "core/AtlasManager.h"

namespace tgfx {

ContextScope::ContextScope() {
  device = DevicePool::Make();
  if (device != nullptr) {
    context = device->lockContext();
    if (context) {
      // Clearing the atlas cache to prevent interference between different text test cases.
      // For glyphs with Linear sampling, when placed at different locations within the atlas,
      // interpolation errors in texture coordinates can lead to slight variations in
      // the final pixel color.
      context->atlasManager()->releaseAll();
    }
  }
}

ContextScope::~ContextScope() {
  if (context) {
    device->unlock();
  }
}

}  // namespace tgfx
