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

#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/resources/TextureView.h"
#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"

namespace tgfx {

GlassRefractionFragmentProcessor::GlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : FragmentProcessor(ClassID()), sourceProxy(std::move(source)), params(params),
      coordTransform(coordMatrix) {
  addCoordTransform(&coordTransform);
  geometryIndex = registerChildProcessor(std::move(geometry));
}

void GlassRefractionFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = 0;
  if (params.dispersion >= 0.01f) {
    flags |= 1;
  }
  if (params.lightIntensity > 0.0f) {
    flags |= 2;
  }
  bytesKey->write(flags);
}

size_t GlassRefractionFragmentProcessor::onCountTextureSamplers() const {
  return 1;
}

std::shared_ptr<Texture> GlassRefractionFragmentProcessor::onTextureAt(size_t) const {
  auto textureView = sourceProxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}

SamplerState GlassRefractionFragmentProcessor::onSamplerStateAt(size_t) const {
  return SamplerState(TileMode::Clamp, TileMode::Clamp);
}

bool GlassRefractionFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                                  AOTNodeID* output) const {
  // The refraction's math, its sampled source and its SDF/UDF geometry child all stay inside the
  // family's kernel — the node only marks the opaque barrier so the draw carries its IR (the
  // planner refuses the graph, the dedicated family serves it, and the matching reads the node
  // instead of walking the processor tree). The geometry child's kind and the dispersion branch
  // are compile-time variant dimensions, so they ride on the node; the chain input stays the
  // modulate operand, mirroring TextureSource.
  if (builder == nullptr || output == nullptr || numChildProcessors() != 1 ||
      numTextureSamplers() != 1) {
    return false;
  }
  AOTOpaqueKernelParameters parameters = {};
  parameters.kernel = AOTOpaqueKernel::GlassRefraction;
  auto child = childProcessor(0);
  if (IsProcessor<GlassSDFGeometryFragmentProcessor>(child)) {
    auto* sdf = static_cast<const GlassSDFGeometryFragmentProcessor*>(child);
    parameters.geometryKind = sdf->getShapeType() == GlassShapeType::RoundedRect
                                 ? AOTGlassGeometryKind::SdfRoundedRect
                                 : AOTGlassGeometryKind::SdfOther;
  } else if (IsProcessor<GlassUDFGeometryFragmentProcessor>(child)) {
    // The UDF kind always declares the edge-light mask sampler; an edge-lit-off draw binds the
    // shared dummy texture there and clears the EdgeMaskOn runtime uniform.
    parameters.geometryKind = AOTGlassGeometryKind::Udf;
  } else {
    return false;
  }
  parameters.dispersionOn = hasDispersion();
  parameters.hasPerspective = FragmentProcessor::numCoordTransforms() > 0 &&
                              FragmentProcessor::coordTransform(0)->matrix.hasPerspective();
  return builder->addOpaqueKernel(input, parameters, output);
}

}  // namespace tgfx
