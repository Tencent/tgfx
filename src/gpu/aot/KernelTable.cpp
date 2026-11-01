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
#include "gpu/shaders/kernels/AtlasTextFillShader.h"
#include "gpu/shaders/kernels/ComplexEllipseFillShader.h"
#include "gpu/shaders/kernels/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/kernels/EllipseFillShader.h"
#include "gpu/shaders/kernels/GaussianBlur1DShader.h"
#include "gpu/shaders/kernels/GlassRefractionShader.h"
#include "gpu/shaders/kernels/GlassUDFTentBlurShader.h"
#include "gpu/shaders/kernels/HairlineLineShader.h"
#include "gpu/shaders/kernels/HairlineQuadShader.h"
#include "gpu/shaders/kernels/MaskFillShader.h"
#include "gpu/shaders/kernels/MeshFillShader.h"
#include "gpu/shaders/kernels/NonAARRectFillShader.h"
#include "gpu/shaders/kernels/QuadColorFillShader.h"
#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include "gpu/shaders/kernels/RoundStrokeRectFillShader.h"
#include "gpu/shaders/kernels/ShapeInstancedFillShader.h"
#include "gpu/shaders/kernels/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/kernels/SolidColorFillShader.h"
#include "gpu/shaders/kernels/TexturedEffectShader.h"
#include "gpu/shaders/kernels/TiledTextureFillShader.h"
#include "gpu/shaders/kernels/YUVTextureFillShader.h"

namespace tgfx {

// The rows, in the walk order their families migrated from (the full dedicated list, 1st through
// 21st). Two rows read the third channel rather than the carried IR: MaskFill's rasterized-shape
// mask is a prepare-grown coverage (exactly the case the staleness guard drops), so it reads the
// live mask facts; the rows otherwise share one body with the walk they replaced, so a row and
// the walk cannot disagree.
static const std::vector<AOTKernelRow> rows = {
    AOTKernelRow{AtlasTextFillShader::Name, AtlasTextFillShader::MatchFromFacts},
    AOTKernelRow{ComplexEllipseFillShader::Name, ComplexEllipseFillShader::MatchFromFacts},
    AOTKernelRow{ComplexNonAARRectFillShader::Name, ComplexNonAARRectFillShader::MatchFromFacts},
    AOTKernelRow{EllipseFillShader::Name, EllipseFillShader::MatchFromFacts},
    AOTKernelRow{GaussianBlur1DShader::Name, GaussianBlur1DShader::MatchFromFacts},
    AOTKernelRow{GlassRefractionShader::Name, GlassRefractionShader::MatchFromFacts},
    AOTKernelRow{GlassUDFTentBlurShader::Name, GlassUDFTentBlurShader::MatchFromFacts},
    AOTKernelRow{HairlineLineShader::Name, HairlineLineShader::MatchFromFacts},
    AOTKernelRow{HairlineQuadShader::Name, HairlineQuadShader::MatchFromFacts},
    AOTKernelRow{MaskFillShader::Name, MaskFillShader::MatchFromFacts},
    AOTKernelRow{MeshFillShader::Name, MeshFillShader::MatchFromFacts},
    AOTKernelRow{NonAARRectFillShader::Name, NonAARRectFillShader::MatchFromFacts},
    AOTKernelRow{QuadColorFillShader::Name, QuadColorFillShader::MatchFromFacts},
    AOTKernelRow{QuadTextureFillShader::Name, QuadTextureFillShader::MatchFromFacts},
    AOTKernelRow{RoundStrokeRectFillShader::Name, RoundStrokeRectFillShader::MatchFromFacts},
    AOTKernelRow{ShapeInstancedFillShader::Name, ShapeInstancedFillShader::MatchFromFacts},
    AOTKernelRow{ShapeInstancedTextureCoverageShader::Name,
                 ShapeInstancedTextureCoverageShader::MatchFromFacts},
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
