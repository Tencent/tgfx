/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class FragmentProcessor;
class ProgramInfo;
struct GeometryLayout;
struct Variant;
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

/// The record one program lookup leaves behind. Both return an empty record when the cache's
/// diagnostic recording is off, so the production path pays one flag read and no string work.

/// Dumps every pipeline-state field the descriptor carries, so a failing creation can be diffed
/// against a succeeding one; the backend logs the underlying error separately.

}  // namespace tgfx
