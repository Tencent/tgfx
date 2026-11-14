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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <optional>
#include "gpu/shaders/Family.h"

namespace tgfx {

struct DrawShape;

/**
 * The value one declaration-sourced dimension takes for a draw. Nullopt means the fact does not
 * apply to this draw's shape (an unannotated dim, or one whose value the match rule still decides);
 * the caller then keeps the rule's explicit value instead of guessing one.
 */
std::optional<uint8_t> DimValueOf(DimFact fact, const DrawShape& shape);

/**
 * Composes a family's variant from its declaration and the draw's facts: the dimensions are walked
 * in the declaration's composition order, so a rule never spells one by position. Nullopt while any
 * free dim of the pair is still Unspecified — the family then stays on its rule's explicit values.
 */
std::optional<Variant> ComposeVariantFromFacts(uint16_t familyIndex, uint8_t geometry,
                                               const DrawShape& shape);

}  // namespace tgfx
