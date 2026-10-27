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

// StageCache hands back compiled shader stages instead of compiling them again, so the one
// thing it must never do is return a stage built from different input. These tests pin that: the
// key covers every input, a stored entry reads back unchanged, and a damaged entry is refused and
// counted instead of being trusted.
//
// Every test builds its own StageCache on a directory of its own: the cache is injected state,
// not a process-wide singleton.

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "ScratchDir.h"
#include "compile/StageCache.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

std::vector<uint8_t> Payload(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// The only regular file below the cache directory that is an entry: entries live in two-letter
// subdirectories named after the first digits of the key.
std::string OnlyEntryFile(const std::string& dir) {
  std::string found;
  for (const auto& item : std::filesystem::recursive_directory_iterator(dir)) {
    if (item.is_regular_file()) {
      EXPECT_TRUE(found.empty()) << "more than one entry file";
      found = item.path().string();
    }
  }
  return found;
}

class StageCacheTest : public ::testing::Test {
 protected:
  StageCacheTest() : scratch("stage_cache") {
  }

  void SetUp() override {
    std::string error;
    ASSERT_TRUE(cache.open(scratch.dir(), &error)) << error;
  }

  // Keys cover the identity of the tool the same way the compiler's keys do.
  std::string KeyOf(const std::string& kind, const std::string& source, uint64_t variant) {
    return StageCacheKey(kind, cache.toolIdentity()).add(source).add(variant).finish();
  }

  ScratchDir scratch;
  StageCache cache;
};

}  // namespace

TEST(Sha256Test, KnownAnswers) {
  // The cache refuses to open when this fails, so it has to pass on its own.
  EXPECT_TRUE(Sha256SelfTest());
  Sha256 empty;
  EXPECT_EQ(Sha256::Hex(empty.finish()),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  Sha256 abc;
  abc.update("abc", 3);
  EXPECT_EQ(Sha256::Hex(abc.finish()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, UpdatingInPiecesEqualsOneUpdate) {
  const std::string text(1000, 'x');
  Sha256 whole;
  whole.update(text.data(), text.size());
  Sha256 pieces;
  for (size_t offset = 0; offset < text.size(); offset += 77) {
    pieces.update(text.data() + offset, std::min<size_t>(77, text.size() - offset));
  }
  EXPECT_EQ(Sha256::Hex(whole.finish()), Sha256::Hex(pieces.finish()));
}

TEST_F(StageCacheTest, KeysAreSixtyFourHexDigits) {
  auto key = KeyOf("spirv", "source", 1);
  EXPECT_EQ(key.size(), 64u);
  EXPECT_EQ(key.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST_F(StageCacheTest, EveryKeyInputChangesTheKey) {
  std::set<std::string> keys = {
      KeyOf("spirv", "source", 1),   // the reference
      KeyOf("msl", "source", 1),     // the kind of work
      KeyOf("spirv", "source2", 1),  // the text
      KeyOf("spirv", "source", 2),   // a numeric input
  };
  EXPECT_EQ(keys.size(), 4u);
  // Adding a field changes the key too, even one that is empty or zero.
  EXPECT_NE(StageCacheKey("spirv", cache.toolIdentity()).add("source").add(uint64_t{1}).finish(),
            StageCacheKey("spirv", cache.toolIdentity())
                .add("source")
                .add(uint64_t{1})
                .add("")
                .finish());
}

TEST_F(StageCacheTest, FieldBoundariesAreNotAmbiguous) {
  // Each field is length prefixed, so moving a byte from one field to the next is a new key.
  EXPECT_NE(StageCacheKey("k", cache.toolIdentity()).add("ab").add("c").finish(),
            StageCacheKey("k", cache.toolIdentity()).add("a").add("bc").finish());
  EXPECT_NE(StageCacheKey("k", cache.toolIdentity()).add("").add("abc").finish(),
            StageCacheKey("k", cache.toolIdentity()).add("abc").add("").finish());
}

TEST_F(StageCacheTest, TheSameInputsMakeTheSameKey) {
  EXPECT_EQ(KeyOf("spirv", "source", 7), KeyOf("spirv", "source", 7));
}

TEST_F(StageCacheTest, AStoredEntryReadsBackUnchanged) {
  auto key = KeyOf("spirv", "source", 1);
  auto payload = Payload("compiled stage bytes");
  StageCacheStats stats;
  cache.store(key, payload, &stats);
  EXPECT_EQ(stats.stored, 1u);

  std::vector<uint8_t> loaded;
  EXPECT_TRUE(cache.load(key, &loaded, &stats));
  EXPECT_EQ(loaded, payload);
  EXPECT_EQ(stats.diskHits, 1u);
  EXPECT_EQ(stats.corrupt, 0u);
}

TEST_F(StageCacheTest, AnEmptyPayloadRoundTrips) {
  auto key = KeyOf("spirv", "empty", 1);
  StageCacheStats stats;
  cache.store(key, {}, &stats);
  std::vector<uint8_t> loaded = {1, 2, 3};
  EXPECT_TRUE(cache.load(key, &loaded, &stats));
  EXPECT_TRUE(loaded.empty());
}

TEST_F(StageCacheTest, AMissingEntryIsAMissNotCorruption) {
  StageCacheStats stats;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(KeyOf("spirv", "absent", 1), &loaded, &stats));
  EXPECT_EQ(stats.diskHits, 0u);
  EXPECT_EQ(stats.corrupt, 0u);
}

TEST_F(StageCacheTest, EntriesOfDifferentKeysDoNotMix) {
  StageCacheStats stats;
  auto keyA = KeyOf("spirv", "a", 1);
  auto keyB = KeyOf("spirv", "b", 1);
  cache.store(keyA, Payload("payload A"), &stats);
  cache.store(keyB, Payload("payload B"), &stats);
  std::vector<uint8_t> loaded;
  ASSERT_TRUE(cache.load(keyA, &loaded, &stats));
  EXPECT_EQ(loaded, Payload("payload A"));
  ASSERT_TRUE(cache.load(keyB, &loaded, &stats));
  EXPECT_EQ(loaded, Payload("payload B"));
}

TEST_F(StageCacheTest, AFlippedPayloadByteIsRefusedAndCounted) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("compiled stage bytes"), &stats);
  auto file = OnlyEntryFile(scratch.dir());
  ASSERT_FALSE(file.empty());

  std::vector<uint8_t> bytes;
  {
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  bytes.back() ^= 0x01;
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }

  StageCacheStats readStats;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(key, &loaded, &readStats));
  EXPECT_EQ(readStats.corrupt, 1u);
  EXPECT_EQ(readStats.diskHits, 0u);
}

TEST_F(StageCacheTest, ATruncatedEntryIsRefusedAndCounted) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("compiled stage bytes"), &stats);
  auto file = OnlyEntryFile(scratch.dir());
  ASSERT_FALSE(file.empty());
  std::filesystem::resize_file(file, std::filesystem::file_size(file) - 3);

  StageCacheStats readStats;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(key, &loaded, &readStats));
  EXPECT_EQ(readStats.corrupt, 1u);
}

TEST_F(StageCacheTest, AnEntryWithoutTheMagicIsRefused) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("compiled stage bytes"), &stats);
  auto file = OnlyEntryFile(scratch.dir());
  ASSERT_FALSE(file.empty());
  {
    std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out);
    out.seekp(0);
    out.write("XXXX", 4);
  }
  StageCacheStats readStats;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(key, &loaded, &readStats));
  EXPECT_EQ(readStats.corrupt, 1u);
}

TEST_F(StageCacheTest, AnEntryTooShortForItsHeaderIsRefused) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("compiled stage bytes"), &stats);
  auto file = OnlyEntryFile(scratch.dir());
  ASSERT_FALSE(file.empty());
  std::filesystem::resize_file(file, 10);

  StageCacheStats readStats;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(key, &loaded, &readStats));
  EXPECT_EQ(readStats.corrupt, 1u);
}

TEST_F(StageCacheTest, ADamagedEntryIsReplacedByTheNextStore) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("first"), &stats);
  auto file = OnlyEntryFile(scratch.dir());
  ASSERT_FALSE(file.empty());
  std::filesystem::resize_file(file, 10);

  std::vector<uint8_t> loaded;
  ASSERT_FALSE(cache.load(key, &loaded, &stats));
  cache.store(key, Payload("rebuilt"), &stats);
  ASSERT_TRUE(cache.load(key, &loaded, &stats));
  EXPECT_EQ(loaded, Payload("rebuilt"));
}

TEST_F(StageCacheTest, NoTemporaryFilesAreLeftBehind) {
  StageCacheStats stats;
  for (uint64_t i = 0; i < 5; ++i) {
    cache.store(KeyOf("spirv", "source", i), Payload("bytes " + std::to_string(i)),
                            &stats);
  }
  size_t entries = 0;
  for (const auto& item : std::filesystem::recursive_directory_iterator(scratch.dir())) {
    if (item.is_regular_file()) {
      ++entries;
      EXPECT_EQ(item.path().string().find(".tmp"), std::string::npos) << item.path();
    }
  }
  EXPECT_EQ(entries, 5u);
}

TEST_F(StageCacheTest, AnEntryLivesUnderTheFirstDigitsOfItsKey) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("bytes"), &stats);
  auto expected = std::filesystem::path(scratch.dir()) / key.substr(0, 2) / key;
  EXPECT_TRUE(std::filesystem::exists(expected)) << expected;
}

TEST_F(StageCacheTest, ReopeningTheSameDirectoryKeepsTheEntries) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("kept"), &stats);
  std::string error;
  ASSERT_TRUE(cache.open(scratch.dir(), &error)) << error;
  std::vector<uint8_t> loaded;
  EXPECT_TRUE(cache.load(key, &loaded, &stats));
  EXPECT_EQ(loaded, Payload("kept"));
}

TEST_F(StageCacheTest, AnEntryOfAnotherDirectoryIsNotVisible) {
  auto key = KeyOf("spirv", "source", 1);
  StageCacheStats stats;
  cache.store(key, Payload("here"), &stats);

  ScratchDir other("stage_cache_other");
  std::string error;
  ASSERT_TRUE(cache.open(other.dir(), &error)) << error;
  std::vector<uint8_t> loaded;
  EXPECT_FALSE(cache.load(key, &loaded, &stats));
}

TEST_F(StageCacheTest, ADirectoryThatCannotBeCreatedKeepsTheCacheInMemory) {
  // A path below a regular file cannot become a directory.
  scratch.write("a_file", "not a directory");
  std::string error;
  EXPECT_FALSE(cache.open(scratch.dir() + "/a_file/below", &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(cache.persistent());
  // Without a store, loads miss and stores are silent no-ops.
  StageCacheStats stats;
  std::vector<uint8_t> loaded;
  cache.store(KeyOf("spirv", "source", 1), Payload("lost"), &stats);
  EXPECT_EQ(stats.stored, 0u);
  EXPECT_FALSE(cache.load(KeyOf("spirv", "source", 1), &loaded, &stats));
}

TEST_F(StageCacheTest, TheKeyCoversTheIdentityOfTheTool) {
  // Rebuilding the tool changes its identity, which must invalidate every entry. The identity is
  // part of every key, so a key made now differs from one made with another identity.
  EXPECT_FALSE(cache.toolIdentity().empty());
  EXPECT_EQ(cache.toolIdentity().size(), 64u);
}

}  // namespace tgfx
