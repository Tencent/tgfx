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

#include "GeometryProcessor.h"

namespace tgfx {
static constexpr char TRANSFORM_UNIFORM_PREFIX[] = "CoordTransformMatrix_";

void GeometryProcessor::computeProcessorKey(Context*, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  for (auto& attribute : attributes) {
    if (!attribute.empty()) {
      bytesKey->write(static_cast<uint32_t>(attribute.format()));
    }
  }
}

void GeometryProcessor::setVertexAttributes(const Attribute* attrs, int attrCount) {
  for (int i = 0; i < attrCount; ++i) {
    auto& attribute = attrs[i];
    if (!attribute.empty()) {
      attributes.push_back(attribute);
    }
  }
}

void GeometryProcessor::setTransformDataHelper(const Matrix& uvMatrix, UniformData* uniformData,
                                               FPCoordTransformIter* transformIter) const {
  int i = 0;
  while (const CoordTransform* coordTransform = transformIter->next()) {
    Matrix combined = {};
    combined.setConcat(coordTransform->getTotalMatrix(), uvMatrix);
    std::string uniformName = TRANSFORM_UNIFORM_PREFIX;
    uniformName += std::to_string(i);
    uniformData->setData(uniformName, combined);
    onSetTransformData(uniformData, coordTransform, i);
    ++i;
  }
}

void GeometryProcessor::emitTransforms(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                                       VaryingHandler* varyingHandler,
                                       UniformHandler* uniformHandler,
                                       const ShaderVar& uvCoordsVar) const {
  std::string uvCoords = "vec3(";
  uvCoords += uvCoordsVar.name();
  uvCoords += ", 1)";
  int i = 0;
  auto transformHandler = args.fpCoordTransformHandler;
  while (transformHandler->nextCoordTransform() != nullptr) {
    std::string strUniName = TRANSFORM_UNIFORM_PREFIX;
    strUniName += std::to_string(i);
    auto uniName =
        uniformHandler->addUniform(strUniName, UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string strVaryingName = "TransformedCoords_";
    strVaryingName += std::to_string(i);
    SLType varyingType = SLType::Float2;
    auto varying = varyingHandler->addVarying(strVaryingName, varyingType);
    transformHandler->specifyCoordsForCurrCoordTransform(varying.name(), varyingType);
    vertexBuilder->codeAppendf("%s = (%s * %s).xy;", varying.vsOut().c_str(), uniName.c_str(),
                               uvCoords.c_str());
    onEmitTransform(args, vertexBuilder, varyingHandler, uniformHandler, uniName, i);
    ++i;
  }
}

}  // namespace tgfx
