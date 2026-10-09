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

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tgfx {

/// SHA-256 (FIPS 180-4). Used for stage cache keys and entry checksums: the cache must never hand
/// back an artifact compiled from different input, so its keys need more than a 64-bit hash.
class Sha256 {
 public:
  Sha256();
  void update(const void* data, size_t size);
  std::array<uint8_t, 32> finish();

  static std::string Hex(const std::array<uint8_t, 32>& digest);

 private:
  void transform(const uint8_t* block);

  std::array<uint32_t, 8> state = {};
  std::array<uint8_t, 64> buffer = {};
  size_t bufferSize = 0;
  uint64_t totalBytes = 0;
};

/// Builds the key of one cached compilation from every input that can change its output. Each
/// field is length prefixed, so different field sequences never produce the same byte stream.
/// Every key also covers the cache schema and the identity of the running shader_build_tool, so
/// rebuilding the tool (its translation code, shaderc, SPIRV-Cross or tint, all linked into it)
/// invalidates every entry.
class StageCacheKey {
 public:
  explicit StageCacheKey(const std::string& kind);
  StageCacheKey& add(const std::string& text);
  StageCacheKey& add(const void* data, size_t size);
  StageCacheKey& add(uint64_t value);
  /// The key as 64 hex digits.
  std::string finish();

 private:
  Sha256 hash;
};

/// What one kind of cached work did during a run.
struct StageCacheStats {
  uint64_t requests = 0;
  uint64_t memoryHits = 0;
  uint64_t diskHits = 0;
  uint64_t executed = 0;
  uint64_t stored = 0;
  uint64_t corrupt = 0;
};

/// A content-addressed store of compiled shader stages, shared by every build of one output
/// directory. An entry is only returned when its key matches and its payload checksum verifies;
/// a damaged entry counts as corrupt and is recompiled. Entries are written to a temporary file
/// and renamed into place, so a concurrent or interrupted build never leaves a half-written entry
/// that can be read back. Only successful compilations are stored.
class StageCache {
 public:
  static StageCache& Get();

  /// Enables the persistent store under dir. Returns false (and stays memory-only) when the
  /// directory cannot be created or the tool's own identity cannot be determined.
  bool open(const std::string& dir, std::string* error);

  bool persistent() const {
    return !directory.empty();
  }

  const std::string& directoryPath() const {
    return directory;
  }

  /// The SHA-256 of the running shader_build_tool executable, or "" when unknown.
  const std::string& toolIdentity() const {
    return identity;
  }

  /// Looks the key up on disk. Counts a disk hit or a corrupt entry in stats.
  bool load(const std::string& key, std::vector<uint8_t>* payload, StageCacheStats* stats);

  /// Stores a payload under the key. Write failures only cost the next build a recompile.
  void store(const std::string& key, const std::vector<uint8_t>& payload, StageCacheStats* stats);

  /// Per-kind counters, printed at the end of a build.
  StageCacheStats& stats(const std::string& kind) {
    return kindStats[kind];
  }

  void printSummary() const;

 private:
  StageCache();
  StageCache(const StageCache&) = delete;
  StageCache& operator=(const StageCache&) = delete;

  std::string entryPath(const std::string& key) const;

  std::string directory = {};
  std::string identity = {};
  std::map<std::string, StageCacheStats> kindStats = {};
  uint64_t tempCounter = 0;
};

/// Runs the SHA-256 known-answer tests. The persistent cache refuses to open if they fail.
bool Sha256SelfTest();

}  // namespace tgfx
