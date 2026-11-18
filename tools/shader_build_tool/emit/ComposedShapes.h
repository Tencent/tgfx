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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/ChainComposition.h"

namespace tgfx {

/// One pre-composed shape: the chain variant it targets (the profile, geometry and minimum Xfer
/// form pick it out of the family's enumeration) and the structure the shared emitter spells into
/// that variant's stored text. The manifest is curated — the shapes the observed draws most often
/// compose — not enumerated; a shape no draw matches costs only its stored bytes.
struct ComposedShape {
  const char* name = "";
  uint8_t profile = 0;
  uint8_t geometry = 0;
  uint8_t minHasXp = 0;
  ChainComposition composition = {};
};

/// The curated shape list the build pre-composes for every GL-text chain variant that hosts one.
const std::vector<ComposedShape>& ComposedShapeManifest();

/**
 * Maps a composition's spec values onto a variant's constant ids, ascending: walks the derived
 * layout's spec fields (the id assignment order), resolves each field's element name against its
 * module, and reads the value sequence from ResolveCompositionSpecValues. The result is exactly
 * the #define table the runtime's specialization would inject for the same shape.
 * @param family       The Chain family.
 * @param variant      The variant the composition targets (its PROFILE fixes the slot capacity).
 * @param composition  The shape whose values are resolved.
 * @return             The (constant id, value) pairs, ascending by id.
 */
std::vector<std::pair<uint32_t, int32_t>> ComposedSpecIdValues(const Family& family,
                                                               const Variant& variant,
                                                               const ChainComposition& composition);

}  // namespace tgfx
