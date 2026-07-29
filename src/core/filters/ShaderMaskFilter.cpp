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

#include "ShaderMaskFilter.h"
#include "core/utils/Types.h"
#include "gpu/FPFlattenHelper.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

// A shader-mask filter whose shader is a shader-mode tiled image (e.g. a Decal sub-region image
// shader) produces CoverageFP=[Xfermode-dst(TiledTextureEffect)], which no precompiled kernel
// serves. Flattening the tiled mask to an exact full-bounds texture collapses it to
// Xfermode-dst(TextureEffect), served by QuadTextureFillShader's HAS_LOCAL_MASK path. Only
// shader-mode (non-hardware) tiling needs this; a plain TextureEffect or a hardware-sampled
// TiledTextureEffect is already matchable. Alpha-only masks are left untouched (the local-mask path
// samples .a, which requires an RGBA source).
static bool NeedsFlattenForLocalMask(const FragmentProcessor* fp) {
  if (fp->name() != "TiledTextureEffect") {
    return false;
  }
  auto* tiled = static_cast<const TiledTextureEffect*>(fp);
  if (tiled->isAlphaOnly()) {
    return false;
  }
  int modeX = 0;
  int modeY = 0;
  tiled->getShaderModes(&modeX, &modeY);
  return modeX != 0 || modeY != 0;
}
std::shared_ptr<MaskFilter> MaskFilter::MakeShader(std::shared_ptr<Shader> shader, bool inverted) {
  if (shader == nullptr) {
    return nullptr;
  }
  return std::make_shared<ShaderMaskFilter>(std::move(shader), inverted);
}

std::shared_ptr<MaskFilter> ShaderMaskFilter::makeWithMatrix(const Matrix& viewMatrix) const {
  auto newShader = shader->makeWithMatrix(viewMatrix);
  return MakeShader(std::move(newShader), inverted);
}

bool ShaderMaskFilter::isEqual(const MaskFilter* maskFilter) const {
  auto type = Types::Get(maskFilter);
  if (type != Types::MaskFilterType::Shader) {
    return false;
  }
  auto other = static_cast<const ShaderMaskFilter*>(maskFilter);
  return inverted == other->inverted && shader->isEqual(other->shader.get());
}

PlacementPtr<FragmentProcessor> ShaderMaskFilter::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto processor = FragmentProcessor::Make(shader, args, uvMatrix);
  auto allocator = args.context->drawingAllocator();
  if (processor == nullptr && inverted) {
    return ConstColorProcessor::Make(allocator, {}, InputMode::Ignore);
  }
  // AOT matchability: when the decomposition route is enabled, flatten a shader-mode tiled mask to
  // an exact full-bounds texture so the resulting Xfermode-dst(TextureEffect) coverage hits the
  // precompiled HAS_LOCAL_MASK path. Gate-guarded so the default path is byte-for-byte unchanged.
  if (processor != nullptr && NeedsFlattenForLocalMask(processor.get())) {
    auto cache = args.context->precompiledShaderCache();
    if (cache != nullptr && cache->decompositionEnabled()) {
      auto flattened = FlattenToTexture(args, std::move(processor));
      if (flattened == nullptr) {
        return nullptr;
      }
      processor = std::move(flattened);
    }
  }
  return FragmentProcessor::MulInputByChildAlpha(allocator, std::move(processor), inverted);
}
}  // namespace tgfx
