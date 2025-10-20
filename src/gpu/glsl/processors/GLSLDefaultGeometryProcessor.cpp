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

#include "GLSLDefaultGeometryProcessor.h"

namespace tgfx {
PlacementPtr<DefaultGeometryProcessor> DefaultGeometryProcessor::Make(
    BlockBuffer* buffer, Color color, int width, int height, AAType aa, const Matrix& viewMatrix,
    const Matrix& uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) {
  return buffer->make<GLSLDefaultGeometryProcessor>(color, width, height, aa, viewMatrix, uvMatrix,
                                                    std::move(dstColorSpace));
}

GLSLDefaultGeometryProcessor::GLSLDefaultGeometryProcessor(
    Color color, int width, int height, AAType aa, const Matrix& viewMatrix, const Matrix& uvMatrix,
    std::shared_ptr<ColorSpace> dstColorSpace)
    : DefaultGeometryProcessor(color, width, height, aa, viewMatrix, uvMatrix,
                               std::move(dstColorSpace)) {
}

void GLSLDefaultGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      args.uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLDefaultGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix, vertexUniformData, transformIter);
  Color dstColor = color;
  ColorSpaceXformSteps steps{ColorSpace::MakeSRGB().get(), AlphaType::Premultiplied,
                             dstColorSpace.get(), AlphaType::Premultiplied};
  steps.apply(dstColor.array());
  fragmentUniformData->setData("Color", dstColor);
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
