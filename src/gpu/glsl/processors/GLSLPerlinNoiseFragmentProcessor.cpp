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

#include "GLSLPerlinNoiseFragmentProcessor.h"
#include <unordered_map>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/processors/AOTPointwiseSlotWriter.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

namespace {
// Emits one pointwise operator on the output color, mirroring AOTPointwiseTailProcessor's slot
// emission. The empty prefix addresses the kernel's shared OpType record (slot 0); slot 1 uses the
// "Slot1" prefix declared by perlin_noise.frag.
void EmitPointwiseSlot(FragmentProcessor::EmitArgs& args,
                       std::unordered_map<std::string, std::string>& declaredArrays, size_t index,
                       const AOTPointwiseSlot& slot) {
  // Per-slot uniform fields are std140 arrays shared with the precompiled kernel; declare each
  // array once and reference its elements by slot index.
  auto declareSlotField = [&](const char* field, UniformFormat format) {
    auto [it, _] = declaredArrays.try_emplace(
        field,
        args.uniformHandler->addUniform(field, format, ShaderStage::Fragment,
                                        GLSLPerlinNoiseFragmentProcessor::MaxPointwiseSlots));
    return it->second + "[" + std::to_string(index) + "]";
  };
  if (slot.type == AOTPointwiseOpType::ColorMatrix) {
    auto matrix = declareSlotField("ColorMatrix", UniformFormat::Float4x4);
    auto vector = declareSlotField("ColorVector", UniformFormat::Float4);
    args.fragBuilder->codeAppendf("%s = vec4(%s.rgb / max(%s.a, 9.9999997473787516e-05), %s.a);",
                                  args.outputColor.c_str(), args.outputColor.c_str(),
                                  args.outputColor.c_str(), args.outputColor.c_str());
    args.fragBuilder->codeAppendf("%s = clamp(%s * %s + %s, 0.0, 1.0);", args.outputColor.c_str(),
                                  matrix.c_str(), args.outputColor.c_str(), vector.c_str());
    args.fragBuilder->codeAppendf("%s.rgb *= %s.a;", args.outputColor.c_str(),
                                  args.outputColor.c_str());
  } else if (slot.type == AOTPointwiseOpType::Luma) {
    auto kr = declareSlotField("Kr", UniformFormat::Float);
    auto kg = declareSlotField("Kg", UniformFormat::Float);
    auto kb = declareSlotField("Kb", UniformFormat::Float);
    auto luma = "perlinSlotLuma" + std::to_string(index);
    args.fragBuilder->codeAppendf("float %s = dot(%s.rgb, vec3(%s, %s, %s));", luma.c_str(),
                                  args.outputColor.c_str(), kr.c_str(), kg.c_str(), kb.c_str());
    args.fragBuilder->codeAppendf("%s = vec4(%s);", args.outputColor.c_str(), luma.c_str());
  } else if (slot.type == AOTPointwiseOpType::AlphaThreshold) {
    auto threshold = declareSlotField("Threshold", UniformFormat::Float);
    auto stepped = "perlinSlotStep" + std::to_string(index);
    args.fragBuilder->codeAppendf("vec4 %s = vec4(0.0);", stepped.c_str());
    args.fragBuilder->codeAppendf("if (%s.a > 0.0) {", args.outputColor.c_str());
    args.fragBuilder->codeAppendf("  %s.rgb = %s.rgb / %s.a;", stepped.c_str(),
                                  args.outputColor.c_str(), args.outputColor.c_str());
    args.fragBuilder->codeAppendf("  %s.a = step(%s, %s.a);", stepped.c_str(), threshold.c_str(),
                                  args.outputColor.c_str());
    args.fragBuilder->codeAppendf("  %s = clamp(%s, 0.0, 1.0);", stepped.c_str(), stepped.c_str());
    args.fragBuilder->codeAppend("}");
    args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), stepped.c_str());
  } else if (slot.type == AOTPointwiseOpType::ColorSpaceXform) {
    ColorSpaceXformHelper helper(static_cast<int>(index),
                                      GLSLPerlinNoiseFragmentProcessor::MaxPointwiseSlots);
    auto steps = slot.colorSpaceXform.steps.get();
    helper.emitCode(args.uniformHandler, steps);
    std::string transformed;
    args.fragBuilder->appendColorGamutXform(&transformed, args.outputColor.c_str(), &helper);
    args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), transformed.c_str());
  }
}

}  // namespace

PlacementPtr<PerlinNoiseFragmentProcessor> PerlinNoiseFragmentProcessor::Make(
    BlockAllocator* allocator, Context* context, PerlinNoiseType noiseType, int numOctaves,
    bool stitchTiles, std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    const Matrix* uvMatrix) {
  if (paintingData == nullptr) {
    return nullptr;
  }

  // Upload permutations texture: 256x1 A8.
  auto permutationsView = TextureView::MakeAlpha(
      context, PerlinNoiseShader::BLOCK_SIZE, 1, paintingData->latticeSelector,
      static_cast<size_t>(PerlinNoiseShader::BLOCK_SIZE), false);
  if (permutationsView == nullptr) {
    return nullptr;
  }

  // Upload noise texture: 256x4 RGBA8888.
  // noise[4][256][2] is uint16_t array. Each row is one channel (R/G/B/A of the output noise).
  // Each uint16_t pair [gradX, gradY] maps to one RGBA pixel:
  //   R = low8(gradX), G = high8(gradX), B = low8(gradY), A = high8(gradY)
  auto noiseView =
      TextureView::MakeRGBA(context, PerlinNoiseShader::BLOCK_SIZE, 4,
                            reinterpret_cast<const uint8_t*>(&paintingData->noise[0][0][0]),
                            static_cast<size_t>(PerlinNoiseShader::BLOCK_SIZE) * 4, false);
  if (noiseView == nullptr) {
    return nullptr;
  }

  return allocator->make<GLSLPerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permutationsView),
      std::move(noiseView), uvMatrix, std::vector<AOTPointwiseSlot>{});
}

PlacementPtr<PerlinNoiseFragmentProcessor> PerlinNoiseFragmentProcessor::MakeFromViews(
    BlockAllocator* allocator, PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix, const std::vector<AOTPointwiseSlot>& slots) {
  if (paintingData == nullptr || permutationsView == nullptr || noiseView == nullptr ||
      slots.size() > MaxPointwiseSlots) {
    return nullptr;
  }
  for (const auto& slot : slots) {
    if (slot.type == AOTPointwiseOpType::None ||
        (slot.type == AOTPointwiseOpType::ColorSpaceXform &&
         slot.colorSpaceXform.steps == nullptr)) {
      return nullptr;
    }
  }
  return allocator->make<GLSLPerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permutationsView),
      std::move(noiseView), uvMatrix, slots);
}

GLSLPerlinNoiseFragmentProcessor::GLSLPerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix, const std::vector<AOTPointwiseSlot>& slots)
    : PerlinNoiseFragmentProcessor(noiseType, numOctaves, stitchTiles, std::move(paintingData),
                                   std::move(permutationsView), std::move(noiseView), uvMatrix,
                                   slots) {
}

void GLSLPerlinNoiseFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  auto& permSampler = (*args.textureSamplers)[0];
  auto& noiseSampler = (*args.textureSamplers)[1];

  auto baseFreqName =
      uniformHandler->addUniform("baseFrequency", UniformFormat::Float2, ShaderStage::Fragment);

  std::string stitchDataName;
  if (stitchTiles) {
    stitchDataName =
        uniformHandler->addUniform("stitchData", UniformFormat::Float2, ShaderStage::Fragment);
  }

  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);

  // tgfx feeds the fragment shader transformedCoords sitting on the pixel centre (X+0.5, Y+0.5).
  // Multiplying by baseFrequency directly would shift every pixel onto a noise lattice point
  // whenever baseFrequency is an integer: every fragment ends up with fract(noiseVec) = 0, all
  // four corner gradient dot products vanish, and the output collapses to a flat 0.5 grey
  // (visible bug at baseFrequency=1, which the SVG feTurbulence reference uses). To prevent
  // this we add a tiny sub-lattice bias so fract(noiseVec) stays bounded away from 0. 1/128 is
  // exactly representable in float and stays well below half a lattice cell at typical
  // baseFrequencies, so it is invisible in output.
  //
  // Octave doubling: every iteration multiplies noiseVec by 2.0, so the bias becomes 2^k/128.
  // For k <= 6 the bias contribution to fract is non-zero, keeping each octave non-degenerate;
  // for k >= 7 the bias lands back on an integer and that octave can degenerate, but its
  // weight in the final accumulation is ratio = 2^-k <= 1/128, contributing < 1% of the total
  // amplitude. With the supported numOctaves <= 8 this is invisible. The exact bias value is
  // not load-bearing — any small non-zero constant breaks the integer-baseFrequency case.
  fragBuilder->codeAppendf("highp vec2 noiseVec = %s * %s + vec2(0.0078125);", texCoordName.c_str(),
                           baseFreqName.c_str());

  fragBuilder->codeAppend("vec4 color = vec4(0.0);");

  if (stitchTiles) {
    fragBuilder->codeAppendf("highp vec2 stitchData = %s;", stitchDataName.c_str());
  }

  fragBuilder->codeAppend("float ratio = 1.0;");

  fragBuilder->codeAppendf("for (int octave = 0; octave < %d; ++octave) {", numOctaves);

  fragBuilder->codeAppend("highp vec4 floorVal;");
  fragBuilder->codeAppend("floorVal.xy = floor(noiseVec);");
  fragBuilder->codeAppend("floorVal.zw = floorVal.xy + vec2(1.0);");
  fragBuilder->codeAppend("highp vec2 fractVal = fract(noiseVec);");

  // Hermite interpolation
  fragBuilder->codeAppend("vec2 noiseSmooth = smoothstep(0.0, 1.0, fractVal);");

  if (stitchTiles) {
    fragBuilder->codeAppend("floorVal -= step(stitchData.xyxy, floorVal) * stitchData.xyxy;");
  }

  // Wrap floorVal into [0, 256) so that permutation and noise texture lookups stay in range
  // across octaves (where noiseVec doubles each iteration and can exceed 256).
  fragBuilder->codeAppend("floorVal = mod(floorVal, 256.0);");

  // Look up permutation values. Permutations texture is 256x1 A8 (swizzled to RRRR).
  // Texel center: (i + 0.5) / 256.0
  fragBuilder->codeAppend("highp float permX = ");
  fragBuilder->appendTextureLookup(permSampler, "vec2((floorVal.x + 0.5) / 256.0, 0.5)");
  fragBuilder->codeAppend(".r;");

  fragBuilder->codeAppend("highp float permZ = ");
  fragBuilder->appendTextureLookup(permSampler, "vec2((floorVal.z + 0.5) / 256.0, 0.5)");
  fragBuilder->codeAppend(".r;");

  // Recover [0,255] index from [0,1] texture value.
  fragBuilder->codeAppend("highp vec2 latticeIdx = floor(vec2(permX, permZ) * 255.0 + 0.5);");

  // bcoords: (latticeIdx + floorY) mod 256, used to index into noise texture.
  fragBuilder->codeAppend("highp vec4 bcoords = mod(latticeIdx.xyxy + floorVal.yyww, 256.0);");

  fragBuilder->codeAppend("vec4 noiseResult;");

  // Channel Y coordinates: noise texture is 256x4, each row is one channel.
  // Texel center Y: (row + 0.5) / 4.0
  static const char* chanCoords[] = {"0.125", "0.375", "0.625", "0.875"};
  static const char* chanNames[] = {"x", "y", "z", "w"};

  for (int ch = 0; ch < 4; ++ch) {
    fragBuilder->codeAppend("{");

    // Corner A: gradient at (floor.x, floor.y), dot with fractVal
    fragBuilder->codeAppend("vec4 lattice = ");
    fragBuilder->appendTextureLookup(
        noiseSampler, std::string("vec2((bcoords.x + 0.5) / 256.0, ") + chanCoords[ch] + ")");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);");

    // Corner B: gradient at (floor.x+1, floor.y), dot with (fractVal - (1,0))
    fragBuilder->codeAppend("lattice = ");
    fragBuilder->appendTextureLookup(
        noiseSampler, std::string("vec2((bcoords.y + 0.5) / 256.0, ") + chanCoords[ch] + ")");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),"
        " fractVal - vec2(1.0, 0.0));");

    fragBuilder->codeAppend("float a = mix(u, v, noiseSmooth.x);");

    // Corner C: gradient at (floor.x+1, floor.y+1), dot with (fractVal - (1,1))
    fragBuilder->codeAppend("lattice = ");
    fragBuilder->appendTextureLookup(
        noiseSampler, std::string("vec2((bcoords.w + 0.5) / 256.0, ") + chanCoords[ch] + ")");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),"
        " fractVal - vec2(1.0, 1.0));");

    // Corner D: gradient at (floor.x, floor.y+1), dot with (fractVal - (0,1))
    fragBuilder->codeAppend("lattice = ");
    fragBuilder->appendTextureLookup(
        noiseSampler, std::string("vec2((bcoords.z + 0.5) / 256.0, ") + chanCoords[ch] + ")");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),"
        " fractVal - vec2(0.0, 1.0));");

    fragBuilder->codeAppend("float b = mix(u, v, noiseSmooth.x);");

    fragBuilder->codeAppendf("noiseResult.%s = mix(a, b, noiseSmooth.y);", chanNames[ch]);

    fragBuilder->codeAppend("}");
  }

  // Accumulate this octave
  fragBuilder->codeAppend("color += ");
  if (noiseType != PerlinNoiseType::FractalNoise) {
    fragBuilder->codeAppend("abs(");
  }
  fragBuilder->codeAppend("noiseResult");
  if (noiseType != PerlinNoiseType::FractalNoise) {
    fragBuilder->codeAppend(")");
  }
  fragBuilder->codeAppend(" * ratio;");

  fragBuilder->codeAppend("noiseVec *= 2.0;");
  fragBuilder->codeAppend("ratio *= 0.5;");
  if (stitchTiles) {
    fragBuilder->codeAppend("stitchData *= 2.0;");
  }
  fragBuilder->codeAppend("}");  // end octave loop

  // FractalNoise: map from [-1,1] to [0,1]
  if (noiseType == PerlinNoiseType::FractalNoise) {
    fragBuilder->codeAppend("color = color * 0.5 + 0.5;");
  }

  // Clamp each channel to [0, 1], then output premultiplied RGBA. The fourth noise channel
  // serves as alpha and the RGB channels are premultiplied by it. Downstream filters consuming
  // premultiplied inputs (e.g. ColorFilter::Matrix) will unpremultiply correctly; callers
  // needing unpremultiplied noise should compose an appropriate color matrix.
  fragBuilder->codeAppend("color = clamp(color, 0.0, 1.0);");
  fragBuilder->codeAppendf("%s = vec4(color.rgb * color.aaa, color.a);", args.outputColor.c_str());

  // Apply the slot-record operators after the premultiplied noise, matching the precompiled
  // kernel's applyPointwiseSlot1(applyPointwiseOp(...)) order. Only reachable when this processor
  // carries slot records (AOT route fallback safety); a pipeline-built perlin has none and relies
  // on composed operator FPs instead.
  std::unordered_map<std::string, std::string> declaredArrays = {};
  for (size_t index = 0; index < slotCount; ++index) {
    EmitPointwiseSlot(args, declaredArrays, index, pointwiseSlots[index]);
  }
}

void GLSLPerlinNoiseFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float baseFreq[2] = {paintingData->baseFrequencyX, paintingData->baseFrequencyY};
  fragmentUniformData->setData("baseFrequency", baseFreq);

  // stitchData is declared unconditionally by the precompiled shader but only when stitchTiles is
  // set by the runtime-generated shader, so feed it optionally (zero when not stitching).
  float stitchDataValues[2] = {0.0f, 0.0f};
  if (stitchTiles) {
    stitchDataValues[0] = static_cast<float>(paintingData->stitchWidth);
    stitchDataValues[1] = static_cast<float>(paintingData->stitchHeight);
  }
  fragmentUniformData->setDataOptional("stitchData", stitchDataValues);

  // The precompiled PerlinNoiseFillShader folds noiseType / numOctaves / stitchTiles into these runtime
  // uniforms; the runtime-generated shader bakes them into code and does not declare them, so set
  // them optionally.
  fragmentUniformData->setDataOptional("NoiseType", static_cast<int>(noiseType));
  fragmentUniformData->setDataOptional("NumOctaves", numOctaves);
  fragmentUniformData->setDataOptional("StitchTiles", stitchTiles ? 1 : 0);

  // Default the shared pointwise operator to NONE (passthrough). When a pointwise operator FP is
  // composed on top of this shader it is visited after this child in FragmentProcessor::Iter and
  // overwrites OpType with its own value, so this only takes effect for the pure-noise case.
  fragmentUniformData->setArrayElementOptional("OpType", 0, static_cast<int>(4));
  // Slots 1/2 are owned by this processor and always written here (no other FP knows the layout).
  fragmentUniformData->setArrayElementOptional("OpType", 1, static_cast<int>(4));
  fragmentUniformData->setArrayElementOptional("OpType", 2, static_cast<int>(4));
  // Runtime loop bound of the precompiled kernel; a uniform so the slot loop stays rolled and the
  // operator library exists once in the binary.
  fragmentUniformData->setDataOptional("PointwiseSlotCount", static_cast<int>(MaxPointwiseSlots));

  for (size_t index = 0; index < slotCount; ++index) {
    UploadAOTPointwiseSlot(fragmentUniformData, index, MaxPointwiseSlots, pointwiseSlots[index]);
  }
}
}  // namespace tgfx
