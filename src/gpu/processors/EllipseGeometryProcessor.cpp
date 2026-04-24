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

#include "EllipseGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
EllipseGeometryProcessor::EllipseGeometryProcessor(int width, int height, bool stroke,
                                                   std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  inEllipseOffset = {"inEllipseOffset", VertexFormat::Float2};
  inEllipseRadii = {"inEllipseRadii", VertexFormat::Float4};
  this->setVertexAttributes(&inPosition, 4);
}

void EllipseGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = stroke ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  bytesKey->write(flags);
}

void EllipseGeometryProcessor::BuildMacros(bool stroke, bool commonColor, ShaderMacroSet& macros) {
  if (stroke) {
    macros.define("TGFX_GP_ELLIPSE_STROKE");
  }
  if (commonColor) {
    macros.define("TGFX_GP_ELLIPSE_COMMON_COLOR");
  }
}

std::vector<ShaderVariant> EllipseGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(4);
  std::hash<std::string> hasher;
  int index = 0;
  for (int strokeInt = 0; strokeInt < 2; ++strokeInt) {
    for (int commonColorInt = 0; commonColorInt < 2; ++commonColorInt) {
      ShaderMacroSet macros;
      BuildMacros(strokeInt != 0, commonColorInt != 0, macros);
      ShaderVariant variant;
      variant.index = index++;
      variant.name = std::string("EllipseGP[stroke=") + (strokeInt ? "1" : "0") +
                     ",commonColor=" + (commonColorInt ? "1" : "0") + "]";
      variant.preamble = macros.toPreamble();
      variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
      variants.emplace_back(std::move(variant));
    }
  }
  return variants;
}
}  // namespace tgfx
