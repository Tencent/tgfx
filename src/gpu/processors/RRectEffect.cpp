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

#include "RRectEffect.h"

namespace tgfx {

// The transform path adds a per-fragment matrix multiply, so a dedicated program that skips this
// for the identity case is worth the extra program. The AA branch adds only a bounded amount of
// work that does not grow with the input, so keeping antiAlias a runtime uniform and sharing one
// program across AA and non-AA draws is a better trade-off than doubling the program count.
void RRectEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  // Both flags change the generated shader code, so they must take part in the key. The smoothness
  // value itself stays a uniform and is deliberately left out, otherwise every distinct value would
  // produce its own program.
  uint32_t key = _needTransform ? 1 : 0;
  key |= _hasSmoothness ? 2 : 0;
  bytesKey->write(key);
}

}  // namespace tgfx
