/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "ColorSpaceXformHelper.h"
#include "core/utils/Log.h"
#include "gpu/ShaderBuilder.h"
namespace tgfx {

void ColorSpaceXformHelper::emitUniform(UniformHandler* uniformHandler,
                                        const ColorSpaceXformSteps* colorSpaceXform,
                                        ShaderStage shaderStage) {
  uint64_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXform);
  std::string nameSuffix = std::to_string(key);
  if (colorSpaceXform) {
    fFlags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      srcTFVar0 =
          uniformHandler->addUniform("SrcTF0_" + nameSuffix, UniformFormat::Float4, shaderStage);
      srcTFVar1 =
          uniformHandler->addUniform("SrcTF1_" + nameSuffix, UniformFormat::Float4, shaderStage);
      _srcTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->srcTransferFunction));
    }
    if (this->applySrcOOTF()) {
      srcOOTFVar =
          uniformHandler->addUniform("SrcOOTF_" + nameSuffix, UniformFormat::Float4, shaderStage);
    }
    if (this->applyGamutXform()) {
      gamutXformVar = uniformHandler->addUniform("ColorXform_" + nameSuffix,
                                                 UniformFormat::Float3x3, shaderStage);
    }
    if (this->applyDstOOTF()) {
      dstOOTFVar =
          uniformHandler->addUniform("DstOOTF_" + nameSuffix, UniformFormat::Float4, shaderStage);
    }
    if (this->applyDstTF()) {
      dstTFVar0 =
          uniformHandler->addUniform("DstTF0_" + nameSuffix, UniformFormat::Float4, shaderStage);
      dstTFVar1 =
          uniformHandler->addUniform("DstTF1_" + nameSuffix, UniformFormat::Float4, shaderStage);
      _dstTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->dstTransferFunctionInverse));
    }
  }
}

void ColorSpaceXformHelper::emitFunction(ShaderBuilder* shaderBuilder,
                                         const ColorSpaceXformSteps* colorSpaceXform) {
  if (isNoop()) {
    return;
  }
  uint64_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXform);
  std::string nameSuffix = std::to_string(key);
  auto emitTFFunc = [&shaderBuilder](const char* name, const char* tfVar0, const char* tfVar1,
                                     gfx::skcms_TFType tfType) {
    std::string funcName = shaderBuilder->getMangledFunctionName(name);
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "float %s(float x)\n", funcName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tfloat G = %s[0];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat A = %s[1];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat B = %s[2];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat C = %s[3];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat D = %s[0];\n", tfVar1);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat E = %s[1];\n", tfVar1);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat F = %s[2];\n", tfVar1);
    function += buffer;
    function += "\tfloat s = sign(x);\n";
    function += "\tx = abs(x);\n";
    switch (tfType) {
      case gfx::skcms_TFType_sRGBish:
        function += "\tx = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;\n";
        break;
      case gfx::skcms_TFType_PQish:
        function += "\tx = pow(max(A + B * pow(x, C), 0.0f) / (D + E * pow(x, C)), F);\n";
        break;
      case gfx::skcms_TFType_HLGish:
        function +=
            "\tx = (x * A <= 1.0f) ? pow(x * A, B) : exp(( x - E) * C) + D; x *= (F + 1.0f);\n";
        break;
      case gfx::skcms_TFType_HLGinvish:
        function += "\tx /= (F + 1.0f); x = (x <= 1.0f) ? A * pow(x, B) : C * log(x - D) + E;\n";
        break;
      default:
        DEBUG_ASSERT(false);
        break;
    }
    function += "\treturn s * x;\n";
    function += "}\n";
    shaderBuilder->addFunction(function);
    return funcName;
  };

  std::string srcTFFunctionName;
  if (applySrcTF()) {
    srcTFFunctionName = "src_tf_" + nameSuffix;
    srcTFFunctionName = emitTFFunc(srcTFFunctionName.c_str(), srcTFUniform0().c_str(),
                                   srcTFUniform1().c_str(), srcTFType());
  }

  std::string dstTFFunctionName;
  if (applyDstTF()) {
    dstTFFunctionName = "dst_tf_" + nameSuffix;
    dstTFFunctionName = emitTFFunc(dstTFFunctionName.c_str(), dstTFUniform0().c_str(),
                                   dstTFUniform1().c_str(), dstTFType());
  }

  auto emitOOTFFunc = [&shaderBuilder](const char* name, const char* ootfVar) {
    std::string funcName = shaderBuilder->getMangledFunctionName(name);
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec3 %s(vec3 color)\n", funcName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tfloat Y = dot(color, %s.rgb);\n", ootfVar);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\treturn color * sign(Y) * pow(abs(Y), %s.a);\n", ootfVar);
    function += buffer;
    function += "}\n";
    shaderBuilder->addFunction(function);
    return funcName;
  };

  std::string srcOOTFFuncName;
  if (applySrcOOTF()) {
    srcOOTFFuncName = "src_ootf_" + nameSuffix;
    srcOOTFFuncName = emitOOTFFunc(srcOOTFFuncName.c_str(), srcOOTFUniform().c_str());
  }

  std::string dstOOTFFuncName;
  if (applyDstOOTF()) {
    dstOOTFFuncName = "dst_ootf_" + nameSuffix;
    dstOOTFFuncName = emitOOTFFunc(dstOOTFFuncName.c_str(), dstOOTFUniform().c_str());
  }

  std::string gamutXformFuncName;
  if (applyGamutXform()) {
    gamutXformFuncName = "gamut_xform_" + nameSuffix;
    gamutXformFuncName = shaderBuilder->getMangledFunctionName(gamutXformFuncName.c_str());
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec4 %s(vec4 color)\n", gamutXformFuncName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tcolor.rgb = (%s * color.rgb);\n",
             gamutXformUniform().c_str());
    function += buffer;
    function += "\treturn color;\n";
    function += "}\n";
    shaderBuilder->addFunction(function);
  }

  {
    std::string function;
    std::string colorXformFuncName = "color_xform_" + nameSuffix;
    colorXformFuncName = shaderBuilder->getMangledFunctionName(colorXformFuncName.c_str());
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec4 %s(vec4 color)\n", colorXformFuncName.c_str());
    function += buffer;
    function += "{\n";
    if (applyUnpremul()) {
      function += "\tfloat alpha = color.a;\n";
      function +=
          "\tcolor = alpha > 0.0f ? vec4(color.rgb / alpha, alpha) : vec4(0.0f, 0.0f, 0.0f, "
          "0.0f);\n";
    }
    if (applySrcTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.r = %s(color.r);\n", srcTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.g = %s(color.g);\n", srcTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.b = %s(color.b);\n", srcTFFunctionName.c_str());
      function += buffer;
    }
    if (applySrcOOTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.rgb = %s(color.rgb);\n", srcOOTFFuncName.c_str());
      function += buffer;
    }
    if (applyGamutXform()) {
      snprintf(buffer, sizeof(buffer), "\tcolor = %s(color);\n", gamutXformFuncName.c_str());
      function += buffer;
    }
    if (applyDstOOTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.rgb = %s(color.rgb);\n", dstOOTFFuncName.c_str());
      function += buffer;
    }
    if (applyDstTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.r = %s(color.r);\n", dstTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.g = %s(color.g);\n", dstTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.b = %s(color.b);\n", dstTFFunctionName.c_str());
      function += buffer;
    }
    if (applyPremul()) {
      function += "\tcolor.rgb *= color.a;\n";
    }
    function += "\treturn color;\n";
    function += "}\n";
    shaderBuilder->addFunction(function);
  }
}

void ColorSpaceXformHelper::setData(UniformData* uniformData,
                                    const ColorSpaceXformSteps* colorSpaceXform) {
  uint64_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXform);
  std::string nameSuffix = std::to_string(key);
  if (colorSpaceXform) {
    fFlags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      float srcTF0[4] = {
          colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
          colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
      float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                         colorSpaceXform->srcTransferFunction.e,
                         colorSpaceXform->srcTransferFunction.f, 0.0f};
      uniformData->setData("SrcTF0_" + nameSuffix, srcTF0);
      uniformData->setData("SrcTF1_" + nameSuffix, srcTF1);
    }
    if (this->applySrcOOTF()) {
      uniformData->setData("SrcOOTF_" + nameSuffix, colorSpaceXform->srcOOTF);
    }
    if (this->applyGamutXform()) {
      uniformData->setData("ColorXform_" + nameSuffix, colorSpaceXform->srcToDstMatrix);
    }
    if (this->applyDstOOTF()) {
      uniformData->setData("DstOOTF_" + nameSuffix, colorSpaceXform->dstOOTF);
    }
    if (this->applyDstTF()) {
      float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                         colorSpaceXform->dstTransferFunctionInverse.a,
                         colorSpaceXform->dstTransferFunctionInverse.b,
                         colorSpaceXform->dstTransferFunctionInverse.c};
      float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                         colorSpaceXform->dstTransferFunctionInverse.e,
                         colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
      uniformData->setData("DstTF0_" + nameSuffix, dstTF0);
      uniformData->setData("DstTF1_" + nameSuffix, dstTF1);
    }
  }
}
}  // namespace tgfx