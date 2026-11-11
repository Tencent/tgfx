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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/ProgramWriterFactory.h"
#include "core/utils/Log.h"
#include "gpu/ProgramWriter.h"
#include "gpu/shaders/ChainInputs.h"
#include "gpu/shaders/Families.h"

namespace tgfx {

/// The declaration-driven families' layout-addressed writer (DeclaredFamilyWriter.cpp).
std::unique_ptr<ProgramWriter> MakeDeclaredFamilyProgramWriter(const ProgramInfo& info,
                                                               const Variant& variant);

// The re-expressing chain family carries its own writer (it compiles the draw's whole expression
// into the chain program); the YUV family appends its compiled tail records to the standard walk;
// the declaration-driven families write through their derived layouts, derived once per program
// from the matched variant; every other family uses the generic precompiled writer.
std::unique_ptr<ProgramWriter> MakeProgramWriter(const ProgramInfo& info, const Variant& variant) {
  // The constant expression makes a name that drifts from the family table fail the build here
  // instead of silently returning another family's writer at runtime.
  constexpr uint16_t chainIndex = FamilyIndexOf("Chain");
  if (variant.family == chainIndex) {
    // The declared Chain family reuses the legacy interpreter's writer verbatim: the assembled
    // block carries the same names, so every by-name write lands unchanged.
    return std::make_unique<ChainProgramWriter>();
  }
  if (variant.family >= FamilyCount()) {
    // Every variant reaching here came out of the family table's match rules, so an out-of-range
    // index is a defect rather than a draw the runtime can serve: nothing would upload that
    // program's uniforms.
    LOGE("MakeProgramWriter: family index %u is outside the family table", variant.family);
    return nullptr;
  }
  return MakeDeclaredFamilyProgramWriter(info, variant);
}

}  // namespace tgfx
