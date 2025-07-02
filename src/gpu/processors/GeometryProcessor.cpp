/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

/**
 * Returns the size of the attrib type in bytes.
 */
static constexpr size_t VertexAttribTypeSize(SLType type) {
  switch (type) {
    case SLType::Float:
      return sizeof(float);
    case SLType::Float2:
      return 2 * sizeof(float);
    case SLType::Float3:
      return 3 * sizeof(float);
    case SLType::Float4:
      return 4 * sizeof(float);
    case SLType::Int:
      return sizeof(int32_t);
    case SLType::Int2:
      return 2 * sizeof(int32_t);
    case SLType::Int3:
      return 3 * sizeof(int32_t);
    case SLType::Int4:
      return 4 * sizeof(int32_t);
    case SLType::UByte4Color:
      return 4 * sizeof(uint8_t);
    default:
      return 0;
  }
}

template <typename T>
static constexpr T Align4(T x) {
  return (x + 3) >> 2 << 2;
}

size_t GeometryProcessor::Attribute::sizeAlign4() const {
  return Align4(VertexAttribTypeSize(_gpuType));
}

void GeometryProcessor::computeProcessorKey(Context*, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  for (const auto* attribute : attributes) {
    attribute->computeKey(bytesKey);
  }
}

void GeometryProcessor::setVertexAttributes(const Attribute* attrs, int attrCount) {
  for (int i = 0; i < attrCount; ++i) {
    if (attrs[i].isInitialized()) {
      attributes.push_back(attrs + i);
    }
  }
}

void GeometryProcessor::setTransformDataHelper(const Matrix& uvMatrix, UniformBuffer* uniformBuffer,
                                               FPCoordTransformIter* transformIter) const {
  int i = 0;
  while (const CoordTransform* coordTransform = transformIter->next()) {
    Matrix combined = {};
    combined.setConcat(coordTransform->getTotalMatrix(), uvMatrix);
    std::string uniformName = TRANSFORM_UNIFORM_PREFIX;
    uniformName += std::to_string(i);
    uniformBuffer->setData(uniformName, combined);
    onSetTransformData(uniformBuffer, coordTransform, i);
    ++i;
  }
}

void GeometryProcessor::emitTransforms(VertexShaderBuilder* vertexBuilder,
                                       VaryingHandler* varyingHandler,
                                       UniformHandler* uniformHandler, const ShaderVar& uvCoordsVar,
                                       FPCoordTransformHandler* transformHandler) const {
  std::string uvCoords = "vec3(";
  uvCoords += uvCoordsVar.name();
  uvCoords += ", 1)";
  int i = 0;
  while (transformHandler->nextCoordTransform() != nullptr) {
    std::string strUniName = TRANSFORM_UNIFORM_PREFIX;
    strUniName += std::to_string(i);
    auto uniName = uniformHandler->addUniform(ShaderFlags::Vertex, SLType::Float3x3, strUniName);
    std::string strVaryingName = "TransformedCoords_";
    strVaryingName += std::to_string(i);
    SLType varyingType = SLType::Float2;
    auto varying = varyingHandler->addVarying(strVaryingName, varyingType);

    transformHandler->specifyCoordsForCurrCoordTransform(varying.name(), varyingType);

    vertexBuilder->codeAppendf("%s = (%s * %s).xy;", varying.vsOut().c_str(), uniName.c_str(),
                               uvCoords.c_str());
    ++i;
  }
}

}  // namespace tgfx
