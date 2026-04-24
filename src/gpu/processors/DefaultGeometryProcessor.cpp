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

#include "DefaultGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
DefaultGeometryProcessor::DefaultGeometryProcessor(PMColor color, int width, int height, AAType aa,
                                                   const Matrix& viewMatrix, const Matrix& uvMatrix)
    : GeometryProcessor(ClassID()), color(color), width(width), height(height), aa(aa),
      viewMatrix(viewMatrix), uvMatrix(uvMatrix) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 2);
}

void DefaultGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aa == AAType::Coverage ? 1 : 0;
  bytesKey->write(flags);
}

void DefaultGeometryProcessor::BuildMacros(AAType aa, ShaderMacroSet& macros) {
  if (aa == AAType::Coverage) {
    macros.define("TGFX_GP_DEFAULT_COVERAGE_AA");
  }
}

std::vector<ShaderVariant> DefaultGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(2);
  std::hash<std::string> hasher;
  // Iteration order: coverage-off at index 0, coverage-on at index 1. MSAA/None collapse to the
  // same shader (no coverage varying).
  const AAType aaValues[2] = {AAType::None, AAType::Coverage};
  for (int i = 0; i < 2; ++i) {
    ShaderMacroSet macros;
    BuildMacros(aaValues[i], macros);
    ShaderVariant variant;
    variant.index = i;
    variant.name = std::string("DefaultGP[coverageAA=") + (i == 1 ? "1" : "0") + "]";
    variant.preamble = macros.toPreamble();
    variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
    variants.emplace_back(std::move(variant));
  }
  return variants;
}
}  // namespace tgfx
