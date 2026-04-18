/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ShaderModuleRegistry.h"
#include <unordered_map>

namespace tgfx {

// Embedded shader module sources, auto-generated from src/gpu/shaders/*.glsl.
// To regenerate after editing .glsl files, run:
//   cmake -DSHADER_DIR=src/gpu/shaders -DOUTPUT_FILE=src/gpu/ShaderModuleEmbedded.inc -P embed_shaders.cmake
#include "ShaderModuleEmbedded.inc"

static const std::unordered_map<std::string, ShaderModuleID> kProcessorModuleMap = {
    {"ConstColorProcessor", ShaderModuleID::ConstColor},
    {"LinearGradientLayout", ShaderModuleID::LinearGradientLayout},
    {"SingleIntervalGradientColorizer", ShaderModuleID::SingleIntervalGradientColorizer},
    {"RadialGradientLayout", ShaderModuleID::RadialGradientLayout},
    {"DiamondGradientLayout", ShaderModuleID::DiamondGradientLayout},
    {"ConicGradientLayout", ShaderModuleID::ConicGradientLayout},
    {"AARectEffect", ShaderModuleID::AARectEffect},
    {"ColorMatrixFragmentProcessor", ShaderModuleID::ColorMatrix},
    {"LumaFragmentProcessor", ShaderModuleID::Luma},
    {"DualIntervalGradientColorizer", ShaderModuleID::DualIntervalGradientColorizer},
    {"AlphaStepFragmentProcessor", ShaderModuleID::AlphaThreshold},
    {"TextureGradientColorizer", ShaderModuleID::TextureGradientColorizer},
    {"DeviceSpaceTextureEffect", ShaderModuleID::DeviceSpaceTextureEffect},
    {"TextureEffect", ShaderModuleID::TextureEffect},
    {"TiledTextureEffect", ShaderModuleID::TiledTextureEffect},
    {"UnrolledBinaryGradientColorizer", ShaderModuleID::UnrolledBinaryGradientColorizer},
    {"EmptyXferProcessor", ShaderModuleID::EmptyXfer},
    {"PorterDuffXferProcessor", ShaderModuleID::PorterDuffXfer},
    {"XfermodeFragmentProcessor - two", ShaderModuleID::XfermodeEffect},
    {"XfermodeFragmentProcessor - dst", ShaderModuleID::XfermodeEffect},
    {"XfermodeFragmentProcessor - src", ShaderModuleID::XfermodeEffect},
    {"ClampedGradientEffect", ShaderModuleID::ClampedGradientEffect},
    {"GaussianBlur1DFragmentProcessor", ShaderModuleID::GaussianBlur1D},
    {"ColorSpaceXformEffect", ShaderModuleID::ColorSpaceXformEffect},
    {"DefaultGeometryProcessor", ShaderModuleID::DefaultGeometry},
    {"MeshGeometryProcessor", ShaderModuleID::MeshGeometry},
    {"QuadPerEdgeAAGeometryProcessor", ShaderModuleID::QuadAAGeometry},
    {"AtlasTextGeometryProcessor", ShaderModuleID::AtlasTextGeometry},
    {"EllipseGeometryProcessor", ShaderModuleID::EllipseGeometry},
    {"RoundStrokeRectGeometryProcessor", ShaderModuleID::RoundStrokeRectGeometry},
    {"HairlineLineGeometryProcessor", ShaderModuleID::HairlineLineGeometry},
    {"HairlineQuadGeometryProcessor", ShaderModuleID::HairlineQuadGeometry},
};

// ---- Public API implementation ----

const std::string& ShaderModuleRegistry::GetModule(ShaderModuleID id) {
  switch (id) {
    case ShaderModuleID::TypesGLSL:
      return kTypesGLSL;
    case ShaderModuleID::ConstColor:
      return kConstColor;
    case ShaderModuleID::LinearGradientLayout:
      return kLinearGradientLayout;
    case ShaderModuleID::SingleIntervalGradientColorizer:
      return kSingleIntervalGradientColorizer;
    case ShaderModuleID::RadialGradientLayout:
      return kRadialGradientLayout;
    case ShaderModuleID::DiamondGradientLayout:
      return kDiamondGradientLayout;
    case ShaderModuleID::ConicGradientLayout:
      return kConicGradientLayout;
    case ShaderModuleID::AARectEffect:
      return kAARectEffect;
    case ShaderModuleID::ColorMatrix:
      return kColorMatrix;
    case ShaderModuleID::Luma:
      return kLuma;
    case ShaderModuleID::DualIntervalGradientColorizer:
      return kDualIntervalGradientColorizer;
    case ShaderModuleID::AlphaThreshold:
      return kAlphaThreshold;
    case ShaderModuleID::TextureGradientColorizer:
      return kTextureGradientColorizer;
    case ShaderModuleID::DeviceSpaceTextureEffect:
      return kDeviceSpaceTextureEffect;
    case ShaderModuleID::TextureEffect:
      return kTextureEffect;
    case ShaderModuleID::TiledTextureEffect:
      return kTiledTextureEffect;
    case ShaderModuleID::UnrolledBinaryGradientColorizer:
      return kUnrolledBinaryGradientColorizer;
    case ShaderModuleID::BlendModes:
      return kBlendModes;
    case ShaderModuleID::EmptyXfer:
      return kEmptyXfer;
    case ShaderModuleID::PorterDuffXfer:
      return kPorterDuffXfer;
    case ShaderModuleID::XfermodeEffect:
      return kXfermodeEffect;
    case ShaderModuleID::ClampedGradientEffect:
      return kClampedGradientEffect;
    case ShaderModuleID::GaussianBlur1D:
      return kGaussianBlur1D;
    case ShaderModuleID::ColorSpaceXformEffect:
      return kColorSpaceXformEffect;
    case ShaderModuleID::DefaultGeometry:
      return kDefaultGeometryVert;
    case ShaderModuleID::MeshGeometry:
      return kMeshGeometryVert;
    case ShaderModuleID::QuadAAGeometry:
      return kQuadAAGeometryVert;
    case ShaderModuleID::AtlasTextGeometry:
      return kAtlasTextGeometryVert;
    case ShaderModuleID::EllipseGeometry:
      return kEllipseGeometryVert;
    case ShaderModuleID::RoundStrokeRectGeometry:
      return kRoundStrokeRectGeometryVert;
    case ShaderModuleID::HairlineLineGeometry:
      return kHairlineLineGeometryVert;
    case ShaderModuleID::HairlineQuadGeometry:
      return kHairlineQuadGeometryVert;
  }
  static const std::string kEmpty;
  return kEmpty;
}

bool ShaderModuleRegistry::HasModule(const std::string& processorName) {
  return kProcessorModuleMap.count(processorName) > 0;
}

ShaderModuleID ShaderModuleRegistry::GetModuleID(const std::string& processorName) {
  return kProcessorModuleMap.at(processorName);
}

}  // namespace tgfx
