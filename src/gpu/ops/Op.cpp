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

#include "Op.h"
#include <atomic>

namespace tgfx {

uint8_t Op::GenOpClassID() {
  static std::atomic_uint8_t currentOpClassID = {1};
  return currentOpClassID.fetch_add(1, std::memory_order_relaxed);
}

bool Op::combineIfPossible(Op* op) {
  if (_classID != op->_classID) {
    return false;
  }
  auto result = onCombineIfPossible(op);
  if (result) {
    _bounds.join(op->_bounds);
  }
  return result;
}
}  // namespace tgfx
