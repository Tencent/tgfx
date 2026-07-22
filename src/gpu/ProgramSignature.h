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

namespace tgfx {
class ProgramInfo;

/// Builds a human-readable effect signature describing the geometry processor, color/coverage
/// fragment processors, and xfer processor of a pipeline, e.g.
/// "GP=DefaultGeometryProcessor;ColorFP=[];CoverageFP=[TextureEffect];XP=EmptyXferProcessor".
/// Used by the AOT hit/miss reporter and the runtime shader dump diagnostics.
std::string BuildEffectSignature(const ProgramInfo* programInfo);

}  // namespace tgfx
