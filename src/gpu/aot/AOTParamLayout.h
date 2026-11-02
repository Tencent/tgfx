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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// A program's resolved parameter addressing: instance k of a parameter lives at
// base + k * stride. A plain field is the k = 0 special case (stride 0, count 1); an ordinal
// family (Subset / Subset_1 / ...) resolves to the constant stride its consecutive declarations
// carry, and a reflected array (the chain's slot records) to its elementStride. Resolving the
// tables once per program is what moves the per-draw writes from composing names and hashing them
// to addressing bytes directly.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace tgfx {

class UniformData;

struct AOTParamSlot {
  size_t base = 0;
  size_t stride = 0;
  size_t elementSize = 0;
  /// The addressable instances. Zero marks a parameter this program does not declare (or one
  /// whose ordinal instances are not equidistant — the name-addressed path stays authoritative
  /// for it), so a writer falls back rather than computes a wrong address.
  uint16_t count = 0;
};

class AOTParamLayout {
 public:
  /// Resolves every registry parameter against one stage's reflected fields: the one-time name
  /// work per program. See AOTParamSlot for what each form resolves to.
  static std::unique_ptr<AOTParamLayout> Resolve(const UniformData& uniforms);

  const AOTParamSlot* slot(uint16_t paramId) const {
    return paramId < _slots.size() && _slots[paramId].count != 0 ? &_slots[paramId] : nullptr;
  }

 private:
  /// Indexed by parameter id; the size is the registry's, fixed for a build.
  std::vector<AOTParamSlot> _slots = {};
};

}  // namespace tgfx
