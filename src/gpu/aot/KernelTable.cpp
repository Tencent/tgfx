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
#include "gpu/shaders/kernels/GaussianBlur1DShader.h"
#include "gpu/shaders/kernels/GlassUDFTentBlurShader.h"
#include "gpu/shaders/kernels/QuadColorFillShader.h"
#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include "gpu/shaders/kernels/SolidColorFillShader.h"
#include "gpu/shaders/kernels/TexturedEffectShader.h"
#include "gpu/shaders/kernels/TiledTextureFillShader.h"
#include "gpu/shaders/kernels/YUVTextureFillShader.h"

namespace tgfx {

// The rows, in the walk order their families migrate from (GaussianBlur 5th, GlassUDFTentBlur
// 7th, QuadColor 13th, QuadTexture 14th, SolidColor 18th, TexturedEffect 19th, TiledTexture
// 20th, YUV 21st of the dedicated list; MaskFill stays on the walk — its rasterized-shape mask
// is a prepare-grown coverage, exactly the case the carried IR drops by the staleness guard): every entry's match is the family's own
// ExtractFacts pipeline, so a row and the walk cannot disagree.
static const std::vector<AOTKernelRow> rows = {
    AOTKernelRow{GaussianBlur1DShader::Name, GaussianBlur1DShader::MatchFromFacts},
    AOTKernelRow{GlassUDFTentBlurShader::Name, GlassUDFTentBlurShader::MatchFromFacts},
    AOTKernelRow{QuadColorFillShader::Name, QuadColorFillShader::MatchFromFacts},
    AOTKernelRow{QuadTextureFillShader::Name, QuadTextureFillShader::MatchFromFacts},
    AOTKernelRow{SolidColorFillShader::Name, SolidColorFillShader::MatchFromFacts},
    AOTKernelRow{TexturedEffectShader::Name, TexturedEffectShader::MatchFromFacts},
    AOTKernelRow{TiledTextureFillShader::Name, TiledTextureFillShader::MatchFromFacts},
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
