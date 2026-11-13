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

#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/TextureList.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/modules/AtlasTextGeometry.h"
#include "gpu/shaders/modules/CoverageTexture.h"
#include "gpu/shaders/modules/DeviceMask.h"
#include "gpu/shaders/modules/DeviceMaskRuntime.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/GlassLocalMask.h"
#include "gpu/shaders/modules/GlassRefraction.h"
#include "gpu/shaders/modules/GlassUDFTentBlur.h"
#include "gpu/shaders/modules/LocalMask.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/Texture.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/Xfer.h"
#include "gpu/shaders/modules/YUVTexture.h"

namespace tgfx {

namespace {

/// One module's textures, in its element declaration order. Returns false when the module
/// declares a slot this draw cannot fill and no placeholder is right; an absent optional source
/// occupies its slot with the dummy instead.
bool ProvideTextures(WriteContext& context, const Module& module) {
  auto* textures = context.textures;
  const auto& sources = context.sources;
  if (&module == &AtlasTextGeometry) {
    return textures->pushProxyFor(module, context.geometry.atlasProxy.get(),
                                  context.geometry.atlasSampler);
  }
  if (&module == &TextureModule || &module == &TiledTexture) {
    const auto* leaf = sources.tiledLeaf;
    return leaf != nullptr &&
           textures->pushProxyFor(module, leaf->textureProxy.get(), leaf->samplerState);
  }
  if (&module == &YUVTexture) {
    const auto* leaf = sources.tiledLeaf;
    auto view = leaf != nullptr && leaf->textureProxy != nullptr
                    ? leaf->textureProxy->getTextureView()
                    : nullptr;
    if (view == nullptr || !view->isYUV()) {
      return textures->pushPlaceholderFor(module);
    }
    return textures->pushPlanesFor(module, *static_cast<const YUVTextureView*>(view.get()),
                                   leaf->samplerState);
  }
  if (&module == &CoverageTexture || &module == &MaskTexture || &module == &GlassLocalMask) {
    const auto* leaf = sources.maskLeaf;
    if (leaf == nullptr) {
      return textures->pushPlaceholderFor(module);
    }
    return textures->pushProxyFor(module, leaf->textureProxy.get(), leaf->samplerState);
  }
  if (&module == &DeviceMask || &module == &DeviceMaskRuntime) {
    const auto* leaf = DeviceMaskCoverageLeaf(context.info.getCarriedIR());
    if (leaf == nullptr) {
      return textures->pushPlaceholderFor(module);
    }
    return textures->pushProxyFor(module, leaf->textureProxy.get(), leaf->samplerState);
  }
  if (&module == &LocalMask) {
    const auto* leaf = LocalMaskCoverageLeaf(context.info.getCarriedIR());
    if (leaf == nullptr) {
      return textures->pushPlaceholderFor(module);
    }
    return textures->pushProxyFor(module, leaf->textureProxy.get(), leaf->samplerState);
  }
  if (&module == &GaussianBlur || &module == &GlassUDFTentBlur) {
    const auto* child = sources.blurChild;
    if (child == nullptr) {
      return textures->pushPlaceholderFor(module);
    }
    return textures->pushProxyFor(module, child->textureProxy.get(), child->samplerState);
  }
  if (&module == &GlassRefraction) {
    const auto* kernel = sources.glassKernel;
    if (kernel == nullptr) {
      return false;
    }
    const auto& shape = kernel->glassShape;
    if (!textures->pushProxyFor(module, kernel->glassSourceProxy.get(),
                                SamplerState(TileMode::Clamp, TileMode::Clamp))) {
      return false;
    }
    // The mask slots exist only on the UDF shape: ask the layout before filling them.
    if (!textures->nextIsFor(module)) {
      return true;
    }
    if (!textures->pushProxyFor(module, shape.maskProxy.get(),
                                SamplerState(TileMode::Decal, TileMode::Decal))) {
      return false;
    }
    // The edge mask is declared only by the edge-lit variants: ask the layout before filling it.
    if (!textures->nextIsFor(module)) {
      return true;
    }
    return textures->pushProxyFor(module, shape.edgeMaskProxy.get(),
                                  SamplerState(TileMode::Decal, TileMode::Decal));
  }
  if (&module == &XferModule()) {
    const auto& xfer = context.info.getXferFacts();
    if (!xfer.shaderBlend() || !textures->nextIsFor(module)) {
      return true;
    }
    return textures->pushProxyFor(module, xfer.dstProxy().get(), {});
  }
  // The remaining modules declare no textures.
  return true;
}

}  // namespace

bool BindModuleTextures(WriteContext& context, const Module& module) {
  return ProvideTextures(context, module);
}

}  // namespace tgfx
