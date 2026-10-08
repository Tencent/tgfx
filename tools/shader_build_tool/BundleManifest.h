//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/// The result of hashing everything that determines a backend's compiler input.
struct SourceDigestResult {
  bool ok = false;
  uint64_t digest = 0;
  size_t shaderCount = 0;
  size_t stageCount = 0;
  std::string error;
};

/// Computes the source digest of one backend: a 64-bit FNV-1a over a canonical stream made of
///  - the toolchain ABI and the backend tag;
///  - for Metal, the compiler invocation (MetalCompilerFingerprint);
///  - for every registered shader, ordered by name: its file names, the vertex and fragment
///    sources with every #include expanded (the exact text the compiler receives), and for every
///    rule-reachable permutation the backend compiles: the two permutation indices and the
///    #define lists they expand to.
/// Shader text, include closure, variant declarations and matcher rules therefore all move the
/// digest. It deliberately does NOT cover the translation code inside shader_build_tool or the
/// versions of shaderc / SPIRV-Cross / tint / Xcode: those change what the same text compiles to.
/// They are covered by the toolchain ABI discipline and by regenerating and comparing the bundle.
/// The digest is an accident detector, not a security measure.
SourceDigestResult ComputeSourceDigest(const std::string& shaderDir, const std::string& backend);

/// The sidecar record that ties one published bundle to the sources it was built from. It lives in
/// shader_bundle.<backend>.manifest next to the .bin, so the binary format and the runtime loader
/// are untouched.
struct BundleManifest {
  uint32_t manifestVersion = 0;
  std::string backend;
  std::string file;
  uint64_t fileSize = 0;
  uint64_t fileHash = 0;      // FNV-1a over every byte of the .bin, binds manifest and bundle.
  uint64_t identityHash = 0;  // The bundle header's content identity (offset 8).
  uint32_t toolchainABI = 0;
  uint16_t formatVersion = 0;
  uint64_t sourceDigest = 0;
  uint64_t shaderCount = 0;
  uint64_t stageCount = 0;
};

inline constexpr uint32_t kBundleManifestVersion = 1;

/// FNV-1a over a byte range, the hash recorded as BundleManifest::fileHash.
uint64_t HashBundleBytes(const uint8_t* data, size_t size);

/// shader_bundle.<backend>.bin -> shader_bundle.<backend>.manifest.
std::string ManifestPathFor(const std::string& bundlePath);

/// Reads the bundle at bundlePath back, computes the file hash and takes the identity hash, ABI
/// and format version from its header, then writes the manifest next to it.
bool WriteBundleManifest(const std::string& bundlePath, const std::string& backend,
                         const SourceDigestResult& digest, std::string* error);

/// Parses a manifest. Returns false with a reason when the file is missing or a required key is
/// absent or malformed.
bool ReadBundleManifest(const std::string& manifestPath, BundleManifest* manifest,
                        std::string* error);

}  // namespace tgfx
