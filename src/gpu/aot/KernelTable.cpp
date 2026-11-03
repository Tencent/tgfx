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
// 21st). Every row is a predicate over the matcher facts, whose expression source is the carried
// IR (the prepare-grown coverage is appended to its terms) plus the made GP/XP; a row and the
// walk it replaced share one body, so they cannot disagree.
static const std::vector<AOTKernelRow> rows = {
    AOTKernelRow{AtlasTextFillShader::Name, AtlasTextFillShader::MatchFromFacts, AtlasTextFillShader::ServesAlphaOnly},
    AOTKernelRow{ComplexEllipseFillShader::Name, ComplexEllipseFillShader::MatchFromFacts, ComplexEllipseFillShader::ServesAlphaOnly},
    AOTKernelRow{ComplexNonAARRectFillShader::Name, ComplexNonAARRectFillShader::MatchFromFacts, ComplexNonAARRectFillShader::ServesAlphaOnly},
    AOTKernelRow{EllipseFillShader::Name, EllipseFillShader::MatchFromFacts, EllipseFillShader::ServesAlphaOnly},
    AOTKernelRow{GaussianBlur1DShader::Name, GaussianBlur1DShader::MatchFromFacts, GaussianBlur1DShader::ServesAlphaOnly},
    AOTKernelRow{GlassRefractionShader::Name, GlassRefractionShader::MatchFromFacts, GlassRefractionShader::ServesAlphaOnly},
    AOTKernelRow{GlassUDFTentBlurShader::Name, GlassUDFTentBlurShader::MatchFromFacts, GlassUDFTentBlurShader::ServesAlphaOnly},
    AOTKernelRow{HairlineLineShader::Name, HairlineLineShader::MatchFromFacts, HairlineLineShader::ServesAlphaOnly},
    AOTKernelRow{HairlineQuadShader::Name, HairlineQuadShader::MatchFromFacts, HairlineQuadShader::ServesAlphaOnly},
    AOTKernelRow{MaskFillShader::Name, MaskFillShader::MatchFromFacts, MaskFillShader::ServesAlphaOnly},
    AOTKernelRow{MeshFillShader::Name, MeshFillShader::MatchFromFacts, MeshFillShader::ServesAlphaOnly},
    AOTKernelRow{NonAARRectFillShader::Name, NonAARRectFillShader::MatchFromFacts, NonAARRectFillShader::ServesAlphaOnly},
    AOTKernelRow{QuadColorFillShader::Name, QuadColorFillShader::MatchFromFacts, QuadColorFillShader::ServesAlphaOnly},
    AOTKernelRow{QuadTextureFillShader::Name, QuadTextureFillShader::MatchFromFacts, QuadTextureFillShader::ServesAlphaOnly},
    AOTKernelRow{RoundStrokeRectFillShader::Name, RoundStrokeRectFillShader::MatchFromFacts, RoundStrokeRectFillShader::ServesAlphaOnly},
    AOTKernelRow{ShapeInstancedFillShader::Name, ShapeInstancedFillShader::MatchFromFacts, ShapeInstancedFillShader::ServesAlphaOnly},
    AOTKernelRow{ShapeInstancedTextureCoverageShader::Name,
                 ShapeInstancedTextureCoverageShader::MatchFromFacts,
                 ShapeInstancedTextureCoverageShader::ServesAlphaOnly},
    AOTKernelRow{SolidColorFillShader::Name, SolidColorFillShader::MatchFromFacts, SolidColorFillShader::ServesAlphaOnly},
    AOTKernelRow{TexturedEffectShader::Name, TexturedEffectShader::MatchFromFacts, TexturedEffectShader::ServesAlphaOnly},
    AOTKernelRow{TiledTextureFillShader::Name, TiledTextureFillShader::MatchFromFacts, TiledTextureFillShader::ServesAlphaOnly},
    AOTKernelRow{YUVTextureFillShader::Name, YUVTextureFillShader::MatchFromFacts, YUVTextureFillShader::ServesAlphaOnly},
};

std::optional<AOTKernelMatch> KernelTableLookup(const AOTDrawFacts& facts,
                                                PermutationMatchFailure* failure) {
  for (const auto& row : rows) {
    if (facts.alphaOnlyTarget && !row.servesAlphaOnly) {
      continue;
    }
    if (auto variant = row.match(facts, failure)) {
      return AOTKernelMatch{&row, *variant};
    }
  }
  return std::nullopt;
}

}  // namespace tgfx
