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
 * Which expressions a lookup may consider:
 *   AnyFamily  a draw's own expression — the curated rules, then the interpreter's fallback row
 *   ChainOnly  a plan pass — the expression is already compiled, only the interpreter carries it
 *
 * It lives in its own header because the per-draw ProgramInfo carries it: the info must be able
 * to state its scope without pulling in the selection entry that reads it.
 */
enum class SelectionScope {
  AnyFamily,
  ChainOnly,
};

}  // namespace tgfx
