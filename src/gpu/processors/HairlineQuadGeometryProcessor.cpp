/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "HairlineQuadGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"
#include "tgfx/core/Color.h"

namespace tgfx {

HairlineQuadGeometryProcessor::HairlineQuadGeometryProcessor(const PMColor& color,
                                                             const Matrix& viewMatrix,
                                                             std::optional<Matrix> uvMatrix,
                                                             float coverage, AAType aaType)
    : GeometryProcessor(ClassID()), color(color), viewMatrix(viewMatrix), uvMatrix(uvMatrix),
      coverage(coverage), aaType(aaType) {
  position = {"aPosition", VertexFormat::Float2};
  hairQuadEdge = {"hairQuadEdge", VertexFormat::Float4};
  setVertexAttributes(&position, 2);
}

void HairlineQuadGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aaType == AAType::Coverage ? 1 : 0;
  bytesKey->write(flags);
}

void HairlineQuadGeometryProcessor::BuildMacros(AAType aaType, ShaderMacroSet& macros) {
  if (aaType == AAType::Coverage) {
    macros.define("TGFX_GP_HQUAD_COVERAGE_AA");
  }
}

std::vector<ShaderVariant> HairlineQuadGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(2);
  std::hash<std::string> hasher;
  const AAType aaValues[2] = {AAType::None, AAType::Coverage};
  for (int i = 0; i < 2; ++i) {
    ShaderMacroSet macros;
    BuildMacros(aaValues[i], macros);
    ShaderVariant variant;
    variant.index = i;
    variant.name = std::string("HairlineQuadGP[coverageAA=") + (i == 1 ? "1" : "0") + "]";
    variant.preamble = macros.toPreamble();
    variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
    variants.emplace_back(std::move(variant));
  }
  return variants;
}

}  // namespace tgfx
