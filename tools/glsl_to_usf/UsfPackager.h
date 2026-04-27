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

#include <string>

#include "GlslToSpv.h"

namespace tgfx {

/**
 * Wrap an HLSL source produced by ConvertSpvToHlsl with the minimum boilerplate Unreal Engine
 * expects from a .usf shader file:
 *   - `#include "/Engine/Public/Platform.ush"` at the top so the HLSL sees UE's platform macros.
 *   - A leading banner comment naming the pipeline key and stage.
 *
 * Entry-point renaming (void main -> MainVS / MainPS) is already done by ConvertSpvToHlsl via
 * SPIRV-Cross's rename_entry_point; this packager does not touch the body.
 */
std::string PackageAsUsf(const std::string& hlslSource, GlslStage stage, const std::string& keyHex);

}  // namespace tgfx
