/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLPorterDuffXferProcessor.h"
#include "gpu/opengl/GLBlend.h"

namespace tgfx {
std::unique_ptr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(BlendMode blend) {
  return std::unique_ptr<PorterDuffXferProcessor>(new GLPorterDuffXferProcessor(blend));
}

GLPorterDuffXferProcessor::GLPorterDuffXferProcessor(BlendMode blend)
    : PorterDuffXferProcessor(blend) {
}

void GLPorterDuffXferProcessor::emitCode(const EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
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

    auto dstTopLeftName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "DstTextureUpperLeft");
    auto dstCoordScaleName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "DstTextureCoordScale");

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
  AppendMode(fragBuilder, args.inputColor, dstColor, outColor, blend);
  fragBuilder->codeAppendf("%s = %s * %s + (vec4(1.0) - %s) * %s;", outColor,
                           args.inputCoverage.c_str(), outColor, args.inputCoverage.c_str(),
                           dstColor.c_str());
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), outColor);
}

void GLPorterDuffXferProcessor::setData(UniformBuffer* uniformBuffer, const Texture* dstTexture,
                                        const Point& dstTextureOffset) const {
  if (dstTexture) {
    uniformBuffer->setData("DstTextureUpperLeft", dstTextureOffset);
    int width;
    int height;
    if (dstTexture->getSampler()->type() == SamplerType::Rectangle) {
      width = 1;
      height = 1;
    } else {
      width = dstTexture->width();
      height = dstTexture->height();
    }
    float scales[] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
    uniformBuffer->setData("DstTextureCoordScale", scales);
  }
}
}  // namespace tgfx
