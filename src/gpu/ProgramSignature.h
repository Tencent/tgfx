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
#include <vector>

namespace tgfx {
class FragmentProcessor;
struct GeometryLayout;
struct XferFacts;

/// Builds a human-readable effect signature describing the geometry, color/coverage fragment
/// processors, and xfer, e.g. "GP=Default;ColorFP=[];CoverageFP=[TextureEffect];XP=fixed".
/// Diagnostics only: the matcher, the program key and the writers read the draw's IR. The
/// signature stays processor-based while processors exist, so the AOT reporter's established
/// signatures (and its reviewed known-gap table) do not churn with the IR migration.
std::string BuildEffectSignature(const GeometryLayout& geometry,
                                 const std::vector<const FragmentProcessor*>& colorProcessors,
                                 const std::vector<const FragmentProcessor*>& coverageProcessors,
                                 const XferFacts& xfer);

}  // namespace tgfx
