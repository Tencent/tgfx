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

#include <cstdint>

namespace tgfx {

/// The shader library bundle is one file carrying the artifacts of every target language: function
/// sources for the text languages and one linkable module per operator for SPIR-V. A package may
/// keep only the sections its platform needs; the header and section table stay self-contained so
/// a trimmed bundle still parses.
/// Version 2 adds the SPIR-V section (the operator modules the Vulkan stitching route links); a
/// version 1 bundle carries no such section and is refused rather than read through a branch the
/// loader no longer has.
constexpr uint32_t SHADER_LIBRARY_BUNDLE_VERSION = 2;
constexpr uint32_t SHADER_LIBRARY_BUNDLE_HEADER_SIZE = 32;
constexpr uint32_t SHADER_LIBRARY_BUNDLE_SECTION_SIZE = 24;
constexpr uint32_t SHADER_LIBRARY_BUNDLE_FUNCTION_SIZE = 16;
constexpr char SHADER_LIBRARY_BUNDLE_MAGIC[] = "TGFXSLB1";

/// The output language of one section. The values are part of the file format, so new languages
/// append instead of renumbering.
enum class ShaderLibraryLanguage : uint32_t {
  GLSL = 1,
  MSL = 2,
  HLSL = 3,
  WGSL = 4,
  SPIRV = 5,
};

constexpr uint32_t ShaderLibraryLanguageMask(ShaderLibraryLanguage language) {
  return 1u << (static_cast<uint32_t>(language) - 1u);
}

}  // namespace tgfx
