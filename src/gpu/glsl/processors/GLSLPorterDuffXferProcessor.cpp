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

// ---- Helper: generate OutputType expression as string ----
static std::string OutputTypeExpr(BlendFormula::OutputType type, const std::string& src,
                                  const std::string& coverage) {
  switch (type) {
    case BlendFormula::OutputType::None:
      return "vec4(0.0)";
    case BlendFormula::OutputType::Coverage:
      return coverage;
    case BlendFormula::OutputType::Modulate:
      return src + " * " + coverage;
    case BlendFormula::OutputType::SAModulate:
      return src + ".a * " + coverage;
    case BlendFormula::OutputType::ISAModulate:
      return "(1.0 - " + src + ".a) * " + coverage;
    case BlendFormula::OutputType::ISCModulate:
      return "(vec4(1.0) - " + src + ") * " + coverage;
  }
  return "vec4(0.0)";
}

// ---- Helper: generate BlendFactor coefficient expression as string ----
static std::string CoeffExpr(BlendFactor factor, const std::string& primary,
                             const std::string& secondary, const std::string& dst) {
  switch (factor) {
    case BlendFactor::Zero:
      return "";  // handled separately
    case BlendFactor::One:
      return "";  // no multiply needed
    case BlendFactor::Src:
      return " * " + primary;
    case BlendFactor::OneMinusSrc:
      return " * (vec4(1.0) - " + primary + ")";
    case BlendFactor::Dst:
      return " * " + dst;
    case BlendFactor::OneMinusDst:
      return " * (vec4(1.0) - " + dst + ")";
    case BlendFactor::SrcAlpha:
      return " * " + primary + ".a";
    case BlendFactor::OneMinusSrcAlpha:
      return " * (1.0 - " + primary + ".a)";
    case BlendFactor::DstAlpha:
      return " * " + dst + ".a";
    case BlendFactor::OneMinusDstAlpha:
      return " * (1.0 - " + dst + ".a)";
    case BlendFactor::Src1:
      return " * " + secondary;
    case BlendFactor::OneMinusSrc1:
      return " * (vec4(1.0) - " + secondary + ")";
    case BlendFactor::Src1Alpha:
      return " * " + secondary + ".a";
    case BlendFactor::OneMinusSrc1Alpha:
      return " * (1.0 - " + secondary + ".a)";
  }
  return "";
}

// ---- Helper: generate BlendOperation expression as string ----
static std::string OperationExpr(BlendOperation op, const std::string& src,
                                 const std::string& dst) {
  switch (op) {
    case BlendOperation::Add:
      return "clamp(" + src + " + " + dst + ", 0.0, 1.0)";
    case BlendOperation::Subtract:
      return "clamp(" + src + " - " + dst + ", 0.0, 1.0)";
    case BlendOperation::ReverseSubtract:
      return "clamp(" + dst + " - " + src + ", 0.0, 1.0)";
    case BlendOperation::Min:
      return "min(" + src + ", " + dst + ")";
    case BlendOperation::Max:
      return "max(" + src + ", " + dst + ")";
  }
  return src;
}
PlacementPtr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(BlockAllocator* allocator,
                                                                    BlendMode blend,
                                                                    DstTextureInfo dstTextureInfo) {
  return allocator->make<GLSLPorterDuffXferProcessor>(blend, std::move(dstTextureInfo));
}

GLSLPorterDuffXferProcessor::GLSLPorterDuffXferProcessor(BlendMode blend,
                                                         DstTextureInfo dstTextureInfo)
    : PorterDuffXferProcessor(blend, std::move(dstTextureInfo)) {
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
  if (dstTextureView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  } else {
    width = dstTextureView->width();
    height = dstTextureView->height();
  }
  float scales[] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  fragmentUniformData->setData("DstTextureCoordScale", scales);
}

ShaderCallResult PorterDuffXferProcessor::buildXferCallStatement(
    const std::string& colorInVar, const std::string& coverageInVar, const std::string& outputVar,
    const std::string& dstColorExpr, const MangledUniforms& uniforms,
    const MangledSamplers& samplers) const {
  ShaderCallResult result;
  result.outputVarName = outputVar;
  std::string code;

  // Step 1: Resolve dst color.
  std::string dst = "_xpDst";
  if (dstTextureInfo.textureProxy) {
    // Coverage discard for dst-texture-read path.
    code += "if (" + coverageInVar + ".r <= 0.0 && " + coverageInVar + ".g <= 0.0 && " +
            coverageInVar + ".b <= 0.0) { discard; }\n";
    auto topLeft = uniforms.get("DstTextureUpperLeft");
    auto scale = uniforms.get("DstTextureCoordScale");
    auto sampler = samplers.get("DstTextureSampler");
    code += "vec2 _xpDstTexCoord = (gl_FragCoord.xy - " + topLeft + ") * " + scale + ";\n";
    code += "vec4 " + dst + " = texture(" + sampler + ", _xpDstTexCoord);\n";
  } else {
    code += "vec4 " + dst + " = " + dstColorExpr + ";\n";
  }

  // Step 2: Compute blended color.
  BlendFormula formula;
  bool isCoeff = BlendModeAsCoeff(blendMode, true, &formula);
  if (isCoeff) {
    // Coefficient-based blend: replicate AppendCoeffBlend logic.
    std::string primary = OutputTypeExpr(formula.primaryOutputType(), colorInVar, coverageInVar);
    code += "vec4 _xpPrimary = " + primary + ";\n";

    std::string secondary;
    if (formula.needSecondaryOutput()) {
      secondary = OutputTypeExpr(formula.secondaryOutputType(), colorInVar, coverageInVar);
      code += "vec4 _xpSecondary = " + secondary + ";\n";
    }

    std::string srcTerm;
    if (formula.srcFactor() == BlendFactor::Zero) {
      srcTerm = "vec4(0.0)";
    } else {
      auto coeff = CoeffExpr(formula.srcFactor(), "_xpPrimary",
                             secondary.empty() ? "_xpPrimary" : "_xpSecondary", dst);
      srcTerm = "_xpPrimary" + coeff;
    }

    std::string dstTerm;
    if (formula.dstFactor() == BlendFactor::Zero) {
      dstTerm = "vec4(0.0)";
    } else {
      auto coeff = CoeffExpr(formula.dstFactor(), "_xpPrimary",
                             secondary.empty() ? "_xpPrimary" : "_xpSecondary", dst);
      dstTerm = dst + coeff;
    }

    code += outputVar + " = " + OperationExpr(formula.operation(), srcTerm, dstTerm) + ";\n";
  } else {
    // Advanced blend mode: use tgfx_blend_* functions from BlendModes module.
    code += "vec4 _xpBlended = tgfx_blend(" + colorInVar + ", " + dst + ", " +
            std::to_string(static_cast<int>(blendMode)) + ");\n";
    // Apply coverage modulation for non-coefficient modes.
    code += outputVar + " = " + coverageInVar + " * _xpBlended + (vec4(1.0) - " + coverageInVar +
            ") * " + dst + ";\n";
  }

  result.statement = code;
  return result;
}
}  // namespace tgfx
