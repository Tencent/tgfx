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

#include <ostream>
#include <string>
#include <vector>
#include "GlslRectNormalizer.h"
#include "SpvToHlsl.h"

namespace tgfx {

/**
 * A flattened description of one program's UE-facing metadata: which .usf files belong to it,
 * how TGFX's processor tuple maps to UE bindings, and which registers the UE C++ side must bind
 * cbuffers / SRVs / Samplers to. ManifestWriter serialises this directly to JSON.
 */
struct ManifestEntry {
  std::string keyHex;
  std::string vsFile;  // relative path under the manifest directory, e.g. "hlsl/<key>.vs.usf"
  std::string psFile;
  // Pipeline tuple — copied verbatim from the Phase A/B JSONL so UE can key on the same
  // identity (GP name, FP chain, XP name).
  std::string geometryProcessor;
  std::vector<std::string> fragmentProcessors;
  size_t numColorProcessors = 0;
  std::string xferProcessor;
  // Register bindings for HLSL resources. Merged across VS and FS.
  std::vector<HlslResourceBinding> cbuffers;
  std::vector<HlslResourceBinding> srvs;
  std::vector<HlslResourceBinding> samplers;
  std::vector<HlslAttributeRemap> attributes;  // vertex input semantics in order
  // Rect samplers the normalizer rewrote to sampler2D. UE must supply an inverse-size vec2
  // uniform per entry, and for any DstTextureSampler_Pn listed here must also set the sibling
  // DstTextureCoordScale_Pn cbuffer member to (1/width, 1/height).
  std::vector<RectSamplerInfo> rectSamplers;
};

class ManifestWriter {
 public:
  explicit ManifestWriter(std::ostream& out);

  /** Begin the top-level JSON object. Writes schema version + generatedAt placeholder. */
  void beginManifest();

  /** Emit one manifest entry. Must be called between beginManifest() and endManifest(). */
  void appendEntry(const ManifestEntry& entry);

  /** Close the top-level JSON object. */
  void endManifest();

 private:
  std::ostream& out_;
  size_t entriesWritten_ = 0;
};

}  // namespace tgfx
