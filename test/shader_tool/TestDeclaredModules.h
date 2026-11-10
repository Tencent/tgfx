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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// A synthetic family for the 2a declaration-layer capabilities: two spec fields (an Int scalar
// and an Int4 array whose length follows the CAP dim), one `when`-gated param and an unroll site.
// Nothing here joins Families(): the family is a value, so the rules are exercised without a
// shipped family (the first real consumer is the Chain module).

#pragma once

#include <cstdint>

#include "gpu/shaders/Family.h"

namespace tgfx {
namespace test_modules {

inline constexpr ModuleDim TestSpecDims[] = {{"CAP", 3}, {"PRESENT", 2}};
// The pool lengths: the former CHAIN_SLOT_CAPACITY values (16 slots, 32 slots, the 3-slot tail).
inline constexpr uint16_t TestPoolCounts[] = {16, 32, 3};

inline constexpr Element TestSpecElements[] = {
    {Element::Kind::Param, ShaderType::Int, "SlotCount", 1, -1, 0, /*spec=*/true},
    {Element::Kind::Param, ShaderType::Int4, "SlotPacked", 1, -1, 0, /*spec=*/true,
     /*countWhen=*/0, TestPoolCounts},
    {Element::Kind::Param, ShaderType::Int, "GatedFlag", 1, 1, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Payload", 1, -1, 0, false, 0, TestPoolCounts},
};

inline constexpr UnrollSite TestUnroll = {"runSlotAt", "result", /*capacityElement=*/1,
                                          /*countElement=*/0};

inline constexpr Module TestSpecModule = {"TestSpec", TestSpecDims, 2, TestSpecElements, 4,
                                          false, true, &TestUnroll};

inline constexpr ModuleDim TestGeomDims[] = {{"STROKE", 2}};

inline constexpr Element TestGeomElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Param, ShaderType::Float4, "Color", 1, 0, 0x02},
};

inline constexpr Module TestGeomModule = {"TestGeom", TestGeomDims, 1, TestGeomElements, 2, true,
                                          false};

inline constexpr const Module* TestGeometries[] = {&TestGeomModule};

inline constexpr FamilyModule TestBody[] = {&TestSpecModule};

inline constexpr Family TestFamily = {"TestFamily", TestGeometries, 1, TestBody, 1, nullptr};

/// The variant knobs: CAP selects the pool length; PRESENT stays 1 so the body module is present
/// (a PRESENT dim of 0 skips the whole module), and the Xfer's HAS_XP stays 0.
inline Variant TestVariant(uint8_t cap) {
  Variant variant = {};
  variant.values[0] = 0;    // TestGeom_STROKE
  variant.values[1] = cap;  // TestSpec_CAP
  variant.values[2] = 1;    // TestSpec_PRESENT
  variant.values[3] = 0;    // HAS_XP
  return variant;
}

}  // namespace test_modules
}  // namespace tgfx
