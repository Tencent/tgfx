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

namespace tgfx {

// The on-disk format of a precompiled shader bundle (shader_bundle.<profile>.bin).
//
// Layering. This header belongs to the contract layer shared by the offline shader_build_tool and
// the runtime loader. A contract header holds constants or pure functions only and may include
// nothing but the standard library, so neither side can pull runtime types into the other:
//
//   contract layer   BundleFormat.h  PrecompiledBundleIdentity.h  ShaderKeyHash.h  KernelContract.h
//        ^                 ^                      ^
//        |                 |                      |
//   shader_build_tool      |                runtime loader
//   BundleWriter  BundleVerifier       PrecompiledShaderCache
//        (writes)      (checks)             (reads)
//
// The writer, the verifier and the loader all read the layout from here. Changing any value is a
// format change: bump CurrentVersion, and bump ExpectedToolchainABI in PrecompiledBundleIdentity.h
// so every published bundle is rebuilt.
//
// File layout, all integers little endian:
//
//   [ header : HeaderSize ][ vertex pool ][ fragment pool ][ data pool ][ reflection pool ]
//
//   header          the fields at the Offset values below.
//   vertex pool     vertPoolCount entries of PoolEntrySize bytes, sorted by stage key.
//   fragment pool   fragPoolCount entries of PoolEntrySize bytes, sorted by stage key.
//   data pool       the shader code blobs, compressed as a whole when compressionType is not None.
//   reflection pool the serialized reflection of every stage (see SerializeStageReflection).
namespace BundleFormat {

/// The first four bytes of every bundle: "TGSF" read as a little endian integer.
inline constexpr uint32_t Magic = 0x54475346u;

/// The version this writer produces. Version 5 added the vertex input reflection entries.
inline constexpr uint16_t CurrentVersion = 5;

/// The oldest version the runtime loader still reads. Version 4 added the array size to every
/// reflection entry and version 5 the vertex inputs, so older versions parse as scalars without
/// vertex inputs.
inline constexpr uint16_t MinReadableVersion = 3;

/// The oldest version shader_build_tool --verify-bundle accepts. Published bundles may predate
/// CurrentVersion, but the verifier rejects the legacy version 3, which carries no array sizes.
inline constexpr uint16_t MinVerifiableVersion = 4;

/// The first version whose reflection entries carry an array size.
inline constexpr uint16_t ArraySizeSinceVersion = 4;

/// The first version whose reflection carries the vertex stage inputs.
inline constexpr uint16_t VertexInputsSinceVersion = 5;

/// The size of the fixed header.
inline constexpr uint32_t HeaderSize = 80;

/// The size of one pool entry: hashHi(8) + hashLo(8) + dataOffset(4) + dataSize(4) +
/// reflectionOffset(4).
inline constexpr uint32_t PoolEntrySize = 28;

/// How the data pool is stored. Stored in the header as a 16 bit value.
namespace Compression {
inline constexpr uint16_t None = 0;
/// The legacy zlib path, still readable.
inline constexpr uint16_t Zlib = 1;
inline constexpr uint16_t Zstd = 2;
/// The largest value the loader accepts.
inline constexpr uint16_t Max = Zstd;
}  // namespace Compression

/// The byte offsets of the header fields.
namespace Offset {
inline constexpr uint32_t Magic = 0;              // u32
inline constexpr uint32_t FormatVersion = 4;      // u16
inline constexpr uint32_t CompressionType = 6;    // u16
inline constexpr uint32_t IdentityHash = 8;       // u64, see PrecompiledBundleIdentity.h
inline constexpr uint32_t ToolchainABI = 16;      // u32, see ExpectedToolchainABI
inline constexpr uint32_t VertPoolCount = 20;     // u32
inline constexpr uint32_t FragPoolCount = 24;     // u32
inline constexpr uint32_t VertPoolOffset = 28;    // u32
inline constexpr uint32_t FragPoolOffset = 32;    // u32
inline constexpr uint32_t DataOffset = 36;        // u32
inline constexpr uint32_t DataSize = 40;          // u32, uncompressed size of the data pool
inline constexpr uint32_t ReflectionOffset = 44;  // u32, 0 when the bundle has no reflection
inline constexpr uint32_t ProfileTag = 48;        // ProfileTagSize bytes, zero padded
}  // namespace Offset

/// The size of the profile tag field, including the zero padding.
inline constexpr uint32_t ProfileTagSize = 32;

/// The byte offsets of the fields inside one pool entry.
namespace EntryOffset {
inline constexpr uint32_t HashHi = 0;             // u64
inline constexpr uint32_t HashLo = 8;             // u64
inline constexpr uint32_t DataOffset = 16;        // u32, relative to the data pool
inline constexpr uint32_t DataSize = 20;          // u32
inline constexpr uint32_t ReflectionOffset = 24;  // u32, relative to the reflection pool
}  // namespace EntryOffset

static_assert(Offset::ProfileTag + ProfileTagSize == HeaderSize,
              "the header fields must tile the header exactly");
static_assert(EntryOffset::ReflectionOffset + sizeof(uint32_t) == PoolEntrySize,
              "the entry fields must tile the pool entry exactly");
static_assert(MinReadableVersion <= MinVerifiableVersion && MinVerifiableVersion <= CurrentVersion,
              "the version range must be ordered");

}  // namespace BundleFormat
}  // namespace tgfx
