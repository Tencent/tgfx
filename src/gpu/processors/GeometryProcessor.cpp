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
  bytesKey->write(static_cast<uint32_t>(hasUVPerspective() ? 1 : 0));
  onComputeProcessorKey(bytesKey);
  for (auto& attribute : attributes) {
    if (!attribute.empty()) {
      bytesKey->write(static_cast<uint32_t>(attribute.format()));
    }
  }
  for (auto& attribute : _instanceAttributes) {
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

void GeometryProcessor::setInstanceAttributes(const Attribute* attrs, int attrCount) {
  for (int i = 0; i < attrCount; ++i) {
    auto& attribute = attrs[i];
    if (!attribute.empty()) {
      _instanceAttributes.push_back(attribute);
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

void GeometryProcessor::registerCoordTransforms(EmitArgs& args, VaryingHandler* varyingHandler,
                                                UniformHandler* uniformHandler) const {
  int i = 0;
  auto transformHandler = args.fpCoordTransformHandler;
  auto* records = args.coordTransformRecords;
  while (const CoordTransform* coordTransform = transformHandler->nextCoordTransform()) {
    std::string strUniName = TRANSFORM_UNIFORM_PREFIX;
    strUniName += std::to_string(i);
    auto uniName =
        uniformHandler->addUniform(strUniName, UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string strVaryingName = "TransformedCoords_";
    strVaryingName += std::to_string(i);
    const bool hasPerspective = hasUVPerspective() || coordTransform->matrix.hasPerspective();
    const SLType varyingType = hasPerspective ? SLType::Float3 : SLType::Float2;
    auto varying = varyingHandler->addVarying(strVaryingName, varyingType);
    transformHandler->specifyCoordsForCurrCoordTransform(varying.name(), varyingType);
    if (records != nullptr) {
      records->push_back({uniName, varying.vsOut(), hasPerspective, i});
    }
    ++i;
  }
}

void GeometryProcessor::emitCoordTransformCode(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                                               const std::string& uvCoordsExpr) const {
  auto* records = args.coordTransformRecords;
  if (records == nullptr) {
    return;
  }
  std::string uvCoords = "vec3(" + uvCoordsExpr + ", 1.0)";
  for (const auto& record : *records) {
    if (record.hasPerspective) {
      vertexBuilder->codeAppendf("%s = %s * %s;", record.varyingVsOut.c_str(),
                                 record.uniformName.c_str(), uvCoords.c_str());
    } else {
      vertexBuilder->codeAppendf("%s = (%s * %s).xy;", record.varyingVsOut.c_str(),
                                 record.uniformName.c_str(), uvCoords.c_str());
    }
    // Pass args.varyingHandler/uniformHandler so that GPs whose onEmitTransform still registers
    // resources (QuadPerEdgeAA's subset varying/uniform in particular) keep working. Moving that
    // registration into emitCode and tightening this contract to forbid phase-2 registration is
    // tracked as a follow-up cleanup.
    onEmitTransform(args, vertexBuilder, args.varyingHandler, args.uniformHandler,
                    record.uniformName, record.hasPerspective, record.index);
  }
}

}  // namespace tgfx
