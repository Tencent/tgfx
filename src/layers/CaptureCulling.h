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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace tgfx {

/**
 * Returns whether a capture culls the layers its background styles do not sample. Always true
 * outside the tests, which clear it to keep an unculled reference through the same render path a
 * display list uses. Both live outside the public headers so the layout of DisplayList stays the
 * same for the applications that use it.
 */
bool CaptureCullingEnabledForTests();

/**
 * Sets the capture cull switch. Only the tests call this.
 */
void SetCaptureCullingEnabledForTests(bool enabled);

}  // namespace tgfx
