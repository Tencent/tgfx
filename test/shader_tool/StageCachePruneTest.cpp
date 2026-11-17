/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The cache grew to 42GB in a day of tool rebuilds: every rebuild changes the tool identity and
// leaves the previous identity's entries behind forever. These tests pin the two rules that keep
// it bounded — foreign identities prune (never the current one, never one inside the grace
// window, the newest one survives the keep quota) and a cap evicts oldest-first — because a
// mistake in either deletes live entries or keeps dead ones.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include "ScratchDir.h"
#include "compile/StageCache.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

namespace fs = std::filesystem;

void TouchWithAge(const fs::path& path, std::chrono::seconds age) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "x";
  }
  fs::last_write_time(path, fs::file_time_type::clock::now() - age, code);
}

TEST(StageCachePruneTest, ForeignIdentitiesPruneWithGraceAndLegacyBucketsGo) {
  ScratchDir dir("cache_prune");
  const auto root = fs::path(dir.dir());
  // The current identity: a stale marker changes nothing, it is never a candidate.
  TouchWithAge(root / "aaaaaaaaaaaa" / "last_used", std::chrono::hours(3));
  TouchWithAge(root / "aaaaaaaaaaaa" / "ab" / "entry", std::chrono::hours(3));
  // A stale foreign identity: removed.
  TouchWithAge(root / "bbbbbbbbbbbb" / "last_used", std::chrono::hours(3));
  TouchWithAge(root / "bbbbbbbbbbbb" / "cd" / "entry", std::chrono::hours(3));
  // A fresh foreign identity: inside the grace window, a concurrent build may be using it.
  TouchWithAge(root / "cccccccccccc" / "last_used", std::chrono::seconds(10));
  // A legacy bucket from before identity directories: removed.
  TouchWithAge(root / "0d" / "legacy-entry", std::chrono::hours(3));

  const auto removed = PruneForeignIdentities(dir.dir(), "aaaaaaaaaaaa", 1, 3600);

  EXPECT_TRUE(fs::exists(root / "aaaaaaaaaaaa" / "ab" / "entry"));
  EXPECT_FALSE(fs::exists(root / "bbbbbbbbbbbb"));
  EXPECT_TRUE(fs::exists(root / "cccccccccccc"));
  EXPECT_FALSE(fs::exists(root / "0d"));
  EXPECT_GT(removed, 0u);
}

TEST(StageCachePruneTest, TheNewestForeignIdentitySurvivesTheKeepQuota) {
  ScratchDir dir("cache_prune_keep");
  const auto root = fs::path(dir.dir());
  TouchWithAge(root / "aaaaaaaaaaaa" / "last_used", std::chrono::hours(5));
  TouchWithAge(root / "bbbbbbbbbbbb" / "last_used", std::chrono::hours(2));

  PruneForeignIdentities(dir.dir(), "cccccccccccc", 1, 3600);

  EXPECT_FALSE(fs::exists(root / "aaaaaaaaaaaa"));
  EXPECT_TRUE(fs::exists(root / "bbbbbbbbbbbb"));
}

TEST(StageCachePruneTest, EvictToCapRemovesTheOldestEntriesFirst) {
  ScratchDir dir("cache_evict");
  const auto root = fs::path(dir.dir());
  // Five 100KB entries with increasing age; a 250KB cap evicts down to 200KB (80%).
  const std::string bytes(100 * 1024, 'x');
  for (int index = 0; index < 5; ++index) {
    const auto path = root / ("entry" + std::to_string(index));
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file << bytes;
    }
    std::error_code code;
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(10 - index),
                        code);
  }

  const auto removed = EvictToCap(dir.dir(), 250 * 1024);

  EXPECT_EQ(removed, 300u * 1024u);
  EXPECT_FALSE(fs::exists(root / "entry0"));
  EXPECT_FALSE(fs::exists(root / "entry1"));
  EXPECT_FALSE(fs::exists(root / "entry2"));
  EXPECT_TRUE(fs::exists(root / "entry3"));
  EXPECT_TRUE(fs::exists(root / "entry4"));
}

TEST(StageCachePruneTest, EvictToCapLeavesATreeUnderTheCapAlone) {
  ScratchDir dir("cache_evict_small");
  dir.write("entry", std::string(1000, 'x'));
  EXPECT_EQ(EvictToCap(dir.dir(), 4096), 0u);
}

}  // namespace
}  // namespace tgfx
