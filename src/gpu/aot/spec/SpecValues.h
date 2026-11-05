/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>
#include "tgfx/core/BytesKey.h"

namespace tgfx {

/**
 * The specialization values of one draw: one 32-bit integer per injectable constant, in the
 * bundle's spec-table order — the master switch (id 0) first, then every field's words (a scalar
 * int holds one, an ivec4 array four per element, element-major). The values are the very bytes
 * the uniform writers wrote (see SpecCapture), and the program cache key is derived from them, so
 * the values a specialized shader folds, the key that addresses it and the upload that feeds the
 * generic form can never disagree.
 */
struct SpecValues {
  std::vector<int32_t> values;

  bool empty() const {
    return values.empty();
  }

  /**
   * Appends the values to a program cache key. The caller prefixes the key with the variant's
   * identity, so two draws of one variant whose structural uniforms differ resolve to two
   * programs — the property the un-specialized key could not express.
   * @param key  The key being built; the values are appended in order.
   */
  void writeKey(BytesKey* key) const {
    for (auto value : values) {
      key->write(static_cast<uint32_t>(value));
    }
  }
};

}  // namespace tgfx
