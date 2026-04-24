/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "MeshGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {

MeshGeometryProcessor::MeshGeometryProcessor(bool hasTexCoords, bool hasColors, bool hasCoverage,
                                             PMColor color, const Matrix& viewMatrix)
    : GeometryProcessor(ClassID()), hasTexCoords(hasTexCoords), hasColors(hasColors),
      hasCoverage(hasCoverage), commonColor(color), viewMatrix(viewMatrix) {
  position = {"aPosition", VertexFormat::Float2};
  if (hasTexCoords) {
    texCoord = {"aTexCoord", VertexFormat::Float2};
  }
  if (hasColors) {
    this->color = {"aColor", VertexFormat::UByte4Normalized};
  }
  if (hasCoverage) {
    coverage = {"aCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 4);
}

void MeshGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = 0;
  if (hasTexCoords) {
    flags |= 1;
  }
  if (hasColors) {
    flags |= 2;
  }
  if (hasCoverage) {
    flags |= 4;
  }
  bytesKey->write(flags);
}

void MeshGeometryProcessor::BuildMacros(bool hasTexCoords, bool hasColors, bool hasCoverage,
                                        ShaderMacroSet& macros) {
  if (hasTexCoords) {
    macros.define("TGFX_GP_MESH_TEX_COORDS");
  }
  if (hasColors) {
    macros.define("TGFX_GP_MESH_VERTEX_COLORS");
  }
  if (hasCoverage) {
    macros.define("TGFX_GP_MESH_VERTEX_COVERAGE");
  }
}

std::vector<ShaderVariant> MeshGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(8);
  std::hash<std::string> hasher;
  int index = 0;
  for (int tc = 0; tc < 2; ++tc) {
    for (int colors = 0; colors < 2; ++colors) {
      for (int coverage = 0; coverage < 2; ++coverage) {
        ShaderMacroSet macros;
        BuildMacros(tc != 0, colors != 0, coverage != 0, macros);
        ShaderVariant variant;
        variant.index = index++;
        variant.name = std::string("MeshGP[texCoords=") + (tc ? "1" : "0") +
                       ",colors=" + (colors ? "1" : "0") + ",coverage=" + (coverage ? "1" : "0") +
                       "]";
        variant.preamble = macros.toPreamble();
        variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
        variants.emplace_back(std::move(variant));
      }
    }
  }
  return variants;
}

}  // namespace tgfx
