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

#include "gpu/ir/ProgramIREmit.h"
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
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto& uvCoordsVar = uvCoord.empty() ? position : uvCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  std::string colorUniformName = {};
  Varying colorVar = {};
  Varying coverageVar = {};
  if (aa == AAType::Coverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
  }

  if (commonColor.has_value()) {
    colorUniformName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  } else {
    colorVar = varyingHandler->addVarying("Color", SLType::Float4);
  }

  // The IR route: the same varyings and outputs as SSA values, so the SPIR-V path serves this
  // geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    if (aa == AAType::Coverage) {
      emitVaryingIR(args, coverageVar, ShaderVar(coverage));
    }
    if (!commonColor.has_value()) {
      emitVaryingIR(args, colorVar, ShaderVar(color));
    }
    emitPositionIR(args, ShaderVar(position));
    ir->SetStage(ShaderStage::Fragment);
    if (aa == AAType::Coverage) {
      const auto coverageIn = ir->InternValue(coverageVar.fsIn(), SLType::Float, Precision::High,
                                              IRValueKind::Varying);
    emitOutputCoverageIR(ir, coverageIn, false, args.outputCoverage);
    } else {
      const auto one = ir->CreateConstant(1.0f);
    emitOutputCoverageIR(ir, one, false, args.outputCoverage);
    }
    if (commonColor.has_value()) {
      const auto colorValue = ir->InternValue(colorUniformName, SLType::Float4, Precision::High,
                                              IRValueKind::Uniform);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    } else {
    emitOutputColorIR(ir, colorVar.fsIn(), true, args.outputColor);
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

static ValueID TransformSubsetCorner(ProgramIRBuilder* ir, ValueID corner, ValueID matrix,
                                     bool hasPerspective) {
  const auto one = ir->CreateConstant(1.0f);
  const auto point = ir->CreateConstruct(SLType::Float3, {corner, one});
  const auto transformed = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float3, matrix, point);
  const auto xy = ir->CreateSwizzle(transformed, SLType::Float2, "xy");
  if (!hasPerspective) {
    return xy;
  }
  const auto z = ir->CreateSwizzle(transformed, SLType::Float, "z");
  return ir->CreateBinary(IRBinaryOp::Div, SLType::Float2, xy, z);
}

void GLSLQuadPerEdgeAAGeometryProcessor::onEmitTransform(EmitArgs& args,
                                                         VertexShaderBuilder* vertexBuilder,
                                                         VaryingHandler* varyingHandler,
                                                         UniformHandler* uniformHandler,
                                                         const std::string& transformUniformName,
                                                         bool hasPerspective, int index) const {
  if (index == 0 && !subset.empty()) {
    auto varying = varyingHandler->addVarying("vTexSubset", SLType::Float4, true);
    std::string subsetMatrixName = transformUniformName;
    if (uvCoord.empty()) {
      subsetMatrixName = uniformHandler->addUniform("texSubsetMatrix", UniformFormat::Float3x3,
                                                    ShaderStage::Vertex);
    }
    if (args.irBuilder != nullptr) {
      // The same corner transform the text calls spell out, so the subset varying has an IR
      // writer of its own: the IR route links the program by reading the varying back here.
      auto ir = args.irBuilder;
      const auto subsetValue = ir->InternValue(subset.name(), SLType::Float4, Precision::High,
                                               IRValueKind::Attribute);
      const auto matrix = ir->InternValue(subsetMatrixName, SLType::Float3x3, Precision::High,
                                          IRValueKind::Uniform);
      const auto leftTop =
          TransformSubsetCorner(ir, ir->CreateSwizzle(subsetValue, SLType::Float2, "xy"), matrix,
                                hasPerspective);
      const auto rightBottom =
          TransformSubsetCorner(ir, ir->CreateSwizzle(subsetValue, SLType::Float2, "zw"), matrix,
                                hasPerspective);
      const auto corners = ir->CreateConstruct(SLType::Float4, {leftTop, rightBottom});
      const auto low = ir->CreateBinary(IRBinaryOp::Min, SLType::Float2,
                                        ir->CreateSwizzle(corners, SLType::Float2, "xy"),
                                        ir->CreateSwizzle(corners, SLType::Float2, "zw"));
      const auto high = ir->CreateBinary(IRBinaryOp::Max, SLType::Float2,
                                         ir->CreateSwizzle(corners, SLType::Float2, "xy"),
                                         ir->CreateSwizzle(corners, SLType::Float2, "zw"));
      const auto normalized = ir->CreateConstruct(SLType::Float4, {low, high});
      const auto varyingValue = ir->AddVarying(
          {varying.name(), varying.type(), Precision::High, 0, varying.isFlat()});
      ir->StoreVarying({varyingValue, ""}, normalized);
    }
    const std::string srcLT = "srcLT";
    const std::string srcRB = "srcRB";
    const std::string perspLT = "perspLT";
    const std::string perspRB = "perspRB";
    vertexBuilder->swizzleVec2(srcLT, subset.name(), "xy");
    vertexBuilder->swizzleVec2(srcRB, subset.name(), "zw");
    vertexBuilder->transformPoint(perspLT, srcLT, subsetMatrixName, hasPerspective);
    vertexBuilder->transformPoint(perspRB, srcRB, subsetMatrixName, hasPerspective);
    vertexBuilder->composeVec4("subset", perspLT, perspRB);
    vertexBuilder->normalizeRect("subset");
    vertexBuilder->storeVarying(varying.vsOut(), "subset");
    *args.outputSubset = varying.fsIn();
  }
}

}  // namespace tgfx
