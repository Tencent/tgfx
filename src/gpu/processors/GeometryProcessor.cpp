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
#include "gpu/ir/CoordinateBinder.h"

namespace tgfx {

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
    uniformData->setData(CoordinateBinder::MatrixUniformName(static_cast<size_t>(i)),
                         CoordinateBinder::CombinedMatrix(*coordTransform, uvMatrix));
    onSetTransformData(uniformData, coordTransform, i);
    ++i;
  }
}

void GeometryProcessor::emitTransforms(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                                       VaryingHandler* varyingHandler,
                                       UniformHandler* uniformHandler,
                                       const ShaderVar& uvCoordsVar) const {
  int i = 0;
  auto transformHandler = args.fpCoordTransformHandler;
  while (const CoordTransform* coordTransform = transformHandler->nextCoordTransform()) {
    auto uniName =
        uniformHandler->addUniform(CoordinateBinder::MatrixUniformName(static_cast<size_t>(i)),
                                   UniformFormat::Float3x3, ShaderStage::Vertex);
    // The final UV transform combines GP's uvMatrix and FP's coordTransform->getTotalMatrix().
    // We use coordTransform->matrix here because getTotalMatrix() preserves its perspective property.
    const bool hasPerspective =
        CoordinateBinder::HasPerspective(hasUVPerspective(), *coordTransform);
    const SLType varyingType = CoordinateBinder::VaryingType(hasPerspective);
    auto varying = varyingHandler->addVarying(CoordinateBinder::VaryingName(static_cast<size_t>(i)),
                                              varyingType);
    transformHandler->specifyCoordsForCurrCoordTransform(varying.name(), varyingType);
    // For perspective: pass vec3 directly, division happens in fragment shader.
    // For non-perspective: compute vec2 directly as an optimization.
    vertexBuilder->transformVarying(varying.vsOut(), uvCoordsVar.name(), uniName, hasPerspective);
    onEmitTransform(args, vertexBuilder, varyingHandler, uniformHandler, uniName, hasPerspective,
                    i);
    ++i;
  }
}

}  // namespace tgfx
