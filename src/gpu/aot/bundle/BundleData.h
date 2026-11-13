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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

// The in-memory shape of one parsed bundle. It lives beside the cache rather than inside it because
// the reader that fills it and the cache that stores it are separate translation units: a nested
// type would force the reader to be a member function of the cache again, which is what made the
// loader one 1000-line file.

/// One stage's artifact: the compiled payload and nothing else. The interface the shader was
/// generated against is the declaration table's (DeriveLayout), so the bundle stores no reflection.
struct ShaderStageBlob {
  std::vector<uint8_t> data;
};

/// The immutable parse of one bundle. loadBundle() shares one instance per (identity hash, byte
/// size) across every cache in the process, so an embedded bundle is parsed once rather than once
/// per Context; the parse is contention-free read-only afterwards.
struct ParsedBundle {
  std::string profileTag;
  uint64_t identityHash = 0;
  uint32_t toolchainABI = 0;
  uint16_t payloadFormat = BundleFormat::Payload::Text;
  std::unordered_map<uint32_t, ShaderStageBlob> vertEntries;
  std::unordered_map<uint32_t, ShaderStageBlob> fragEntries;
};

}  // namespace tgfx
