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

#include "GLSLPorterDuffXferProcessor.h"
#include "gpu/glsl/GLSLBlend.h"

namespace tgfx {
PlacementPtr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(BlockBuffer* buffer,
                                                                    BlendMode blend,
                                                                    DstTextureInfo dstTextureInfo) {
  return buffer->make<GLSLPorterDuffXferProcessor>(blend, std::move(dstTextureInfo));
}

GLSLPorterDuffXferProcessor::GLSLPorterDuffXferProcessor(BlendMode blend,
                                                         DstTextureInfo dstTextureInfo)
    : PorterDuffXferProcessor(blend, std::move(dstTextureInfo)) {
}

void GLSLPorterDuffXferProcessor::emitCode(const EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  const auto& dstColor = fragBuilder->dstColor();

  if (args.dstTextureSamplerHandle.isValid()) {
    // While shaders typically don't output negative coverage, we use <= as a precaution against
    // floating point precision errors. We only check the rgb values since the alpha might not be
    // set when using lcd. If we're using single channel coverage, alpha will match rgb anyway.
    //
    // Discarding here also helps batch text draws that need to read from a dst copy for blends.
    // This is particularly useful when the outer bounding boxes of each letter overlap, though it
    // doesn't help when actual parts of the text overlap.
    fragBuilder->codeAppendf("if (%s.r <= 0.0 && %s.g <= 0.0 && %s.b <= 0.0) {",
                             args.inputCoverage.c_str(), args.inputCoverage.c_str(),
                             args.inputCoverage.c_str());
    fragBuilder->codeAppend("discard;");
    fragBuilder->codeAppend("}");

    auto dstTopLeftName = uniformHandler->addUniform("DstTextureUpperLeft", UniformFormat::Float2,
                                                     ShaderStage::Fragment);
    auto dstCoordScaleName = uniformHandler->addUniform(
        "DstTextureCoordScale", UniformFormat::Float2, ShaderStage::Fragment);

    fragBuilder->codeAppend("// Read color from copy of the destination.\n");
    std::string dstTexCoord = "_dstTexCoord";
    fragBuilder->codeAppendf("vec2 %s = (gl_FragCoord.xy - %s) * %s;", dstTexCoord.c_str(),
                             dstTopLeftName.c_str(), dstCoordScaleName.c_str());

    fragBuilder->codeAppendf("vec4 %s = ", dstColor.c_str());
    fragBuilder->appendTextureLookup(args.dstTextureSamplerHandle, dstTexCoord);
    fragBuilder->codeAppend(";");
  }

  const char* outColor = "localOutputColor";
  fragBuilder->codeAppendf("vec4 %s;", outColor);
  AppendMode(fragBuilder, args.inputColor, args.inputCoverage, dstColor, outColor, blendMode, true);

  if (!BlendModeAsCoeff(blendMode, true)) {
    fragBuilder->codeAppendf("%s = %s * %s + (vec4(1.0) - %s) * %s;", outColor,
                             args.inputCoverage.c_str(), outColor, args.inputCoverage.c_str(),
                             dstColor.c_str());
  }
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), outColor);
}

void GLSLPorterDuffXferProcessor::setData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  if (dstTextureInfo.textureProxy == nullptr) {
    return;
  }
  auto dstTextureView = dstTextureInfo.textureProxy->getTextureView();
  if (dstTextureView == nullptr) {
    return;
  }
  fragmentUniformData->setData("DstTextureUpperLeft", dstTextureInfo.offset);
  int width;
  int height;
  if (dstTextureView->getTexture()->type() == GPUTextureType::Rectangle) {
    width = 1;
    height = 1;
  } else {
    width = dstTextureView->width();
    height = dstTextureView->height();
  }
  float scales[] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  fragmentUniformData->setData("DstTextureCoordScale", scales);
}
}  // namespace tgfx
