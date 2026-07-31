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

namespace tgfx {

// Returns the shader expression for sampling a child at a coord offset by the tent kernel.
// "offset" and "offsetValue" are shader variables declared in the emitted code.
static std::string TentOffsetCoordFunc(std::string_view coord) {
  return "(" + std::string(coord) + " + offset * offsetValue)";
}

PlacementPtr<FragmentProcessor> GlassUDFTentBlurFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> fineSource,
    PlacementPtr<FragmentProcessor> coarseSource, float fineRadius, float coarseRadius,
    GlassUDFBlurDirection direction, int maxRadius, bool inputIsPacked) {
  if (allocator == nullptr || fineSource == nullptr || coarseSource == nullptr) {
    return nullptr;
  }
  if (maxRadius < 1 || fineRadius <= 0.0f || coarseRadius <= 0.0f) {
    return nullptr;
  }
  return allocator->make<GlassUDFTentBlurFragmentProcessor>(std::move(fineSource),
                                                            std::move(coarseSource), fineRadius,
                                                            coarseRadius, direction, maxRadius,
                                                            inputIsPacked);
}

GlassUDFTentBlurFragmentProcessor::GlassUDFTentBlurFragmentProcessor(
    PlacementPtr<FragmentProcessor> fineSource, PlacementPtr<FragmentProcessor> coarseSource,
    float fineRadius, float coarseRadius, GlassUDFBlurDirection direction, int maxRadius,
    bool inputIsPacked)
    : FragmentProcessor(ClassID()), fineRadius(fineRadius), coarseRadius(coarseRadius),
      direction(direction), maxRadius(maxRadius), inputIsPacked(inputIsPacked) {
  registerChildProcessor(std::move(fineSource));
  registerChildProcessor(std::move(coarseSource));
}

void GlassUDFTentBlurFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxRadius);
  key->write(static_cast<uint32_t>(inputIsPacked));
}

void GlassUDFTentBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto radiusName = args.uniformHandler->addUniform("GlassUDFRadius", UniformFormat::Float2,
                                                    ShaderStage::Fragment);
  auto stepName =
      args.uniformHandler->addUniform("GlassUDFStep", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", stepName.c_str());
  fragBuilder->codeAppend("const vec3 UNPACK24 = vec3(1.0, 1.0/255.0, 1.0/65025.0);");

  // Pairwise bilinear sampling: adjacent taps (k, k+1) collapse into one fetch because the tent
  // kernel weights are linear, so this is exact. Sample count: 2R+1 -> R+1.
  // Loop mapping: j=0 -> center, j=1,2 -> +/-pair(k=1), j=3,4 -> +/-pair(k=3), ...
  // When w2 == 0 (unpaired tail) the combined weight degrades to w1 with offset k.
  auto emitTentLoop = [&](size_t childIndex, const char* radiusSwizzle, const char* sumName,
                          bool decodePacked) {
    fragBuilder->codeAppendf("float %s = 0.0;", sumName);
    fragBuilder->codeAppend("{");
    fragBuilder->codeAppendf("float radius = %s.%s;", radiusName.c_str(), radiusSwizzle);
    fragBuilder->codeAppend("float total = 0.0;");
    fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", maxRadius);
    fragBuilder->codeAppend("float offsetValue;");
    fragBuilder->codeAppend("float weight;");
    fragBuilder->codeAppend("if (j == 0) {");
    fragBuilder->codeAppend("  offsetValue = 0.0;");
    fragBuilder->codeAppend("  weight = radius;");
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppend("  int k = 2 * ((j - 1) / 2) + 1;");
    fragBuilder->codeAppend("  float s = (j % 2 == 1) ? 1.0 : -1.0;");
    fragBuilder->codeAppend("  float w1 = max(0.0, radius - float(k));");
    fragBuilder->codeAppend("  float w2 = max(0.0, radius - float(k + 1));");
    fragBuilder->codeAppend("  weight = w1 + w2;");
    fragBuilder->codeAppend("  offsetValue = (weight > 0.0) ? s * (float(k) + w2 / weight) : 0.0;");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("if (weight <= 0.0) { break; }");
    fragBuilder->codeAppend("total += weight;");
    std::string tempColor = "tempColor";
    emitChild(childIndex, &tempColor, args, TentOffsetCoordFunc);
    if (decodePacked) {
      fragBuilder->codeAppendf("%s += dot(%s.rgb, UNPACK24) * weight;", sumName,
                               tempColor.c_str());
    } else {
      fragBuilder->codeAppendf("%s += %s.a * weight;", sumName, tempColor.c_str());
    }
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppendf("%s /= total;", sumName);
    fragBuilder->codeAppend("}");
  };

  // The coarse field always lives in the alpha channel, both in the source coverage image and in
  // this processor's own layout, so it never needs the 24-bit decode path.
  emitTentLoop(0, "x", "fineResult", inputIsPacked);
  emitTentLoop(1, "y", "coarseResult", false);

  // Encode the fine field into RGB with true 24-bit precision. The fract chain pre-compensates the
  // 8-bit rounding of the following channel; a floor-based split would mismatch the GPU's
  // round-to-nearest UNORM8 conversion. The upper clamp is required because fract(1.0) == 0.0 and
  // interior UDF samples are exactly 1.0.
  fragBuilder->codeAppend("float fineValue = clamp(fineResult, 0.0, 1.0 - 1.0/16581375.0);");
  fragBuilder->codeAppend("vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * fineValue);");
  fragBuilder->codeAppend("enc.x -= enc.y / 255.0;");
  fragBuilder->codeAppend("enc.y -= enc.z / 255.0;");
  fragBuilder->codeAppendf("%s = vec4(enc, clamp(coarseResult, 0.0, 1.0));",
                           args.outputColor.c_str());
}

void GlassUDFTentBlurFragmentProcessor::onSetData(UniformData*,
                                                  UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {1.0f, 0}};
  if (direction == GlassUDFBlurDirection::Vertical) {
    stepVectors[1] = {0, 1.0f};
  }
  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto matrix = processor->coordTransform(0)->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  Point radii = {fineRadius, coarseRadius};
  fragmentUniformData->setData("GlassUDFRadius", radii);
  fragmentUniformData->setData("GlassUDFStep", step);
}

}  // namespace tgfx
