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
#include "tgfx/gpu/Backend.h"

namespace tgfx {

// Shared identity contract between the offline bundle writer (tools/shader_build_tool) and the
// runtime loader (PrecompiledShaderCache). A bundle's identity hash is a 64-bit FNV-1a over
// the serialized pool entries, the uncompressed data pool, the reflection pool, the profile
// tag, and the counts. Any shader-content or reflection-layout change produces a different
// hash even when shader names and permutation indices stay the same, which is exactly the
// compatibility gap a format version cannot express. Both sides must feed the exact same
// field sequence; the helpers below are the single source of truth for that sequence.

/**
 * The toolchain ABI version every bundle records (header offset 16). This is the compatibility
 * gate for everything the content identity hash cannot express on old bundles (hash 0): the
 * reflection layout grammar, the uniform block contracts, and the rule semantics the runtime
 * matchers assume when they turn a pool entry into a program. Bump this constant whenever any of
 * those contracts change in a way that keeps shader names and pool indices stable but changes
 * what the bytes mean; the writer picks the new value up automatically and the loader then
 * refuses every older bundle instead of feeding mismatched layouts to the GPU.
 *
 * Bumping this constant also makes the bundles published under resources/shaders stale: they record
 * the old value, and builds that consume them (cross builds on a Linux host, Web with
 * TGFX_WEB_PREBUILT_BUNDLES) would hand the runtime a bundle it refuses, which then falls back to
 * the runtime shader path on every draw. Republish them in the SAME commit as the bump, with
 * tools/shader_build_tool/publish_bundles.sh (or the tgfx_publish_shader_bundles target). The
 * CI step "Verify Published Shader Bundles" fails the commit that forgets: it rejects a bundle
 * whose recorded ABI is not this value. The same goes for any change to a shader source or to its
 * variant rules, which changes the source digest the manifests record.
 *
 * 0x00010005: the chain kernel's FragmentUniformBlock gained TiledLeafIndex2 (two shader-tiled
 * leaves sharing one recipe block). Bundles from before that change describe a uniform layout
 * without the second selector; a new runtime fed such a bundle would silently leave the second
 * tiled leaf on the plain Subset-clamp path (a semantic downgrade, not a crash), so they are
 * refused instead.
 *
 * 0x00010006: GlassRefractionShader turned dispersion into the DISPERSION_ON compile-time
 * dimension and dropped the DispersionOn uniform. Both halves of that change are invisible to
 * the identity hash: inserting a dimension shifts every fragment permutation index (an old
 * index names a different variant now), and the uniform block lost a field the old kernels
 * still read. A new runtime fed a pre-change bundle would bind a mismatched variant and leave
 * that field uninitialized, so those bundles are refused.
 *
 * 0x00010007: the chain kernel's second tiled leaf reads its own recipe (the Tiled*2 uniform set)
 * instead of sharing the first leaf's. A pre-change kernel would apply the first leaf's recipe to
 * the second leaf, clamping it to the wrong domain, while the runtime now admits leaves with
 * different recipes, so those bundles are refused.
 *
 * 0x00010008: pointwise resource profiles become explicit admission and program-key contracts;
 * PointwiseGraphShader gains a no-noise, three-tiled-recipe uniform-array layout.
 *
 * 0x00010009: gradients move to extended chains, which gain the base geometry interfaces;
 * base chains no longer carry the source coordinate transform or gradient parameters.
 *
 * 0x0001000A: chain leaf and standalone tiled sampling gain runtime read-swizzle uniforms.
 *
 * 0x0001000B: GlassRefractionShader's quad form gains an optional local shape mask (the
 * HasLocalMask runtime branch, LocalMaskSampler, and the CoordTransformMatrix_1 transform), and the
 * matcher now admits the masked draw. A pre-change kernel has none of them, so it would draw the
 * glass over the whole rect with the mask silently ignored; those bundles are refused.
 *
 * 0x0001000C: ComplexEllipseFillShader gained the runtime clip contract (the coverage_uniforms.inc
 * fields in an always-present FragmentUniformBlock), and the matcher now admits a sole AA-rect or
 * rrect coverage FP. A pre-change kernel has no clip fields, so feeding it such a draw would leave
 * HasClip uninitialized and the clip silently ignored; those bundles are refused.
 *
 * 0x0001000D: ComplexEllipseFillShader gained the HAS_DEVICE_MASK dimension and its sampler binding
 * (shifting the terminal XP dst texture one binding up), and the matcher now admits a device-space
 * mask coverage FP. Inserting a fragment dimension shifts every fragment permutation index, so a
 * pre-change bundle would bind a different variant and ignore the mask; those bundles are refused.
 *
 * 0x0001000E: the device mask became a runtime uniform (HasDeviceMask) over an always-declared
 * MaskTextureSampler instead of the HAS_DEVICE_MASK dimension: the family is rare, and the
 * dimension doubled every stroke/common-color/XP variant for it. The uniform block gains
 * DeviceCoordMatrix/HasDeviceMask and the terminal XP dst texture permanently moves to binding 1,
 * which a 0x0001000D kernel does not describe.
 *
 * 0x0001000F: ShapeInstancedTextureCoverageShader gained the runtime clip contract (the
 * coverage_uniforms.inc fields) and the matcher now admits an analytic clip coverage FP beside the
 * coverage texture. A pre-change kernel has no clip fields, so HasClip would read uninitialized and
 * the clip would be silently ignored.
 *
 * 0x00010010: the pointwise kernels gained the OutputAlphaSwizzle uniform (alpha-only render
 * targets), and the AAAA matcher branch now admits the pointwise DAG kernels. A pre-change kernel
 * lacks the field, so an alpha-only target would read it uninitialized and write RGBA.
 *
 * 0x00010011: the pointwise kernels gained the hairline-line GP layout (GP_LAYOUT=2: the
 * edge-distance varying, the Coverage/AAEnabled uniforms and the leaf-free device-mask variant),
 * and the matcher now routes HairlineLineGeometryProcessor draws carrying color or coverage FPs
 * onto them. Widening GP_LAYOUT from two to three values re-encodes every pointwise permutation
 * index, so a pre-change bundle would bind a different variant; those bundles are refused.
 *
 * 0x00010012: the pointwise kernels gained the complex rounded-rect GP layouts (GP_LAYOUT=3 fill,
 * 4 stroke: ComplexEllipseGeometryProcessor's offset/radii/edge-distance varyings, plus the stroke
 * width for 4), and the matcher now routes ComplexEllipseGeometryProcessor draws carrying color or
 * composed coverage FPs onto them. GP_LAYOUT widens from three to five values (re-encoding every
 * pointwise permutation index), and those layouts move the graph kernel's SourceCoords varying
 * from location 7 to 9; pre-change bundles are refused.
 *
 * 0x00010013: NonAARRectFill and RoundStrokeRectFill gained the runtime clip contract fields
 * (Rect/HasClip/LocalRect/RadiiX/RadiiY/AntiAlias/DeviceToLocal) and multiply clipCoverage() into
 * their output coverage, so a draw carrying a sole analytic clip is served by the dedicated kernel
 * instead of falling back. The fragment uniform blocks of both kernels now always exist and carry
 * those fields, so a pre-change bundle would bind the previous layout and is refused.
 *
 * 0x00010014: the pointwise kernels' CoverageRect / CoverageLocalRect /
 * CoverageLocalDeviceToLocal uniforms became two-element arrays indexed by the slot's ordinal, so a
 * draw whose clip stack holds two rects in one space (two analytic clips, or a clip over a shape
 * mask) is served by one chain pass instead of falling back. Every pointwise variant's uniform
 * block layout changed, so pre-change bundles are refused.
 *
 * 0x00010015: the NonAARRect, ComplexNonAARRect and RoundStrokeRect kernels made the color
 * attribute unconditional (common-color batches broadcast the paint color into the slot; the exact
 * color rides the Color uniform through the HasCommonColor runtime uniform), dropping the
 * HAS_COMMON_COLOR dimension. Every variant of the three families changes its vertex layout and
 * fragment uniform block, and the removed axis re-encodes their permutation indices; pre-change
 * bundles are refused.
 *
 * 0x00010016: the RoundStrokeRect kernel made the coverage and ellipse-radii attributes
 * unconditional (non-AA draws write a 1.0 coverage and zero radii, with the AA ramp selected by
 * the AAEnabled runtime uniform), dropping the HAS_AA dimension. Every variant changes its vertex
 * layout and fragment uniform block, and the removed axis re-encodes the family's permutation
 * indices; pre-change bundles are refused.
 *
 * 0x00010017: the non-AA path triangles gained a constant 1.0 coverage slot (they share the AA
 * vertex layout), and the DefaultGP and ShapeInstancedGP coverage attributes became
 * unconditional, so the SolidColor, TexturedEffect, TiledTexture, PerlinNoise and
 * ShapeInstancedFill kernels dropped their HAS_COVERAGE / HAS_AA dimensions and the pointwise
 * chain's DefaultGP forms dropped their coverage-free variants. Every affected family changes its
 * vertex layout, and the removed axes re-encode their permutation indices; pre-change bundles are
 * refused.
 */
inline constexpr uint32_t kExpectedToolchainABI = 0x00010017u;

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

// Pool entries are fed in the on-disk field order: hashHi(8) + hashLo(8) + dataOffset(4) +
// dataSize(4) + reflOffset(4), all little-endian.
inline uint64_t BundleIdentityHashEntry(uint64_t state, uint64_t hashHi, uint64_t hashLo,
                                        uint32_t dataOffset, uint32_t dataSize,
                                        uint32_t reflOffset) {
  auto feedU64 = [&state](uint64_t value) {
    uint8_t le[8] = {};
    for (int i = 0; i < 8; ++i) {
      le[i] = static_cast<uint8_t>(value >> (i * 8));
    }
    state = BundleIdentityHashBytes(state, le, 8);
  };
  auto feedU32 = [&state](uint32_t value) {
    uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                     static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    state = BundleIdentityHashBytes(state, le, 4);
  };
  feedU64(hashHi);
  feedU64(hashLo);
  feedU32(dataOffset);
  feedU32(dataSize);
  feedU32(reflOffset);
  return state;
}

// Maps a runtime Backend to the profile tag the bundle writer embeds (the --backends CLI
// value). Bundles whose tag does not match the running backend must be rejected: their code
// blobs are compiled for a different shader dialect or binary format, and the format version
// says nothing about that mismatch.
inline const char* ExpectedProfileTag(Backend backend) {
  switch (backend) {
    case Backend::OpenGL:
      return "opengl";
    case Backend::Metal:
      return "metal";
    case Backend::Vulkan:
      return "vulkan";
    case Backend::WebGPU:
      return "webgpu";
    default:
      return "";
  }
}

}  // namespace tgfx
