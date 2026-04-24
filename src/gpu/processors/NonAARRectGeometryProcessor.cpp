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

#include "NonAARRectGeometryProcessor.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
NonAARRectGeometryProcessor::NonAARRectGeometryProcessor(int width, int height, bool stroke,
                                                         std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  inLocalCoord = {"inLocalCoord", VertexFormat::Float2};
  inRadii = {"inRadii", VertexFormat::Float2};
  inRectBounds = {"inRectBounds", VertexFormat::Float4};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  if (stroke) {
    inStrokeWidth = {"inStrokeWidth", VertexFormat::Float2};
  }
  this->setVertexAttributes(&inPosition, 6);
}

void NonAARRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = commonColor.has_value() ? 1 : 0;
  flags |= stroke ? 2 : 0;
  bytesKey->write(flags);
}

void NonAARRectGeometryProcessor::BuildMacros(bool commonColor, bool stroke,
                                              ShaderMacroSet& macros) {
  if (commonColor) {
    macros.define("TGFX_GP_NONAA_COMMON_COLOR");
  }
  if (stroke) {
    macros.define("TGFX_GP_NONAA_STROKE");
  }
}

std::vector<ShaderVariant> NonAARRectGeometryProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(4);
  std::hash<std::string> hasher;
  int index = 0;
  for (int cc = 0; cc < 2; ++cc) {
    for (int stroke = 0; stroke < 2; ++stroke) {
      ShaderMacroSet macros;
      BuildMacros(cc != 0, stroke != 0, macros);
      ShaderVariant variant;
      variant.index = index++;
      variant.name = std::string("NonAARRectGP[commonColor=") + (cc ? "1" : "0") +
                     ",stroke=" + (stroke ? "1" : "0") + "]";
      variant.preamble = macros.toPreamble();
      variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
      variants.emplace_back(std::move(variant));
    }
  }
  return variants;
}
}  // namespace tgfx
