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

#include "output/BundleWriter.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include "core/DiagnosticCodes.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "output/OutputFile.h"
#include "zstd.h"

namespace tgfx {

namespace {

void ReportWriteFailure(Diagnostics* diagnostics, const char* code, const std::string& shader,
                        const std::string& profile, int vert, int frag, std::string message) {
  if (diagnostics == nullptr) {
    return;
  }
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.layer = Layer::Output;
  diagnostic.code = code;
  diagnostic.shader = shader;
  diagnostic.profile = profile;
  diagnostic.vertPermutationIndex = vert;
  diagnostic.fragPermutationIndex = frag;
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

}  // namespace

static void WriteU16LE(std::ostream& out, uint16_t val) {
  uint8_t buf[2] = {static_cast<uint8_t>(val & 0xFF), static_cast<uint8_t>((val >> 8) & 0xFF)};
  out.write(reinterpret_cast<char*>(buf), 2);
}

static void WriteU32LE(std::ostream& out, uint32_t val) {
  uint8_t buf[4];
  buf[0] = static_cast<uint8_t>(val & 0xFF);
  buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
  out.write(reinterpret_cast<char*>(buf), 4);
}

static void WriteU64LE(std::ostream& out, uint64_t val) {
  uint8_t buf[8];
  for (int i = 0; i < 8; i++) {
    buf[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
  }
  out.write(reinterpret_cast<char*>(buf), 8);
}

struct PoolEntry {
  uint32_t variantId = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
};

// The header and pool entry layout is defined in gpu/ShaderBundleContract.h, shared with the verifier and
// the runtime loader. The header is written field by field in that order, and WriteBundle checks
// the written size against BundleFormat::HeaderSize.

bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants, bool compress,
                 Diagnostics* diagnostics) {
  if (variants.empty()) {
    return true;
  }

  // The payload kind is a header field, so every variant of a bundle must agree on it: a bundle
  // that mixed text and binary blobs would have no single answer for what the data pool holds.
  const uint16_t payloadKind = variants.front().payloadKind;
  for (const auto& v : variants) {
    if (v.payloadKind != payloadKind) {
      ReportWriteFailure(diagnostics, DiagnosticCode::OutputWrite, "", profileTag, -1, -1,
                         "the variants disagree on the payload kind; one bundle cannot hold both "
                         "text and binary code blobs");
      return false;
    }
  }

  // One entry per variant and pool, keyed by the id the compile step stamped. The bundle carries
  // no shader name and no interface description — the runtime derives that from the declaration
  // table — so an entry is the id and the compiled bytes.
  std::vector<uint8_t> dataPool;
  std::vector<PoolEntry> vertPool;
  std::vector<PoolEntry> fragPool;
  std::set<uint32_t> seenIds;

  auto addBlob = [&](const std::vector<uint8_t>& blob, PoolEntry* entry) {
    entry->dataOffset = static_cast<uint32_t>(dataPool.size());
    entry->dataSize = static_cast<uint32_t>(blob.size());
    dataPool.insert(dataPool.end(), blob.begin(), blob.end());
  };

  for (const auto& v : variants) {
    // A repeated id would make the runtime's lookup ambiguous; the compile step derives the id
    // from (family, ordinal), so a collision here means a caller bug, not a legal duplicate.
    if (!seenIds.insert(v.variantId).second) {
      ReportWriteFailure(diagnostics, DiagnosticCode::OutputDuplicateKey, v.shaderName,
                         v.profileTag, static_cast<int>(v.vertPermutationIndex),
                         static_cast<int>(v.fragPermutationIndex),
                         "two variants carry the same bundle id; the bundle cannot hold both");
      return false;
    }
    PoolEntry vert;
    vert.variantId = v.variantId;
    addBlob(v.vertexBlob, &vert);
    vertPool.push_back(vert);
    PoolEntry frag;
    frag.variantId = v.variantId;
    addBlob(v.fragmentBlob, &frag);
    fragPool.push_back(frag);
  }

  // Sort pools by variant id for deterministic output.
  auto hashCompare = [](const PoolEntry& a, const PoolEntry& b) {
    return a.variantId < b.variantId;
  };
  std::sort(vertPool.begin(), vertPool.end(), hashCompare);
  std::sort(fragPool.begin(), fragPool.end(), hashCompare);

  // Compute layout offsets
  auto vertPoolCount = static_cast<uint32_t>(vertPool.size());
  auto fragPoolCount = static_cast<uint32_t>(fragPool.size());
  uint32_t vertPoolOffset = BundleFormat::HeaderSize;
  uint32_t fragPoolOffset = vertPoolOffset + vertPoolCount * BundleFormat::PoolEntrySize;
  uint32_t dataOffset = fragPoolOffset + fragPoolCount * BundleFormat::PoolEntrySize;
  uint32_t dataSize = static_cast<uint32_t>(dataPool.size());

  // Serialize in memory; the file is only replaced when its bytes change (see WriteFileIfChanged).
  std::ostringstream file(std::ios::binary);

  // Optionally compress the data pool with zstd.
  std::vector<uint8_t> compressedData;
  uint16_t compressionFlag = BundleFormat::Compression::None;
  const uint8_t* dataToWrite = dataPool.data();
  size_t dataToWriteSize = dataPool.size();
  if (compress && !dataPool.empty()) {
    size_t compBound = ZSTD_compressBound(dataPool.size());
    compressedData.resize(compBound);
    size_t compSize = ZSTD_compress(compressedData.data(), compBound, dataPool.data(),
                                    dataPool.size(), ZSTD_maxCLevel());
    if (!ZSTD_isError(compSize) && compSize < dataPool.size()) {
      compressedData.resize(compSize);
      dataToWrite = compressedData.data();
      dataToWriteSize = compSize;
      compressionFlag = BundleFormat::Compression::Zstd;
    }
    // If compression didn't reduce size, fall back to uncompressed.
  }

  // Identity hash: covers the pool entries, the uncompressed data pool, the profile tag and the
  // counts. The runtime loader recomputes this over the same fields and rejects a bundle whose
  // hash does not match, closing the gap where a same-content-length bundle would load fine.
  char tagBuf[32] = {};
  std::strncpy(tagBuf, profileTag.c_str(), sizeof(tagBuf) - 1);
  uint64_t identityHash = BundleIdentityHashInit();
  identityHash = BundleIdentityHashHeader(identityHash, BundleFormat::CurrentVersion, vertPoolCount,
                                          fragPoolCount, reinterpret_cast<const uint8_t*>(tagBuf));
  for (const auto& entry : vertPool) {
    identityHash =
        BundleIdentityHashEntry(identityHash, entry.variantId, entry.dataOffset, entry.dataSize);
  }
  for (const auto& entry : fragPool) {
    identityHash =
        BundleIdentityHashEntry(identityHash, entry.variantId, entry.dataOffset, entry.dataSize);
  }
  identityHash = BundleIdentityHashBytes(identityHash, dataPool.data(), dataPool.size());

  // Header, in the order of BundleFormat::Offset.
  WriteU32LE(file, BundleFormat::Magic);
  WriteU16LE(file, BundleFormat::CurrentVersion);
  WriteU16LE(file, compressionFlag);
  WriteU64LE(file, identityHash);  // bundle identity, see above
  WriteU32LE(file, ExpectedToolchainABI);
  WriteU32LE(file, vertPoolCount);
  WriteU32LE(file, fragPoolCount);
  WriteU32LE(file, vertPoolOffset);
  WriteU32LE(file, fragPoolOffset);
  WriteU32LE(file, dataOffset);
  WriteU32LE(file, dataSize);  // uncompressed
  static_assert(sizeof(tagBuf) == BundleFormat::ProfileTagSize, "the tag buffer is the tag field");
  file.write(tagBuf, sizeof(tagBuf));
  WriteU16LE(file, payloadKind);
  WriteU16LE(file, 0);  // reserved, keeps the header 4-byte aligned
  if (static_cast<std::streamoff>(file.tellp()) !=
      static_cast<std::streamoff>(BundleFormat::HeaderSize)) {
    ReportWriteFailure(diagnostics, DiagnosticCode::OutputWrite, "", profileTag, -1, -1,
                       "the written header does not match BundleFormat::HeaderSize");
    return false;
  }

  // Vert pool entries
  for (const auto& entry : vertPool) {
    WriteU32LE(file, entry.variantId);
    WriteU32LE(file, entry.dataOffset);
    WriteU32LE(file, entry.dataSize);
  }

  // Frag pool entries
  for (const auto& entry : fragPool) {
    WriteU32LE(file, entry.variantId);
    WriteU32LE(file, entry.dataOffset);
    WriteU32LE(file, entry.dataSize);
  }

  // Data pool (possibly compressed)
  file.write(reinterpret_cast<const char*>(dataToWrite),
             static_cast<std::streamsize>(dataToWriteSize));

  // A failed write (disk full, permission) must not report success: the caller would treat the
  // truncated or missing file as a valid bundle.
  std::string error;
  if (!WriteFileIfChanged(outPath, file.str(), nullptr, &error)) {
    ReportWriteFailure(diagnostics, DiagnosticCode::OutputWrite, "", profileTag, -1, -1,
                       "cannot write the bundle: " + error);
    return false;
  }
  return true;
}

}  // namespace tgfx
