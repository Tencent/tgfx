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

#include "RoundStrokeRectGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
RoundStrokeRectGeometryProcessor::RoundStrokeRectGeometryProcessor(
    AAType aaType, std::optional<PMColor> commonColor, std::optional<Matrix> uvMatrix)
    : GeometryProcessor(ClassID()), aaType(aaType), commonColor(commonColor), uvMatrix(uvMatrix) {
  inPosition = {"inPosition", VertexFormat::Float2};
  if (aaType == AAType::Coverage) {
    inCoverage = {"inCoverage", VertexFormat::Float};
  }
  inEllipseOffset = {"inEllipseOffset", VertexFormat::Float2};
  if (aaType == AAType::Coverage) {
    inEllipseRadii = {"inEllipseRadii", VertexFormat::Float2};
  }
  if (!uvMatrix.has_value()) {
    inUVCoord = {"inUVCoord", VertexFormat::Float2};
  }
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  setVertexAttributes(&inPosition, 6);
}

void RoundStrokeRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aaType == AAType::Coverage ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  flags |= uvMatrix.has_value() ? 4 : 0;
  bytesKey->write(flags);
}

void RoundStrokeRectGeometryProcessor::BuildMacros(bool coverageAA, bool commonColor,
                                                   bool hasUvMatrix, ShaderMacroSet& macros) {
  if (coverageAA) {
    macros.define("TGFX_GP_RRECT_COVERAGE_AA");
  }
  if (commonColor) {
    macros.define("TGFX_GP_RRECT_COMMON_COLOR");
  }
  if (hasUvMatrix) {
    macros.define("TGFX_GP_RRECT_UV_MATRIX");
  }
}

std::vector<ShaderVariant> RoundStrokeRectGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(8);
  std::hash<std::string> hasher;
  int index = 0;
  for (int aa = 0; aa < 2; ++aa) {
    for (int cc = 0; cc < 2; ++cc) {
      for (int uv = 0; uv < 2; ++uv) {
        ShaderMacroSet macros;
        BuildMacros(aa != 0, cc != 0, uv != 0, macros);
        ShaderVariant variant;
        variant.index = index++;
        variant.name = std::string("RoundStrokeRectGP[coverageAA=") + (aa ? "1" : "0") +
                       ",commonColor=" + (cc ? "1" : "0") + ",uvMatrix=" + (uv ? "1" : "0") + "]";
        variant.preamble = macros.toPreamble();
        variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
        variants.emplace_back(std::move(variant));
      }
    }
  }
  return variants;
}

}  // namespace tgfx
