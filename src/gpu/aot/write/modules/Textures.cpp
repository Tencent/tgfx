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

#include "gpu/aot/write/modules/Textures.h"
#include <array>
#include "gpu/ProgramInfo.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/aot/write/modules/LeafWrite.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/modules/CoverageTexture.h"
#include "gpu/shaders/modules/DeviceMask.h"
#include "gpu/shaders/modules/DeviceMaskRuntime.h"
#include "gpu/shaders/modules/GlassLocalMask.h"
#include "gpu/shaders/modules/LocalMask.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/Texture.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/YUVTexture.h"

namespace tgfx {

namespace {

/// The device-space mask transform: the leaf's raw matrix scaled by the instantiated view's
/// normalized coordinate. Both mask modules that sample in device space upload this value.
Matrix DeviceMaskMatrix(const AOTTextureParameters& leaf) {
  Matrix matrix = leaf.uvMatrix;
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return matrix;
  }
  const auto scale = view->getTextureCoord(1, 1);
  matrix.postScale(scale.x, scale.y);
  return matrix;
}

}  // namespace

// Texture: the plain leaf source. Both matrices carry the source coordinate transform — the subset
// bounds go through the leaf's bare total transform (the old kernel's texSubsetMatrix), while the
// source coordinate takes the transform with the geometry's uv matrix pre-concatenated.
void WriteTextureModule(WriteContext& context) {
  const auto* leaf = context.sources.tiledLeaf;
  if (leaf == nullptr) {
    return;
  }
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(TextureModule, "CoordMatrix");
  constexpr uint8_t SubsetMatrix = UniformElementIndexOf(TextureModule, "SubsetMatrix");
  context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
  context.slot->set(SubsetMatrix, TotalLeafMatrix(*leaf));
  WriteTextureLeafParams<TextureModule>(*leaf, context.slot);
}

// TiledTexture: the single-tap or full nine-mode recipe, field for field the same values
// BindTiledSourceLeaf uploads for the legacy families.
void WriteTiledTextureModule(WriteContext& context) {
  const auto* leaf = context.sources.tiledLeaf;
  if (leaf == nullptr) {
    return;
  }
  constexpr uint8_t ShaderModeX = UniformElementIndexOf(TiledTexture, "ShaderModeX");
  constexpr uint8_t ShaderModeY = UniformElementIndexOf(TiledTexture, "ShaderModeY");
  constexpr uint8_t Subset = UniformElementIndexOf(TiledTexture, "Subset");
  constexpr uint8_t Clamp = UniformElementIndexOf(TiledTexture, "Clamp");
  constexpr uint8_t Dimension = UniformElementIndexOf(TiledTexture, "Dimension");
  constexpr uint8_t AlphaOnly = UniformElementIndexOf(TiledTexture, "AlphaOnly");
  constexpr uint8_t Strict = UniformElementIndexOf(TiledTexture, "Strict");
  // The runtime texture read swizzle (GRAY_8/RG_88) is only read by the full nine-mode path; its
  // field exists iff FULL != 0, absent from the single-tap variants.
  constexpr uint8_t ReadSwizzle = UniformElementIndexOf(TiledTexture, "ReadSwizzle");
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(TiledTexture, "CoordMatrix");
  context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
  if (!leaf->tiledRecipe.has_value()) {
    return;
  }
  const auto& sampling = *leaf->tiledRecipe;
  context.slot->set(ShaderModeX, static_cast<int32_t>(sampling.shaderModeX));
  context.slot->set(ShaderModeY, static_cast<int32_t>(sampling.shaderModeY));
  const float subset[4] = {sampling.shaderSubset.left, sampling.shaderSubset.top,
                           sampling.shaderSubset.right, sampling.shaderSubset.bottom};
  context.slot->set(Subset, subset);
  const float clampRect[4] = {sampling.shaderClamp.left, sampling.shaderClamp.top,
                              sampling.shaderClamp.right, sampling.shaderClamp.bottom};
  context.slot->set(Clamp, clampRect);
  if (sampling.usesShaderDimensions) {
    const float dimensions[2] = {sampling.shaderDimensions.x, sampling.shaderDimensions.y};
    context.slot->set(Dimension, dimensions);
  }
  context.slot->set(AlphaOnly, static_cast<int32_t>(sampling.alphaOnly ? 1 : 0));
  context.slot->set(Strict, static_cast<int32_t>(sampling.strict ? 1 : 0));
  if (context.slot->present(ReadSwizzle)) {
    auto view = leaf->textureProxy != nullptr ? leaf->textureProxy->getTextureView() : nullptr;
    if (view != nullptr && view->getTexture() != nullptr) {
      context.slot->set(ReadSwizzle,
                        static_cast<int32_t>(AOTReadSwizzleMode(view->getTexture()->format())));
    }
  }
}

// YUVTexture: the source transform, the leaf's subset fields and the conversion pair off the view.
void WriteYUVTextureModule(WriteContext& context) {
  const auto* leaf = context.sources.tiledLeaf;
  if (leaf == nullptr) {
    return;
  }
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(YUVTexture, "CoordMatrix");
  constexpr uint8_t LimitedRange = UniformElementIndexOf(YUVTexture, "LimitedRange");
  constexpr uint8_t ColorConversion = UniformElementIndexOf(YUVTexture, "ColorConversion");
  context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
  WriteTextureLeafParams<YUVTexture>(*leaf, context.slot);
  auto view = leaf->textureProxy != nullptr ? leaf->textureProxy->getTextureView() : nullptr;
  if (view == nullptr || !view->isYUV()) {
    return;
  }
  auto* yuvView = static_cast<const YUVTextureView*>(view.get());
  context.slot->set(LimitedRange,
                    static_cast<int32_t>(IsLimitedYUVColorRange(yuvView->yuvColorSpace()) ? 1 : 0));
  if (auto conversion = YUVColorConversionMatrix(yuvView->yuvColorSpace())) {
    context.slot->set(ColorConversion, *conversion);
  }
}

// CoverageTexture: the leaf's subset rect and read gates, the same values BindTextureSourceLeaf
// uploads. The sample coordinate rides the geometry's own transform.
void WriteCoverageTextureModule(WriteContext& context) {
  const auto* leaf = context.sources.maskLeaf;
  if (leaf != nullptr) {
    WriteTextureLeafParams<CoverageTexture>(*leaf, context.slot);
  }
}

// MaskTexture: the rasterized shape mask's coordinate transform. The texture itself is bound by
// the sampler path; the shader samples it and folds the red channel into the coverage.
void WriteMaskTextureModule(WriteContext& context) {
  const auto* leaf = context.sources.maskLeaf;
  if (leaf == nullptr) {
    return;
  }
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(MaskTexture, "CoordMatrix");
  context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
}

// GlassLocalMask: the runtime gate plus the mask leaf's own coordinate transform — the second
// transform the legacy walk reported.
void WriteGlassLocalMaskModule(WriteContext& context) {
  const auto* leaf = context.sources.maskLeaf;
  constexpr uint8_t HasMask = UniformElementIndexOf(GlassLocalMask, "HasMask");
  context.slot->set(HasMask, static_cast<int32_t>(leaf != nullptr ? 1 : 0));
  if (leaf != nullptr) {
    constexpr uint8_t CoordMatrix = UniformElementIndexOf(GlassLocalMask, "CoordMatrix");
    context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
  }
}

// DeviceMask: the mask's destination-to-sample transform (the effect's uv matrix scaled by the
// texture view's normalized coord).
void WriteDeviceMaskModule(WriteContext& context) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(DeviceMask, "CoordMatrix");
  const auto* maskLeaf = DeviceMaskCoverageLeaf(context.info.getCarriedIR());
  if (maskLeaf == nullptr) {
    return;
  }
  context.slot->set(CoordMatrix, DeviceMaskMatrix(*maskLeaf));
}

// DeviceMaskRuntime: the runtime gate plus the same device-space transform; a draw without a
// device mask keeps the branch off.
void WriteDeviceMaskRuntimeModule(WriteContext& context) {
  constexpr uint8_t HasDeviceMask = UniformElementIndexOf(DeviceMaskRuntime, "HasDeviceMask");
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(DeviceMaskRuntime, "CoordMatrix");
  const auto* maskLeaf = DeviceMaskCoverageLeaf(context.info.getCarriedIR());
  context.slot->set(HasDeviceMask, static_cast<int32_t>(maskLeaf != nullptr ? 1 : 0));
  if (maskLeaf == nullptr) {
    return;
  }
  context.slot->set(CoordMatrix, DeviceMaskMatrix(*maskLeaf));
}

// LocalMask: the coverage mask leaf's own coordinate transform, off the IR's Subtree term.
void WriteLocalMaskModule(WriteContext& context) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(LocalMask, "CoordMatrix");
  const auto* leaf = LocalMaskCoverageLeaf(context.info.getCarriedIR());
  if (leaf != nullptr) {
    context.slot->set(CoordMatrix, SourceCoordMatrix(*leaf, context.info.getGeometryLayout()));
  }
}

ModuleWriter TextureSourcesWriterOf(const Module& module) {
  if (&module == &TextureModule) {
    return WriteTextureModule;
  }
  if (&module == &TiledTexture) {
    return WriteTiledTextureModule;
  }
  if (&module == &YUVTexture) {
    return WriteYUVTextureModule;
  }
  if (&module == &CoverageTexture) {
    return WriteCoverageTextureModule;
  }
  if (&module == &MaskTexture) {
    return WriteMaskTextureModule;
  }
  if (&module == &GlassLocalMask) {
    return WriteGlassLocalMaskModule;
  }
  if (&module == &DeviceMask) {
    return WriteDeviceMaskModule;
  }
  if (&module == &DeviceMaskRuntime) {
    return WriteDeviceMaskRuntimeModule;
  }
  if (&module == &LocalMask) {
    return WriteLocalMaskModule;
  }
  return nullptr;
}

}  // namespace tgfx
