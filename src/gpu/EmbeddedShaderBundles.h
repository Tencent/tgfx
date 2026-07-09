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

#include <cstddef>
#include <cstdint>
#include <utility>
#include "tgfx/gpu/Backend.h"

namespace tgfx {

/// Provides access to precompiled shader bundles that are embedded directly into the library
/// binary at build time. Only the backends compiled in the current build will have data available.
class EmbeddedShaderBundles {
 public:
  /// Registers an embedded bundle for a given backend. Called by auto-generated source files.
  static void Register(Backend backend, const uint8_t* data, size_t size);

  /// Returns the embedded bundle data for the given backend. Returns {nullptr, 0} if no bundle
  /// is available for that backend.
  static std::pair<const uint8_t*, size_t> GetBundle(Backend backend);
};

}  // namespace tgfx
