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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/BundleCodec.h"
#include <algorithm>
#include <cstring>

namespace tgfx {
namespace {

uint16_t ReadHeaderU16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadHeaderU32(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t ReadHeaderU64(const uint8_t* data, size_t offset) {
  return static_cast<uint64_t>(ReadHeaderU32(data, offset)) |
         (static_cast<uint64_t>(ReadHeaderU32(data, offset + 4)) << 32);
}

void WriteEntries(std::vector<uint8_t>& blob, const std::vector<BundleReflectionEntry>& entries) {
  for (const auto& entry : entries) {
    blob.push_back(static_cast<uint8_t>(entry.name.size()));
    blob.insert(blob.end(), entry.name.begin(), entry.name.end());
    blob.push_back(entry.format);
    // v4: array element count (1 for scalars).
    blob.push_back(static_cast<uint8_t>(entry.arraySize & 0xFF));
    blob.push_back(static_cast<uint8_t>((entry.arraySize >> 8) & 0xFF));
  }
}

// Reads count entries of one uniform-shaped list. The offset advances past what was consumed;
// every structural rule (bounds, non-empty NUL-free unique names) is checked here so neither
// side re-implements the walking.
bool ReadEntries(const uint8_t* data, size_t length, size_t* offset, uint8_t count,
                 std::vector<BundleReflectionEntry>* out) {
  out->reserve(count);
  for (uint8_t i = 0; i < count; i++) {
    if (*offset >= length) {
      return false;
    }
    uint8_t nameLen = data[(*offset)++];
    // The name is followed by the format byte and the two array-size bytes.
    if (nameLen == 0 || static_cast<size_t>(nameLen) + 3 > length - *offset) {
      return false;
    }
    std::string name(reinterpret_cast<const char*>(data + *offset), nameLen);
    if (name.find('\0') != std::string::npos ||
        std::any_of(out->begin(), out->end(),
                    [&name](const BundleReflectionEntry& entry) { return entry.name == name; })) {
      return false;
    }
    *offset += nameLen;
    auto format = data[(*offset)++];
    uint16_t arraySize = static_cast<uint16_t>(static_cast<uint16_t>(data[*offset]) |
                                               static_cast<uint16_t>(data[*offset + 1] << 8));
    *offset += 2;
    out->push_back(BundleReflectionEntry{std::move(name), format, arraySize});
  }
  return true;
}

}  // namespace

std::vector<uint8_t> SerializeBundleReflection(const BundleReflection& reflection) {
  std::vector<uint8_t> blob;
  blob.push_back(static_cast<uint8_t>(reflection.uniforms.size()));
  blob.push_back(static_cast<uint8_t>(reflection.samplers.size()));
  // v5: the vertex input count occupies the first reserved byte, so a v4 reader still sees a
  // byte-identical prefix with zero vertex inputs.
  blob.push_back(static_cast<uint8_t>(reflection.vertexInputs.size()));
  blob.push_back(0);  // reserved

  WriteEntries(blob, reflection.uniforms);
  WriteEntries(blob, reflection.samplers);
  for (const auto& entry : reflection.vertexInputs) {
    blob.push_back(static_cast<uint8_t>(entry.name.size()));
    blob.insert(blob.end(), entry.name.begin(), entry.name.end());
    blob.push_back(entry.format);
    blob.push_back(entry.location);
  }
  return blob;
}

bool ParseBundleHeader(const uint8_t* data, size_t size, BundleHeader* header) {
  if (data == nullptr || header == nullptr || size < BundleFormat::HeaderSize) {
    return false;
  }
  header->magic = ReadHeaderU32(data, BundleFormat::Offset::Magic);
  header->formatVersion = ReadHeaderU16(data, BundleFormat::Offset::FormatVersion);
  header->compressionType = ReadHeaderU16(data, BundleFormat::Offset::CompressionType);
  header->identityHash = ReadHeaderU64(data, BundleFormat::Offset::IdentityHash);
  header->toolchainABI = ReadHeaderU32(data, BundleFormat::Offset::ToolchainABI);
  header->vertPoolCount = ReadHeaderU32(data, BundleFormat::Offset::VertPoolCount);
  header->fragPoolCount = ReadHeaderU32(data, BundleFormat::Offset::FragPoolCount);
  header->vertPoolOffset = ReadHeaderU32(data, BundleFormat::Offset::VertPoolOffset);
  header->fragPoolOffset = ReadHeaderU32(data, BundleFormat::Offset::FragPoolOffset);
  header->dataOffset = ReadHeaderU32(data, BundleFormat::Offset::DataOffset);
  header->dataSize = ReadHeaderU32(data, BundleFormat::Offset::DataSize);
  header->reflectionOffset = ReadHeaderU32(data, BundleFormat::Offset::ReflectionOffset);
  const auto* tag = reinterpret_cast<const char*>(data + BundleFormat::Offset::ProfileTag);
  header->profileTag.assign(tag, strnlen(tag, BundleFormat::ProfileTagSize));
  header->payloadFormat = ReadHeaderU16(data, BundleFormat::Offset::PayloadFormat);
  return true;
}

bool ParseBundleReflection(const uint8_t* data, size_t length, BundleReflection* reflection) {
  if (length < 4) {
    return false;
  }
  size_t offset = 0;
  uint8_t uniformCount = data[offset++];
  uint8_t samplerCount = data[offset++];
  uint8_t vertexInputCount = data[offset++];
  offset += 1;  // reserved

  if (!ReadEntries(data, length, &offset, uniformCount, &reflection->uniforms) ||
      !ReadEntries(data, length, &offset, samplerCount, &reflection->samplers)) {
    return false;
  }
  reflection->vertexInputs.reserve(vertexInputCount);
  for (uint8_t i = 0; i < vertexInputCount; i++) {
    if (offset >= length) {
      return false;
    }
    uint8_t nameLen = data[offset++];
    // The name is followed by the format byte and the location byte.
    if (nameLen == 0 || static_cast<size_t>(nameLen) + 2 > length - offset) {
      return false;
    }
    std::string name(reinterpret_cast<const char*>(data + offset), nameLen);
    if (name.find('\0') != std::string::npos ||
        std::any_of(reflection->vertexInputs.begin(), reflection->vertexInputs.end(),
                    [&name](const BundleVertexInput& entry) { return entry.name == name; })) {
      return false;
    }
    offset += nameLen;
    auto format = data[offset++];
    auto location = data[offset++];
    reflection->vertexInputs.push_back(BundleVertexInput{std::move(name), format, location});
  }
  return true;
}

}  // namespace tgfx
