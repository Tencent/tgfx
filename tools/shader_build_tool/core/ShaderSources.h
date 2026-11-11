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

#pragma once

#include <cstddef>
#include <string>

namespace tgfx {

inline constexpr const char* GeneratorContractFiles[] = {"contract/ChainISA.h",
                                                         "contract/ChainOps.h",
                                                         // The declaration layer generates the
                                                         // assembled text the same way the chain
                                                         // contract generates the interpreter's
                                                         // operator table: a header change changes
                                                         // what was compiled without touching any
                                                         // shader source.
                                                         "Module.h",
                                                         "modules/AnalyticClip.h",
                                                         "modules/AnalyticClipModule.h",
                                                         "modules/AtlasTextGeometry.h",
                                                         "modules/BlendParams.h",
                                                         "modules/BlurSourceGeometry.h",
                                                         "modules/Chain.h",
                                                         "modules/ChainAtlasGeometry.h",
                                                         "modules/ChainComplexGeometry.h",
                                                         "modules/ChainEllipseGeometry.h",
                                                         "modules/ChainHairlineGeometry.h",
                                                         "modules/ChainParams.h",
                                                         "modules/ChainRectGeometry.h",
                                                         "modules/ChainRectUVGeometry.h",
                                                         "modules/ColorSpaceParams.h",
                                                         "modules/ComplexEllipseGeometry.h",
                                                         "modules/ComplexRRectGeometry.h",
                                                         "modules/CoverageTexture.h",
                                                         "modules/DeviceMask.h",
                                                         "modules/DeviceMaskModule.h",
                                                         "modules/DeviceMaskRuntime.h",
                                                         "modules/EllipseGeometry.h",
                                                         "modules/EngineParams.h",
                                                         "modules/Frame.h",
                                                         "modules/GaussianBlur.h",
                                                         "modules/GlassLocalMask.h",
                                                         "modules/GlassRefraction.h",
                                                         "modules/GlassUDFTentBlur.h",
                                                         "modules/Gradient.h",
                                                         "modules/HairlineLineGeometry.h",
                                                         "modules/HairlineQuadGeometry.h",
                                                         "modules/LocalMask.h",
                                                         "modules/MaskTexture.h",
                                                         "modules/MeshGeometry.h",
                                                         "modules/ParamHandle.h",
                                                         "modules/ParamWriter.h",
                                                         "modules/PointwiseOpModule.h",
                                                         "modules/PorterDuffXPModule.h",
                                                         "modules/QuadColorGeometry.h",
                                                         "modules/QuadGeometry.h",
                                                         "modules/QuadTextureGeometry.h",
                                                         "modules/RRectGeometry.h",
                                                         "modules/RectGeometry.h",
                                                         "modules/RoundStrokeRectGeometry.h",
                                                         "modules/ShapeInstancedGeometry.h",
                                                         "modules/Texture.h",
                                                         "modules/TextureSourceModule.h",
                                                         "modules/TiledSourceModule.h",
                                                         "modules/TiledTexture.h",
                                                         "modules/Xfer.h",
                                                         "modules/YUVTexture.h",
                                                         "Family.h",
                                                         "Families.h"};

/// Reads GeneratorContractFiles from the contract directory beside shaderDir (shaderDir points at
/// .../shaders/glsl; the contract headers live in .../shaders/contract) and concatenates them in
/// declaration order. Returns false with an error when one cannot be read: a digest that silently
/// omitted them would call a stale bundle fresh.
bool ReadGeneratorContract(const std::string& shaderDir, std::string* text, std::string* error);

/// The deepest include nesting the expander below accepts.
inline constexpr int MaxIncludeDepth = 32;

/// Expands every #include of an assembled stage text against shaderDir — the same expansion the
/// bundle build applies to the assembled variants, so the text the compiler receives is the text
/// the source digest's directory walk describes. Returns an empty string on failure.
std::string ExpandStageIncludes(const std::string& source, const std::string& shaderDir,
                                std::string* error);

}  // namespace tgfx
