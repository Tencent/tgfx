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

#pragma once

#include <cstddef>
#include <string>

namespace tgfx {

/// What a stored direct-GLSL text contains that its dialect must not: reason names the leak
/// ("surviving set", "surviving binding", "dialect header", "surviving conditional",
/// "subpass syntax") or is null for a clean text, and offset is the position of the surviving
/// qualifier inside text (only meaningful for the two qualifier reasons).
struct DirectFormLeak {
  const char* reason = nullptr;
  size_t offset = 0;
};

/// Scans one stored direct-GLSL text — already preprocessed, so everything left is live for the
/// variant — for anything that must not survive the dialect rewrite: a set/binding layout
/// qualifier (preceded by '(' or ',', the layout-qualifier context), the wrong dialect header,
/// a surviving conditional directive, or Vulkan-only subpass syntax. The GL and ES paths of the
/// build used to run two hand-rolled copies of this scan; they share this one now.
DirectFormLeak FindDirectFormLeak(const std::string& text, bool esForm);

}  // namespace tgfx
