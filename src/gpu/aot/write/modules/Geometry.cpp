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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/Geometry.h"
#include <array>
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/shaders/modules/AtlasTextGeometry.h"
#include "gpu/shaders/modules/ComplexEllipseGeometry.h"
#include "gpu/shaders/modules/ComplexRRectGeometry.h"
#include "gpu/shaders/modules/EllipseGeometry.h"
#include "gpu/shaders/modules/QuadColorGeometry.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/QuadTextureGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/RoundStrokeRectGeometry.h"

namespace tgfx {

namespace {

/// The colour triple the rounded/analytic shapes share: the paint colour, whether it is exact for
/// the whole draw, and whether the shape is stroked. Every such module declares the same three
/// fields, so the body is one template and every element index resolves at compile time.
template <const Module& module>
void WriteShapeColor(WriteContext& context, uint8_t colorElement, uint8_t hasCommonColorElement,
                     uint8_t strokeElement) {
  context.slot->set(colorElement, context.geometry.commonColor);
  context.slot->set(hasCommonColorElement,
                    static_cast<int32_t>(context.geometry.hasCommonColor ? 1 : 0));
  context.slot->set(strokeElement, static_cast<int32_t>(context.geometry.isStroke ? 1 : 0));
}

}  // namespace

// RectGeometry (the DefaultGP geometry): the view transform and the paint colour. The per-vertex
// coverage rides the varying and needs no write.
void WriteRectGeometryModule(WriteContext& context) {
  constexpr uint8_t Matrix = UniformElementIndexOf(RectGeometry, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(RectGeometry, "Color");
  context.slot->set(Matrix, context.geometry.viewMatrix);
  context.slot->set(Color, context.geometry.commonColor);
}

// QuadGeometry (the QuadPerEdgeAA geometry): the paint colour; the position is already in device
// space, so there is no view transform.
void WriteQuadGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadGeometry, "Color");
  context.slot->set(Color, context.geometry.commonColor);
}

// QuadColorGeometry: the exact paint colour and its flag; the per-vertex colour rides the varying.
void WriteQuadColorGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadColorGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(QuadColorGeometry, "HasCommonColor");
  context.slot->set(Color, context.geometry.commonColor);
  context.slot->set(HasCommonColor, static_cast<int32_t>(context.geometry.hasCommonColor ? 1 : 0));
}

// QuadTextureGeometry: the paint colour the texture source multiplies by.
void WriteQuadTextureGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadTextureGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(QuadTextureGeometry, "HasCommonColor");
  context.slot->set(Color, context.geometry.commonColor);
  context.slot->set(HasCommonColor, static_cast<int32_t>(context.geometry.hasCommonColor ? 1 : 0));
}

// RRectGeometry (the non-AA rounded rect): the exact common colour and its flag. The 8-bit
// quantized varying needs no write; the values come from the geometry processor.
void WriteRRectGeometryModule(WriteContext& context) {
  constexpr uint8_t CommonColor = UniformElementIndexOf(RRectGeometry, "CommonColor");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(RRectGeometry, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(RRectGeometry, "StrokeEnabled");
  WriteShapeColor<RRectGeometry>(context, CommonColor, HasCommonColor, StrokeEnabled);
}

// ComplexRRectGeometry: the colour pair. The corner data and the SDF run entirely on the varyings.
void WriteComplexRRectGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(ComplexRRectGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ComplexRRectGeometry, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(ComplexRRectGeometry, "StrokeEnabled");
  WriteShapeColor<ComplexRRectGeometry>(context, Color, HasCommonColor, StrokeEnabled);
}

// EllipseGeometry: the colour pair and the stroke selector; the ellipse data rides the varyings.
void WriteEllipseGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(EllipseGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(EllipseGeometry, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(EllipseGeometry, "StrokeEnabled");
  WriteShapeColor<EllipseGeometry>(context, Color, HasCommonColor, StrokeEnabled);
}

// ComplexEllipseGeometry: as above, with the complex form's own declaration.
void WriteComplexEllipseGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(ComplexEllipseGeometry, "Color");
  constexpr uint8_t HasCommonColor =
      UniformElementIndexOf(ComplexEllipseGeometry, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(ComplexEllipseGeometry, "StrokeEnabled");
  WriteShapeColor<ComplexEllipseGeometry>(context, Color, HasCommonColor, StrokeEnabled);
}

// RoundStrokeRectGeometry: the colour pair and the AA flag; the analytic clip is a separate module.
void WriteRoundStrokeRectGeometryModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(RoundStrokeRectGeometry, "Color");
  constexpr uint8_t HasCommonColor =
      UniformElementIndexOf(RoundStrokeRectGeometry, "HasCommonColor");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(RoundStrokeRectGeometry, "AAEnabled");
  context.slot->set(Color, context.geometry.commonColor);
  context.slot->set(HasCommonColor, static_cast<int32_t>(context.geometry.hasCommonColor ? 1 : 0));
  context.slot->set(AAEnabled,
                    static_cast<int32_t>(context.geometry.aaType == AAType::None ? 0 : 1));
}

// AtlasTextGeometry: the atlas size scale, the colour pair and the AlphaOnly switch. The atlas
// sample supplies the coverage itself, so the geometry needs no transform.
void WriteAtlasTextGeometryModule(WriteContext& context) {
  constexpr uint8_t AtlasSizeInv = UniformElementIndexOf(AtlasTextGeometry, "AtlasSizeInv");
  constexpr uint8_t Color = UniformElementIndexOf(AtlasTextGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(AtlasTextGeometry, "HasCommonColor");
  constexpr uint8_t AlphaOnly = UniformElementIndexOf(AtlasTextGeometry, "AlphaOnly");
  const auto& proxy = context.geometry.atlasProxy;
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view != nullptr && view->getTexture() != nullptr) {
    const auto scale = view->getTextureCoord(1.f, 1.f);
    context.slot->set(AtlasSizeInv, std::array<float, 2>{scale.x, scale.y});
  }
  context.slot->set(Color, context.geometry.commonColor);
  context.slot->set(HasCommonColor, static_cast<int32_t>(context.geometry.hasCommonColor ? 1 : 0));
  context.slot->set(AlphaOnly, static_cast<int32_t>(context.geometry.isAlphaOnly ? 1 : 0));
}

ModuleWriter GeometryWriterOf(const Module& module) {
  if (&module == &RectGeometry) {
    return WriteRectGeometryModule;
  }
  if (&module == &QuadGeometry) {
    return WriteQuadGeometryModule;
  }
  if (&module == &QuadColorGeometry) {
    return WriteQuadColorGeometryModule;
  }
  if (&module == &QuadTextureGeometry) {
    return WriteQuadTextureGeometryModule;
  }
  if (&module == &RRectGeometry) {
    return WriteRRectGeometryModule;
  }
  if (&module == &ComplexRRectGeometry) {
    return WriteComplexRRectGeometryModule;
  }
  if (&module == &EllipseGeometry) {
    return WriteEllipseGeometryModule;
  }
  if (&module == &ComplexEllipseGeometry) {
    return WriteComplexEllipseGeometryModule;
  }
  if (&module == &RoundStrokeRectGeometry) {
    return WriteRoundStrokeRectGeometryModule;
  }
  if (&module == &AtlasTextGeometry) {
    return WriteAtlasTextGeometryModule;
  }
  return nullptr;
}

}  // namespace tgfx
