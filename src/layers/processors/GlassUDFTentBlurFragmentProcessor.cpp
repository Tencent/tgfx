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

#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "core/utils/Log.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

// Returns the shader expression for sampling a child at a coord offset by the tent kernel.
// "offset" and "offsetValue" are shader variables declared in the emitted code.
PlacementPtr<FragmentProcessor> GlassUDFTentBlurFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> fineSource,
    PlacementPtr<FragmentProcessor> coarseSource, float fineRadius, float coarseRadius,
    GlassUDFBlurDirection direction, int maxRadius, bool inputIsPacked, GlassUDFField field) {
  if (allocator == nullptr) {
    return nullptr;
  }
  bool usesFine = field == GlassUDFField::Refraction;
  if (usesFine && (fineSource == nullptr || fineRadius <= 0.0f)) {
    return nullptr;
  }
  if (!usesFine && (coarseSource == nullptr || coarseRadius <= 0.0f)) {
    return nullptr;
  }
  if (maxRadius < 1) {
    return nullptr;
  }
  return allocator->make<GlassUDFTentBlurFragmentProcessor>(
      std::move(fineSource), std::move(coarseSource), fineRadius, coarseRadius, direction,
      maxRadius, inputIsPacked, field);
}

GlassUDFTentBlurFragmentProcessor::GlassUDFTentBlurFragmentProcessor(
    PlacementPtr<FragmentProcessor> fineSource, PlacementPtr<FragmentProcessor> coarseSource,
    float fineRadius, float coarseRadius, GlassUDFBlurDirection direction, int maxRadius,
    bool inputIsPacked, GlassUDFField field)
    : FragmentProcessor(ClassID()), fineRadius(fineRadius), coarseRadius(coarseRadius),
      direction(direction), maxRadius(maxRadius), inputIsPacked(inputIsPacked), field(field) {
  // The lowered node reads its source off child 0 (see lowerToAOT), so the active source is the
  // one registered child either way.
  if (field == GlassUDFField::EdgeLight) {
    registerChildProcessor(std::move(coarseSource));
  } else {
    registerChildProcessor(std::move(fineSource));
  }
}

// Returns the shader expression for sampling the child at a coord offset by the tent kernel.
static std::string TentBlurOffsetCoordFunc(std::string_view coord) {
  return "(" + std::string(coord) + " + offset * offsetValue)";
}

void GlassUDFTentBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;

  std::string radiusName =
      args.uniformHandler->addUniform("Radius", UniformFormat::Float, ShaderStage::Fragment);
  std::string texelSizeName =
      args.uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());

  fragBuilder->codeAppendf("float radius = %s;", radiusName.c_str());
  fragBuilder->codeAppend("float sum = 0.0;");
  fragBuilder->codeAppend("float total = 0.0;");

  // RGBA8 unpack constants: dot(rgba, UNPACK) recovers the original float.
  fragBuilder->codeAppend("const vec4 UNPACK = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);");

  // Pairwise bilinear sampling: combine adjacent positions (k, k+1) into a single texture fetch
  // via hardware bilinear filtering. The tent kernel's linear weights make this exact. The fixed
  // 64-iteration upper bound satisfies GLSL ES 1.0's constant-loop constraint; the actual exit is
  // the weight<=0 break at radius+1 iterations.
  fragBuilder->codeAppend("int k = 1;");
  fragBuilder->codeAppend("float s = 1.0;");
  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", maxRadius);
  fragBuilder->codeAppend("float offsetValue;");
  fragBuilder->codeAppend("float weight;");
  fragBuilder->codeAppend("if (j == 0) {");
  fragBuilder->codeAppend("  offsetValue = 0.0;");
  fragBuilder->codeAppend("  weight = radius;");
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("  float w1 = max(0.0, radius - float(k));");
  fragBuilder->codeAppend("  float w2 = max(0.0, radius - float(k + 1));");
  fragBuilder->codeAppend("  weight = w1 + w2;");
  fragBuilder->codeAppend("  offsetValue = (weight > 0.0) ? s * (float(k) + w2 / weight) : 0.0;");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("if (weight <= 0.0) { break; }");
  fragBuilder->codeAppend("total += weight;");

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, TentBlurOffsetCoordFunc);

  // Unpack sample: the fine field's packed input decodes RGBA8 to a float; the coarse field (and
  // the fine field's first pass) reads the alpha channel.
  if (field == GlassUDFField::Refraction && inputIsPacked) {
    fragBuilder->codeAppendf("sum += dot(%s, UNPACK) * weight;", tempColor.c_str());
  } else {
    fragBuilder->codeAppendf("sum += %s.a * weight;", tempColor.c_str());
  }
  fragBuilder->codeAppend("if (j > 0) {");
  fragBuilder->codeAppend("  if (s < 0.0) { k += 2; }");
  fragBuilder->codeAppend("  s = -s;");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppend("float result = sum / total;");
  if (field == GlassUDFField::EdgeLight) {
    // The coarse field carries one 8-bit value in alpha.
    fragBuilder->codeAppendf("%s = vec4(0.0, 0.0, 0.0, clamp(result, 0.0, 1.0));",
                             args.outputColor.c_str());
  } else {
    // Encode the fine field into RGB with true 24-bit precision; the fract chain pre-compensates
    // the 8-bit rounding of the following channel. The upper clamp is required because fract(1.0)
    // == 0.0 and interior UDF samples are exactly 1.0.
    fragBuilder->codeAppend("float fineValue = clamp(result, 0.0, 1.0 - 1.0 / 16581375.0);");
    fragBuilder->codeAppend("vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * fineValue);");
    fragBuilder->codeAppend("enc.x -= enc.y / 255.0;");
    fragBuilder->codeAppend("enc.y -= enc.z / 255.0;");
    fragBuilder->codeAppendf("%s = vec4(enc, 0.0);", args.outputColor.c_str());
  }
}

void GlassUDFTentBlurFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                  UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {1, 0}};
  if (direction == GlassUDFBlurDirection::Vertical) {
    stepVectors[1] = {0, 1};
  }

  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto transform = processor->coordTransform(0);
  auto matrix = transform->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  fragmentUniformData->setData("Radius", field == GlassUDFField::Refraction ? fineRadius
                                                                            : coarseRadius);
  fragmentUniformData->setData("Step", step);
}

bool GlassUDFTentBlurFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                                   AOTNodeID* output) const {
  // Same contract as the gaussian blur's opaque node: the active source child lowers into the
  // graph (a texture leaf — the horizontal pass reads a plain TextureEffect, the vertical a
  // TiledTextureEffect), everything else stays inside the family's kernel. The matcher reads
  // the child's servable shape and the radius cap off the node.
  if (builder == nullptr || output == nullptr || numChildProcessors() != 1) {
    return false;
  }
  AOTNodeID childNode = AOTNodeID::Invalid();
  if (!childProcessor(0)->lowerToAOT(builder, input, &childNode)) {
    return false;
  }
  AOTOpaqueKernelParameters parameters = {};
  parameters.kernel = AOTOpaqueKernel::GlassUDFTentBlur;
  parameters.maxRadius = maxRadius;
  parameters.tentFineRadius = fineRadius;
  parameters.tentCoarseRadius = coarseRadius;
  parameters.tentField = static_cast<int>(field);
  parameters.tentInputIsPacked = inputIsPacked ? 1 : 0;
  parameters.tentVertical = direction == GlassUDFBlurDirection::Vertical;
  return builder->addOpaqueKernel(childNode, parameters, output);
}

void GlassUDFTentBlurFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxRadius);
  key->write(static_cast<uint32_t>(field));
  key->write(static_cast<uint32_t>(inputIsPacked));
}

}  // namespace tgfx
