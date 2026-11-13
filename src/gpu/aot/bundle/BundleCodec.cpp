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

#include "gpu/aot/bundle/BundleCodec.h"
#include <cstring>

namespace tgfx {

bool ParseBundleHeader(const uint8_t* data, size_t size, BundleHeader* header) {
  if (data == nullptr || header == nullptr || size < BundleFormat::HeaderSize) {
    return false;
  }
  header->magic = ReadBundleU32LE(data, BundleFormat::Offset::Magic);
  header->formatVersion = ReadBundleU16LE(data, BundleFormat::Offset::FormatVersion);
  header->compressionType = ReadBundleU16LE(data, BundleFormat::Offset::CompressionType);
  header->identityHash = ReadBundleU64LE(data, BundleFormat::Offset::IdentityHash);
  header->toolchainABI = ReadBundleU32LE(data, BundleFormat::Offset::ToolchainABI);
  header->vertPoolCount = ReadBundleU32LE(data, BundleFormat::Offset::VertPoolCount);
  header->fragPoolCount = ReadBundleU32LE(data, BundleFormat::Offset::FragPoolCount);
  header->vertPoolOffset = ReadBundleU32LE(data, BundleFormat::Offset::VertPoolOffset);
  header->fragPoolOffset = ReadBundleU32LE(data, BundleFormat::Offset::FragPoolOffset);
  header->dataOffset = ReadBundleU32LE(data, BundleFormat::Offset::DataOffset);
  header->dataSize = ReadBundleU32LE(data, BundleFormat::Offset::DataSize);
  const auto* tag = reinterpret_cast<const char*>(data + BundleFormat::Offset::ProfileTag);
  header->profileTag.assign(tag, strnlen(tag, BundleFormat::ProfileTagSize));
  header->payloadFormat = ReadBundleU16LE(data, BundleFormat::Offset::PayloadFormat);
  return true;
}

}  // namespace tgfx
