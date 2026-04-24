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
    // Mirror the mangled uniform name into gpUniforms under the stable key
    // "CoordTransformMatrix_i" so that subclasses' buildVSCallExpr overrides can reference it
    // by a deterministic, variant-independent key. This is how QuadPerEdgeAA resolves its
    // subset matrix when it reuses the first FP coord transform.
    if (args.gpUniforms != nullptr) {
      args.gpUniforms->add(strUniName, uniName);
    }
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

std::string GeometryProcessor::buildCoordTransformCode(EmitArgs& args,
                                                       const std::string& uvCoordsExpr) const {
  auto* records = args.coordTransformRecords;
  if (records == nullptr || records->empty()) {
    return "";
  }
  std::string uvCoords = "vec3(" + uvCoordsExpr + ", 1.0)";
  std::string code;
  for (const auto& record : *records) {
    code += record.varyingVsOut;
    code += " = ";
    if (record.hasPerspective) {
      code += record.uniformName + " * " + uvCoords + ";\n";
    } else {
      code += "(" + record.uniformName + " * " + uvCoords + ").xy;\n";
    }
  }
  return code;
}

}  // namespace tgfx
