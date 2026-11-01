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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/KernelTable.h"
#include "gpu/shaders/kernels/QuadColorFillShader.h"
#include "gpu/shaders/kernels/SolidColorFillShader.h"
#include "gpu/shaders/kernels/YUVTextureFillShader.h"

namespace tgfx {

// The rows, in the walk order their families migrate from (QuadColor 13th, SolidColor 18th,
// YUV 21st of the dedicated list): every entry's match is the family's own ExtractFacts
// pipeline, so a row and the walk cannot disagree.
static const std::vector<AOTKernelRow> rows = {
    AOTKernelRow{QuadColorFillShader::Name, QuadColorFillShader::MatchFromFacts},
    AOTKernelRow{SolidColorFillShader::Name, SolidColorFillShader::MatchFromFacts},
    AOTKernelRow{YUVTextureFillShader::Name, YUVTextureFillShader::MatchFromFacts},
};

const std::vector<AOTKernelRow>& KernelTableRows() {
  return rows;
}

std::optional<AOTKernelMatch> KernelTableLookup(const AOTDrawFacts& facts,
                                                PermutationMatchFailure* failure) {
  for (const auto& row : rows) {
    if (auto variant = row.match(facts, failure)) {
      return AOTKernelMatch{&row, *variant};
    }
  }
  return std::nullopt;
}

}  // namespace tgfx
