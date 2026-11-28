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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "BuildCommand.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace tgfx {

SLType SLTypeFromName(const std::string& glslType) {
  if (glslType == "float") return SLType::Float;
  if (glslType == "int") return SLType::Int;
  if (glslType == "vec2") return SLType::Float2;
  if (glslType == "vec3") return SLType::Float3;
  if (glslType == "vec4") return SLType::Float4;
  if (glslType == "ivec4") return SLType::Int4;
  if (glslType == "mat3") return SLType::Float3x3;
  if (glslType == "mat4") return SLType::Float4x4;
  if (glslType == "sampler2D") return SLType::Texture2DSampler;
  if (glslType == "sampler2DRect") return SLType::Texture2DRectSampler;
  return SLType::Void;
}

std::string UsageText() {
  return "Usage: shader_build_tool [options]\n"
         "  --shader-dir <path>   Directory containing shader sources\n"
         "  --library-bundle <path>\n"
         "                        Scan the function sources and write the single shader library\n"
         "                        bundle\n"
         "  --library-languages <list>\n"
         "                        Comma-separated sections the library bundle carries (glsl,\n"
         "                        msl, wgsl, spirv); the Metal section translates every function\n"
         "                        to MSL\n"
         "\n"
         "  --metal-sdk <sdk>     Platform of the Metal section: macosx (default), iphoneos or\n"
         "                        iphonesimulator\n"
         "  --metal-min-os <ver>  Minimum OS version of the Metal section, e.g. 15.0\n"
         "  --d3d12-shader-format <form>\n"
         "                        What a d3d12 section stores: dxbc (default, bytecode compiled\n"
         "                        here) or hlsl (source the backend compiles at module creation)\n";
}

void PrintUsage() {
  std::cerr << UsageText();
}

bool ParseArgs(int argc, char** argv, BuildOptions* options) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--library-bundle") == 0 && i + 1 < argc) {
      options->libraryBundlePath = argv[++i];
    } else if (std::strcmp(argv[i], "--library-languages") == 0 && i + 1 < argc) {
      options->libraryLanguages = argv[++i];
    } else if (std::strcmp(argv[i], "--shader-dir") == 0 && i + 1 < argc) {
      options->shaderDir = argv[++i];
    } else if (std::strcmp(argv[i], "--metal-sdk") == 0 && i + 1 < argc) {
      options->metalSdk = argv[++i];
    } else if (std::strcmp(argv[i], "--metal-min-os") == 0 && i + 1 < argc) {
      options->metalMinOS = argv[++i];
    } else if (std::strcmp(argv[i], "--d3d12-shader-format") == 0 && i + 1 < argc) {
      options->d3d12ShaderFormat = argv[++i];
    } else {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      PrintUsage();
      return false;
    }
  }
  return true;
}

}  // namespace tgfx
