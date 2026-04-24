/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLQuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
PlacementPtr<QuadPerEdgeAAGeometryProcessor> QuadPerEdgeAAGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset) {
  return allocator->make<GLSLQuadPerEdgeAAGeometryProcessor>(width, height, aa, commonColor,
                                                             uvMatrix, hasSubset);
}

GLSLQuadPerEdgeAAGeometryProcessor::GLSLQuadPerEdgeAAGeometryProcessor(
    int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset)
    : QuadPerEdgeAAGeometryProcessor(width, height, aa, commonColor, uvMatrix, hasSubset) {
}

void GLSLQuadPerEdgeAAGeometryProcessor::emitCode(EmitArgs& args) const {
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  registerCoordTransforms(args, varyingHandler, uniformHandler);

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageVar.fsIn());
    }
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorVar.fsIn());
    }
  }

  // Subset support: when a subset attribute is present, register the subset varying and the
  // (optional) subset transform uniform here, in phase 1, so that phase 2 (buildVSCallExpr) and
  // phase 3 (emitCoordTransformCode + onEmitTransform) never register new resources. The
  // `vTexSubset` varying is always written by onEmitTransform at coord-transform index 0, but it
  // must be declared up front because uniform/varying layout is finalized before the code phases
  // run.
  if (!subset.empty()) {
    auto subsetVarying = varyingHandler->addVarying("vTexSubset", SLType::Float4, true);
    // Stash the subset varying's fsIn name into EmitArgs::outputSubset so the TextureEffect FP can
    // reference it from the fragment shader. This channel is the GP->FP contract for subset.
    *args.outputSubset = subsetVarying.fsIn();
    // Remember the vsOut name for phase 3 code emission.
    if (args.gpVaryings) {
      args.gpVaryings->add("vTexSubset", subsetVarying.vsOut());
    }
    // When no FP coord transform is available for the subset (i.e. the GP owns its own uvMatrix),
    // register a dedicated texSubsetMatrix uniform. Otherwise phase 3 will reuse
    // CoordTransformMatrix_0, which is registered by registerCoordTransforms() above.
    if (uvCoord.empty()) {
      auto subsetMatrixName = uniformHandler->addUniform("texSubsetMatrix", UniformFormat::Float3x3,
                                                         ShaderStage::Vertex);
      if (args.gpUniforms) {
        args.gpUniforms->add("texSubsetMatrix", subsetMatrixName);
      }
    }
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::setData(UniformData* vertexUniformData,
                                                 UniformData* fragmentUniformData,
                                                 FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::onSetTransformData(UniformData* uniformData,
                                                            const CoordTransform* coordTransform,
                                                            int index) const {
  if (index == 0 && !subset.empty() && uvCoord.empty()) {
    // Subset only applies to the first image in ProgramInfo.
    uniformData->setData("texSubsetMatrix", coordTransform->getTotalMatrix());
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::onEmitTransform(EmitArgs& args,
                                                         VertexShaderBuilder* vertexBuilder,
                                                         VaryingHandler* /*varyingHandler*/,
                                                         UniformHandler* /*uniformHandler*/,
                                                         const std::string& transformUniformName,
                                                         bool hasPerspective, int index) const {
  if (index != 0 || subset.empty()) {
    return;
  }
  // Emit a single call to either TGFX_QuadAA_ComputeSubset or the perspective variant. Both
  // helpers live in quad_aa_geometry.vert.glsl and perform the full axis-aligned bounds
  // computation, so no GLSL logic is assembled here — only the function name is selected by
  // C++ based on whether the subset matrix may contain a perspective row. This matches the
  // existing program-key encoding (perspective vs. affine produce different cached programs).
  auto subsetMatrixName =
      uvCoord.empty() ? args.gpUniforms->get("texSubsetMatrix") : transformUniformName;
  auto subsetVaryingVsOut = args.gpVaryings->get("vTexSubset");
  const char* computeFn =
      hasPerspective ? "TGFX_QuadAA_ComputeSubsetPersp" : "TGFX_QuadAA_ComputeSubset";
  vertexBuilder->codeAppendf("%s = %s(%s, %s);", subsetVaryingVsOut.c_str(), computeFn,
                             subset.name().c_str(), subsetMatrixName.c_str());
}

}  // namespace tgfx
