/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// Shared predicates of the per-family Match functions. Declared here and defined once in
// PermutationMatcher.cpp (their private helper chains stay internal to that translation unit);
// each family's .cpp includes this header, so family matching code never duplicates them.
namespace tgfx {

class ProgramInfo;

/// 0 = Empty XferProcessor, 1 = PorterDuff DST_TEX, 2 = PorterDuff FBF, -1 = none representable.
int GetXPType(const ProgramInfo* programInfo);

/// True when the draw carries no fragment processors at all, or exactly one direct analytic clip
/// coverage FP (AA rect or rrect) that the runtime clip contract uniforms can evaluate.
bool HasNoProcessorsOrDirectClip(const ProgramInfo* programInfo);

}  // namespace tgfx
