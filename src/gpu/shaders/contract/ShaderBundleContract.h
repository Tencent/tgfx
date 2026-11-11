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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tgfx {

// The contract layer shared by the offline shader_build_tool and the runtime loader. A contract
// header holds constants or pure functions only and may include nothing but the standard library,
// so neither side can pull runtime types into the other:
//
//   ShaderBundleContract.h  (this file)
//        ^                                   ^
//        |                                   |
//   shader_build_tool                    runtime loader
//   BundleWriter  BundleVerifier   PrecompiledShaderCache  UniformData  pipelines
//        (writes)      (checks)              (reads)
//
// It carries five facts that must agree byte for byte on both sides: the shader profile (the
// dialect a bundle is built for), the stage-key hash, the shader resource binding layout, the
// on-disk bundle format, and the bundle identity (content hash + toolchain ABI). Changing any of
// them is a bundle-format or toolchain-ABI change: bump CurrentVersion below for an on-disk layout
// change and/or ExpectedToolchainABI (see the end of this file) for a semantic change. Every
// build generates its own bundle, so there is nothing to republish: the bump takes effect the
// next time the bundles are built. The ABI change history lives in git log (search for
// "0x000100"): each bump's commit states what changed and why the older bundles are refused.

// ---------------------------------------------------------------------------------------------
// Shader profile: the dialect or binary format a bundle is built for.
//
// Several profiles can belong to one runtime Backend, for example the desktop and the ES flavor
// of OpenGL, so a profile is not a Backend. shader_build_tool writes one bundle per profile
// (--backends takes the tags below) and the runtime loads the bundle of the profile its context
// runs (see ShaderProfileResolver.h, which needs runtime types and lives outside this header).
//
// The tag strings are stored in every bundle header and are part of every stage key hash, so
// they are fixed forever. ShaderProfileTest pins them.
// D3D12 carries the Direct3D 5.0 shader model payload, as HLSL text or precompiled DXBC bytecode.
// The D3D11 backend consumes the same bundle when it is added: both API levels share the SM 5.0
// register model (b/t/s), and their binding differences live in the pipeline layer, not the
// shader, so one profile serves both rather than one profile per API.
enum class ShaderProfile { OpenGL, OpenGLES, Vulkan, Metal, WebGPU, D3D12 };

/// Every profile, in the order the tool reports them.
inline constexpr ShaderProfile AllShaderProfiles[] = {
    ShaderProfile::OpenGL, ShaderProfile::OpenGLES, ShaderProfile::Vulkan,
    ShaderProfile::Metal,  ShaderProfile::WebGPU,   ShaderProfile::D3D12};

/**
 * Returns the tag of the profile: the text stored in the bundle header and used for the bundle
 * file name (shader_bundle.<tag>.bin). Adding a profile makes the compiler flag every switch over
 * ShaderProfile that does not handle it, so none of them has a default case.
 */
inline const char* ProfileTag(ShaderProfile profile) {
  switch (profile) {
    case ShaderProfile::OpenGL:
      return "opengl";
    case ShaderProfile::OpenGLES:
      return "opengles";
    case ShaderProfile::Vulkan:
      return "vulkan";
    case ShaderProfile::Metal:
      return "metal";
    case ShaderProfile::WebGPU:
      return "webgpu";
    case ShaderProfile::D3D12:
      return "d3d12";
  }
  return "";
}

/**
 * Returns the profile with the given tag, or nullopt when no profile has it. Parsing user input
 * with this fails closed: a misspelled tag is rejected instead of silently selecting nothing.
 */
inline std::optional<ShaderProfile> ParseProfileTag(std::string_view tag) {
  for (auto profile : AllShaderProfiles) {
    if (tag == ProfileTag(profile)) {
      return profile;
    }
  }
  return std::nullopt;
}

/**
 * The single profile that serves a context: an ES-standard OpenGL context consumes the opengles
 * bundle even though its backend enum is OpenGL, and every other context consumes its backend's
 * profile. The bundle loader accepts exactly this profile, so a bundle built for the other GL
 * dialect is rejected at load time instead of being loaded and then having every draw refused at
 * match time.
 */
inline ShaderProfile RequiredProfile(ShaderProfile backendProfile, bool esProfileContext) {
  if (backendProfile == ShaderProfile::OpenGL && esProfileContext) {
    return ShaderProfile::OpenGLES;
  }
  return backendProfile;
}

/**
 * Returns true when draws on this profile can read the destination color through framebuffer
 * fetch, which is what the variants with the framebuffer fetch dst kind (HAS_XP == 2) use.
 * The bundle of a profile without it does not contain those variants: the runtime can never
 * request them, and the closure verifier must not report them missing.
 *
 * Metal reads the destination through the subpassInput to [[color(0)]] translation. OpenGLES uses
 * the GL_EXT_shader_framebuffer_fetch dialect. WGSL cannot express the fetch at all. Desktop
 * OpenGL has no such path. D3D12 has no such path either: SM 5.0 can only write SV_Target, never
 * read it, so its bundle excludes the fetch variants the same way. The Vulkan backend never wires
 * the chain: ShaderCaps leaves frameBufferFetchSupport false there and the render pass has no
 * input attachment binding, so a draw always resolves a dst texture. The Vulkan variants are
 * excluded, not deleted: the GLSL sources and the matcher rules keep enumerating them, so
 * supporting the fetch on Vulkan later needs the caps flag, the render pass wiring and this
 * function returning true.
 */
inline bool ProfileSupportsFramebufferFetch(ShaderProfile profile) {
  switch (profile) {
    case ShaderProfile::Metal:
    case ShaderProfile::OpenGLES:
      return true;
    case ShaderProfile::OpenGL:
    case ShaderProfile::Vulkan:
    case ShaderProfile::WebGPU:
    case ShaderProfile::D3D12:
      return false;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Stage-key hash: single source for the shader key/blob encoding shared by the runtime loader
// (PrecompiledShaderCache), the offline bundle writer (tools/shader_build_tool/BundleWriter), and
// the audit verifier (BundleVerifier). Header-only on purpose: the build tool links only
// src/gpu/shaders sources, so a .cpp here would be compiled twice or not at all on one side.
// Any change to the serialization order, the length prefixes, or the hash constants is a
// compatibility break between bundles and the runtime — see the fixed test vectors in
// test/src/ShaderKeyHashTest.cpp.

struct ShaderKeyHash {
  uint64_t hi = 0;
  uint64_t lo = 0;
};

namespace shader_key_hash_detail {

inline constexpr uint64_t FNV1A_SEED_HI = 0x6C62272E07BB0142ULL;
inline constexpr uint64_t FNV1A_SEED_LO = 0x62B821756295C58DULL;
inline constexpr uint64_t FNV1A_PRIME = 0x00000100000001B3ULL;

inline uint64_t Fnv1a64(const std::vector<uint8_t>& data, uint64_t seed) {
  uint64_t hash = seed;
  for (auto byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV1A_PRIME;
  }
  return hash;
}

// Serialized key layout: [nameLen:u32][name][permutationIndex:u32][tagLen:u32][tag], every length
// little-endian and measured in bytes.
inline std::vector<uint8_t> SerializeFieldsLE(const std::string& shaderName,
                                              uint32_t permutationIndex,
                                              const std::string& profileTag) {
  std::vector<uint8_t> bytes;
  auto nameLen = static_cast<uint32_t>(shaderName.size());
  bytes.push_back(static_cast<uint8_t>(nameLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), shaderName.begin(), shaderName.end());
  bytes.push_back(static_cast<uint8_t>(permutationIndex & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 24) & 0xFF));
  auto tagLen = static_cast<uint32_t>(profileTag.size());
  bytes.push_back(static_cast<uint8_t>(tagLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), profileTag.begin(), profileTag.end());
  return bytes;
}

}  // namespace shader_key_hash_detail

/// Computes a 128-bit FNV-1a hash for a shader key composed of shaderName, permutationIndex, and
/// profileTag. Same implementation on the runtime and build-tool sides (see the section comment).
inline ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                          const std::string& profileTag) {
  auto data = shader_key_hash_detail::SerializeFieldsLE(shaderName, permutationIndex, profileTag);
  ShaderKeyHash result;
  result.hi = shader_key_hash_detail::Fnv1a64(data, shader_key_hash_detail::FNV1A_SEED_HI);
  result.lo = shader_key_hash_detail::Fnv1a64(data, shader_key_hash_detail::FNV1A_SEED_LO);
  return result;
}

/// Computes a stable 128-bit content hash for a code or reflection blob using the same dual-seed
/// FNV-1a scheme as the shader keys.
inline ShaderKeyHash ComputeBlobHash(const std::vector<uint8_t>& blob) {
  ShaderKeyHash result;
  result.hi = shader_key_hash_detail::Fnv1a64(blob, shader_key_hash_detail::FNV1A_SEED_HI);
  result.lo = shader_key_hash_detail::Fnv1a64(blob, shader_key_hash_detail::FNV1A_SEED_LO);
  return result;
}

/// The suffix that tells a vertex stage key from a fragment stage key of the same shader name.
/// Every place that builds or prints a stage key must use these, not spell the text again.
inline constexpr char VertexStageSuffix[] = "_Vert";
inline constexpr char FragmentStageSuffix[] = "_Frag";

/// Computes the vertex shader key hash: hash(shaderName + "_Vert", permutationIndex, profileTag).
inline ShaderKeyHash ComputeVertexKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                          const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + VertexStageSuffix, permutationIndex, profileTag);
}

/// Computes the fragment shader key hash: hash(shaderName + "_Frag", permutationIndex, profileTag).
inline ShaderKeyHash ComputeFragmentKeyHash(const std::string& shaderName,
                                            uint32_t permutationIndex,
                                            const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + FragmentStageSuffix, permutationIndex, profileTag);
}

// ---------------------------------------------------------------------------------------------
// Shader resource binding layout: where the shader resources of one pipeline live, for the
// SPIR-V based backends (Vulkan, Metal, D3D12, WebGPU). The runtime pipelines create their
// descriptor sets or bind groups from these values, and the offline shader_build_tool reads the
// block names from the shaders it reflects.
//
// This section lives apart from UniformData.h, which is a runtime class that pulls in Color,
// ColorSpace and Matrix, so the shader tool can share the layout without linking runtime types.
// Changing a value changes the resource layout every precompiled shader was built against, so it
// is also a bundle format change: bump ExpectedToolchainABI.

/// The name of the uniform block every vertex stage declares.
inline constexpr char VertexUniformBlockName[] = "VertexUniformBlock";

/// The name of the uniform block every fragment stage declares.
inline constexpr char FragmentUniformBlockName[] = "FragmentUniformBlock";

// Descriptor set indices for SPIR-V based backends. The runtime pipelines build exactly three
// descriptor sets / bind groups — one per uniform block and one for the textures — and the
// precompiled shaders must declare their resources in the same three sets: vertex UBO in set 0,
// fragment UBO in set 1, textures in set 2. (The GLSL templates get these values injected as the
// TGFX_* contract macros below, so the numbers exist only in this file.)
inline constexpr int VERTEX_UBO_DESCRIPTOR_SET = 0;
inline constexpr int FRAGMENT_UBO_DESCRIPTOR_SET = 1;
inline constexpr int TEXTURE_DESCRIPTOR_SET = 2;

// The framebuffer-fetch subpass input of the FBF variants (HAS_XP == 2). Those variants only
// enter the Metal and GLES bundles — Metal's translation turns the subpass input into a
// [[color(N)]] read and the ES emitter deletes the declaration, so no stored form carries the
// set. Vulkan is excluded from the FBF variants today (ProfileSupportsFramebufferFetch); if it
// ever gains them, this must move off the texture set's bindings.
inline constexpr int FBF_INPUT_ATTACHMENT_SET = 3;

/// The binding of the vertex uniform block inside its descriptor set.
inline constexpr int VERTEX_UBO_BINDING_POINT = 0;

/// The binding of the fragment uniform block inside its descriptor set.
inline constexpr int FRAGMENT_UBO_BINDING_POINT = 1;

// Where the WebGPU rewrite packs the separated texture/sampler pairs of the texture set: pair i
// lives on bindings START + 2i (texture) and START + 2i + 1 (sampler). The runtime JIT rewrite
// and the render pipeline's bind-group expansion use the same packing.
inline constexpr int WEBGPU_TEXTURE_BINDING_START = 0;

/// The bindings of the separated texture/sampler pair i of one profile's texture set. WebGPU
/// packs the pairs interleaved from WEBGPU_TEXTURE_BINDING_START; the combined-sampler profiles
/// (GLSL, Vulkan SPIR-V, MSL) keep the template's declaration-order bindings, which reflection
/// reports dense from zero. HLSL joins them as register(t{i}) / register(s{i}) on the D3D12 root
/// signature, so the joined numbers are what the pipeline lays out.
inline std::pair<int, int> SamplerBinding(ShaderProfile profile, int i) {
  switch (profile) {
    case ShaderProfile::WebGPU:
      return {WEBGPU_TEXTURE_BINDING_START + 2 * i, WEBGPU_TEXTURE_BINDING_START + 2 * i + 1};
    case ShaderProfile::OpenGL:
    case ShaderProfile::OpenGLES:
    case ShaderProfile::Vulkan:
    case ShaderProfile::Metal:
    case ShaderProfile::D3D12:
      return {i, i};
  }
  return {i, i};
}

/// The contract macros injected into every compiled variant, as "NAME=value" strings: the GLSL
/// templates spell their layout qualifiers with these names, so the numbers above exist in the
/// sources only through this list. shader_build_tool prepends it to every stage's defines, and
/// the manifests' source digest covers it, so a value change rebuilds and invalidates bundles.
inline std::vector<std::string> BindingContractDefines() {
  return {"TGFX_VERTEX_UBO_SET=" + std::to_string(VERTEX_UBO_DESCRIPTOR_SET),
          "TGFX_FRAGMENT_UBO_SET=" + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET),
          "TGFX_TEXTURE_SET=" + std::to_string(TEXTURE_DESCRIPTOR_SET),
          "TGFX_VERTEX_UBO_BINDING=" + std::to_string(VERTEX_UBO_BINDING_POINT),
          "TGFX_FRAGMENT_UBO_BINDING=" + std::to_string(FRAGMENT_UBO_BINDING_POINT),
          "TGFX_FBF_INPUT_SET=" + std::to_string(FBF_INPUT_ATTACHMENT_SET)};
}

// ---------------------------------------------------------------------------------------------
// The on-disk format of a precompiled shader bundle (shader_bundle.<profile>.bin).
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
//
// Changing any value below is a format change: bump CurrentVersion, and bump
// ExpectedToolchainABI so every published bundle is rebuilt.
namespace BundleFormat {

/// The first four bytes of every bundle: "TGSF" read as a little endian integer.
inline constexpr uint32_t Magic = 0x54475346u;

/// The only version the writer produces and the runtime loader reads. Older versions carried no
/// array sizes (v3), no vertex input reflection (v4), no payload format field (v5), no spec table
/// (v6) or hash-keyed pool entries (v7); they are refused instead of read through compatibility
/// branches the loader no longer has. v8 keys the entries by the declaration table's variant id
/// instead of a name hash, so the bundle no longer carries shader names at all.
inline constexpr uint16_t CurrentVersion = 8;

/// The size of the fixed header.
inline constexpr uint32_t HeaderSize = 84;

/// The size of one pool entry: variantId(4) + dataOffset(4) + dataSize(4) + reflectionOffset(4).
inline constexpr uint32_t PoolEntrySize = 16;

/// How the data pool is stored. Stored in the header as a 16 bit value.
namespace Compression {
inline constexpr uint16_t None = 0;
inline constexpr uint16_t Zstd = 2;
/// The largest value the loader accepts.
inline constexpr uint16_t Max = Zstd;
}  // namespace Compression

/// The byte offsets of the header fields.
namespace Offset {
inline constexpr uint32_t Magic = 0;              // u32
inline constexpr uint32_t FormatVersion = 4;      // u16
inline constexpr uint32_t CompressionType = 6;    // u16
inline constexpr uint32_t IdentityHash = 8;       // u64, see the identity hash below
inline constexpr uint32_t ToolchainABI = 16;      // u32, see ExpectedToolchainABI
inline constexpr uint32_t VertPoolCount = 20;     // u32
inline constexpr uint32_t FragPoolCount = 24;     // u32
inline constexpr uint32_t VertPoolOffset = 28;    // u32
inline constexpr uint32_t FragPoolOffset = 32;    // u32
inline constexpr uint32_t DataOffset = 36;        // u32
inline constexpr uint32_t DataSize = 40;          // u32, uncompressed size of the data pool
inline constexpr uint32_t ReflectionOffset = 44;  // u32, 0 when the bundle has no reflection
inline constexpr uint32_t ProfileTag = 48;        // ProfileTagSize bytes, zero padded
inline constexpr uint32_t PayloadFormat = 80;     // u16, see Payload
}  // namespace Offset

// What the data pool holds for this profile. A profile's payload is uniform across its stages and
// variants, so the kind is a header field rather than a per-entry one: the runtime reads it once
// and picks ShaderCodeFormat from it instead of assuming a form per backend.
namespace Payload {
/// Driver-native source text: GLSL, MSL, WGSL or HLSL.
inline constexpr uint16_t Text = 0;
/// Precompiled container: SPIR-V words, a metallib or DXBC bytecode.
inline constexpr uint16_t Binary = 1;
/// The largest value the loader accepts.
inline constexpr uint16_t Max = Binary;
}  // namespace Payload

/// The size of the profile tag field, including the zero padding.
inline constexpr uint32_t ProfileTagSize = 32;

/// Limits of the reflection pool. A stage's reflection serializes its list counts and every name
/// length as one byte (see SerializeStageReflection), so these are format facts, not policy: the
/// writer, the loader and shader_build_tool's contract check all read them from here. A change
/// to a wider field must move them with the format version.
namespace Reflection {
inline constexpr uint32_t MaxListEntries = 255;  // uniforms or samplers per stage
inline constexpr uint32_t MaxNameLength = 255;   // bytes of one uniform/sampler/input name
}  // namespace Reflection

/// The byte offsets of the fields inside one pool entry.
namespace EntryOffset {
inline constexpr uint32_t VariantId = 0;          // u32, see VariantIdOf
inline constexpr uint32_t DataOffset = 4;         // u32, relative to the data pool
inline constexpr uint32_t DataSize = 8;           // u32
inline constexpr uint32_t ReflectionOffset = 12;  // u32, relative to the reflection pool
}  // namespace EntryOffset

static_assert(Offset::PayloadFormat == Offset::ProfileTag + ProfileTagSize,
              "the payload format field follows the profile tag");
// PayloadFormat is a u16 followed by a u16 of reserved space, keeping the header 4-byte aligned.
static_assert(Offset::PayloadFormat + 2 * sizeof(uint16_t) == HeaderSize,
              "the header fields must tile the header exactly");
static_assert(EntryOffset::ReflectionOffset + sizeof(uint32_t) == PoolEntrySize,
              "the entry fields must tile the pool entry exactly");

}  // namespace BundleFormat

/**
 * Returns true when a bundle of the given profile may declare the given payload form. The profile
 * fixes which forms are legal: Vulkan stores only SPIR-V words, and the GL and WGSL profiles store
 * only driver source, while Metal (MSL or metallib) and D3D12 (HLSL or DXBC) support both a
 * precompiled container and the driver-native source the driver compiles. A kind outside the
 * profile's set would make the runtime hand the blobs to the backend as the wrong format.
 */
inline bool ProfileAllowsPayload(ShaderProfile profile, uint16_t payloadFormat) {
  if (payloadFormat > BundleFormat::Payload::Max) {
    return false;
  }
  const bool binary = payloadFormat == BundleFormat::Payload::Binary;
  switch (profile) {
    case ShaderProfile::Vulkan:
      return binary;
    case ShaderProfile::OpenGL:
    case ShaderProfile::OpenGLES:
    case ShaderProfile::WebGPU:
      return !binary;
    case ShaderProfile::Metal:
    case ShaderProfile::D3D12:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Bundle identity: the content hash and the toolchain ABI.
//
// A bundle's identity hash is a 64-bit FNV-1a over the serialized pool entries, the uncompressed
// data pool, the reflection pool, the profile tag, and the counts. Any shader-content or
// reflection-layout change produces a different hash even when shader names and permutation
// indices stay the same, which is exactly the compatibility gap a format version cannot express.
// Both sides must feed the exact same field sequence; the helpers below are the single source of
// truth for that sequence. The mapping from a runtime Backend to a shader profile needs runtime
// types and lives in ShaderProfileResolver.h.

/**
 * The toolchain ABI version every bundle records (header offset 16). This is the compatibility
 * gate for everything the content identity hash cannot express: the reflection layout grammar,
 * the uniform block contracts, and the rule semantics the runtime matchers assume when they turn
 * a pool entry into a program. Bump this constant whenever any of those contracts change in a
 * way that keeps shader names and pool indices stable but changes what the bytes mean; a GLSL
 * function-body edit that leaves every field, name and index identical does not bump it (the
 * identity hash covers it). The writer picks the new value up automatically and the loader then
 * refuses every older bundle instead of feeding mismatched layouts to the GPU.
 *
 * Every build generates its bundle together with the library, so an ABI bump cannot hand the
 * runtime a stale bundle: the next build simply records the new value everywhere. The loader
 * still refuses bundles whose recorded ABI is not this value, which guards hand-carried or
 * cached bundle files.
 */
inline constexpr uint32_t ExpectedToolchainABI = 0x00010020u;

inline uint64_t BundleIdentityHashInit() {
  return 0x54475346424E4443ULL;  // "TGSF BNDC"
}

inline uint64_t BundleIdentityHashBytes(uint64_t state, const void* data, size_t size) {
  auto* bytes = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    state ^= bytes[i];
    state *= 0x100000001B3ULL;
  }
  return state;
}

// The header-level fields are fed little-endian, matching the on-disk serialization order:
// formatVersion(2) + vertPoolCount(4) + fragPoolCount(4) + profileTag(32, zero padded).
inline uint64_t BundleIdentityHashHeader(uint64_t state, uint16_t formatVersion,
                                         uint32_t vertPoolCount, uint32_t fragPoolCount,
                                         const uint8_t* profileTag32) {
  uint8_t le[4] = {};
  le[0] = static_cast<uint8_t>(formatVersion);
  le[1] = static_cast<uint8_t>(formatVersion >> 8);
  state = BundleIdentityHashBytes(state, le, 2);
  for (uint32_t value : {vertPoolCount, fragPoolCount}) {
    le[0] = static_cast<uint8_t>(value);
    le[1] = static_cast<uint8_t>(value >> 8);
    le[2] = static_cast<uint8_t>(value >> 16);
    le[3] = static_cast<uint8_t>(value >> 24);
    state = BundleIdentityHashBytes(state, le, 4);
  }
  return BundleIdentityHashBytes(state, profileTag32, 32);
}

// Pool entries are fed in the on-disk field order: variantId(4) + dataOffset(4) + dataSize(4) +
// reflOffset(4), all little-endian.
inline uint64_t BundleIdentityHashEntry(uint64_t state, uint32_t variantId, uint32_t dataOffset,
                                        uint32_t dataSize, uint32_t reflOffset) {
  auto feedU32 = [&state](uint32_t value) {
    uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                     static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    state = BundleIdentityHashBytes(state, le, 4);
  };
  feedU32(variantId);
  feedU32(dataOffset);
  feedU32(dataSize);
  feedU32(reflOffset);
  return state;
}

}  // namespace tgfx
