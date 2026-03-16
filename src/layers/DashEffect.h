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

#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

/**
 * Creates a dash path effect from the given dash pattern. Handles odd-count pattern expansion and
 * simplification for square caps. Returns nullptr if dashes are empty or simplified to solid.
 */
std::shared_ptr<PathEffect> CreateDashPathEffect(const std::vector<float>& dashes, float dashOffset,
                                                 bool adaptive, const Stroke& stroke);

}  // namespace tgfx
