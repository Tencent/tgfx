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
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

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

  auto permTex = permutationsView->getTexture();
  auto noiseTex = noiseView->getTexture();
  return allocator->make<GLSLPerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permTex),
      std::move(noiseTex), uvMatrix);
}

GLSLPerlinNoiseFragmentProcessor::GLSLPerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<Texture> permutationsTexture, std::shared_ptr<Texture> noiseTexture,
    const Matrix* uvMatrix)
    : PerlinNoiseFragmentProcessor(noiseType, numOctaves, stitchTiles, std::move(paintingData),
                                   std::move(permutationsTexture), std::move(noiseTexture),
                                   uvMatrix) {
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

  // Compute noise coordinates with 0.5 pixel offset for SVG spec consistency.
  fragBuilder->codeAppendf("vec2 noiseVec = (%s + 0.5) * %s;", texCoordName.c_str(),
                           baseFreqName.c_str());

  fragBuilder->codeAppend("vec4 color = vec4(0.0);");

  if (stitchTiles) {
    fragBuilder->codeAppendf("vec2 stitchData = %s;", stitchDataName.c_str());
  }

  fragBuilder->codeAppend("float ratio = 1.0;");

  fragBuilder->codeAppendf("for (int octave = 0; octave < %d; ++octave) {", numOctaves);

  fragBuilder->codeAppend("vec4 floorVal;");
  fragBuilder->codeAppend("floorVal.xy = floor(noiseVec);");
  fragBuilder->codeAppend("floorVal.zw = floorVal.xy + vec2(1.0);");
  fragBuilder->codeAppend("vec2 fractVal = fract(noiseVec);");

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
  fragBuilder->codeAppend("float permX = ");
  fragBuilder->appendTextureLookup(permSampler, "vec2((floorVal.x + 0.5) / 256.0, 0.5)");
  fragBuilder->codeAppend(".r;");

  fragBuilder->codeAppend("float permZ = ");
  fragBuilder->appendTextureLookup(permSampler, "vec2((floorVal.z + 0.5) / 256.0, 0.5)");
  fragBuilder->codeAppend(".r;");

  // Recover [0,255] index from [0,1] texture value.
  fragBuilder->codeAppend("vec2 latticeIdx = floor(vec2(permX, permZ) * 255.0 + 0.5);");

  // bcoords: (latticeIdx + floorY) mod 256, used to index into noise texture.
  fragBuilder->codeAppend("vec4 bcoords = mod(latticeIdx.xyxy + floorVal.yyww, 256.0);");

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

  // Clamp and output with alpha = 1.0 (opaque). The fourth noise channel is discarded to avoid
  // premultiply artifacts when downstream filters interpret alpha as transparency.
  fragBuilder->codeAppend("color = clamp(color, 0.0, 1.0);");
  fragBuilder->codeAppendf("%s = vec4(color.rgb, 1.0);", args.outputColor.c_str());
}

void GLSLPerlinNoiseFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float baseFreq[2] = {paintingData->baseFrequencyX, paintingData->baseFrequencyY};
  fragmentUniformData->setData("baseFrequency", baseFreq);

  if (stitchTiles) {
    float stitchDataValues[2] = {static_cast<float>(paintingData->stitchWidth),
                                 static_cast<float>(paintingData->stitchHeight)};
    fragmentUniformData->setData("stitchData", stitchDataValues);
  }
}
}  // namespace tgfx
