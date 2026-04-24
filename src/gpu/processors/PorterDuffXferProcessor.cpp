/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "PorterDuffXferProcessor.h"
#include <functional>

namespace tgfx {

// BlendMode is a contiguous enum with Clear=0 and PlusDarker as its last value. Keep the total
// count in sync with include/tgfx/core/BlendMode.h — the EnumerateVariants test asserts it.
static constexpr int kBlendModeCount = static_cast<int>(BlendMode::PlusDarker) + 1;

static const char* BlendModeName(BlendMode mode) {
  switch (mode) {
    case BlendMode::Clear:
      return "Clear";
    case BlendMode::Src:
      return "Src";
    case BlendMode::Dst:
      return "Dst";
    case BlendMode::SrcOver:
      return "SrcOver";
    case BlendMode::DstOver:
      return "DstOver";
    case BlendMode::SrcIn:
      return "SrcIn";
    case BlendMode::DstIn:
      return "DstIn";
    case BlendMode::SrcOut:
      return "SrcOut";
    case BlendMode::DstOut:
      return "DstOut";
    case BlendMode::SrcATop:
      return "SrcATop";
    case BlendMode::DstATop:
      return "DstATop";
    case BlendMode::Xor:
      return "Xor";
    case BlendMode::PlusLighter:
      return "PlusLighter";
    case BlendMode::Modulate:
      return "Modulate";
    case BlendMode::Screen:
      return "Screen";
    case BlendMode::Overlay:
      return "Overlay";
    case BlendMode::Darken:
      return "Darken";
    case BlendMode::Lighten:
      return "Lighten";
    case BlendMode::ColorDodge:
      return "ColorDodge";
    case BlendMode::ColorBurn:
      return "ColorBurn";
    case BlendMode::HardLight:
      return "HardLight";
    case BlendMode::SoftLight:
      return "SoftLight";
    case BlendMode::Difference:
      return "Difference";
    case BlendMode::Exclusion:
      return "Exclusion";
    case BlendMode::Multiply:
      return "Multiply";
    case BlendMode::Hue:
      return "Hue";
    case BlendMode::Saturation:
      return "Saturation";
    case BlendMode::Color:
      return "Color";
    case BlendMode::Luminosity:
      return "Luminosity";
    case BlendMode::PlusDarker:
      return "PlusDarker";
  }
  return "Unknown";
}

const TextureView* PorterDuffXferProcessor::dstTextureView() const {
  auto textureProxy = dstTextureInfo.textureProxy;
  return textureProxy ? textureProxy->getTextureView().get() : nullptr;
}

void PorterDuffXferProcessor::computeProcessorKey(Context*, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  if (auto textureView = dstTextureView()) {
    TextureView::ComputeTextureKey(textureView->getTexture(), bytesKey);
  }
}

void PorterDuffXferProcessor::BuildMacros(BlendMode blendMode, bool hasDstTexture,
                                          ShaderMacroSet& macros) {
  if (hasDstTexture) {
    macros.define("TGFX_PDXP_DST_TEXTURE_READ");
  }
  if (!BlendModeAsCoeff(blendMode, true)) {
    macros.define("TGFX_PDXP_NON_COEFF");
  }
  macros.define("TGFX_BLEND_MODE", static_cast<int>(blendMode));
}

std::vector<ShaderVariant> PorterDuffXferProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(static_cast<size_t>(kBlendModeCount) * 2);
  std::hash<std::string> hasher;
  int index = 0;
  // Iteration order: blendMode outer, hasDstTexture inner. This is the stable ordering offline
  // tooling should rely on when mapping runtime configurations to shader variant indices.
  for (int modeInt = 0; modeInt < kBlendModeCount; ++modeInt) {
    auto mode = static_cast<BlendMode>(modeInt);
    for (int hasDstInt = 0; hasDstInt < 2; ++hasDstInt) {
      bool hasDstTexture = (hasDstInt != 0);
      ShaderMacroSet macros;
      BuildMacros(mode, hasDstTexture, macros);
      ShaderVariant variant;
      variant.index = index++;
      variant.name = std::string("PorterDuffXP[mode=") + BlendModeName(mode) +
                     ",dst=" + (hasDstTexture ? "1" : "0") + "]";
      variant.preamble = macros.toPreamble();
      variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
      variants.emplace_back(std::move(variant));
    }
  }
  return variants;
}

}  // namespace tgfx
