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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace tgfx {

class Context;
class PrecompiledShaderCache;

/**
 * The one internal way to a Context's precompiled shader cache. The cache is an AOT artefact store:
 * it is not part of the public API, and the public Context header exposes no accessor for it. Code
 * inside the library (routing, the program creator, the diagnostics) and the tests reach it here.
 */
class ContextCacheAccess {
 public:
  /// The cache of `context`, or null when the context has none (a context without a GPU backend).
  static PrecompiledShaderCache* Cache(const Context* context);
};

}  // namespace tgfx
