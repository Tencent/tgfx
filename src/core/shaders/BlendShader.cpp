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

#include "BlendShader.h"
#include "core/utils/Types.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

static bool IsSimpleBlendChild(const FragmentProcessor* fp) {
  if (fp == nullptr) {
    return true;
  }
  auto fpName = fp->name();
  if (fpName == "TextureEffect") {
    auto* te = static_cast<const TextureEffect*>(fp);
    return !te->isYUV() && te->numTextureSamplers() > 0;
  }
  if (fpName == "ConstColorProcessor") {
    return true;
  }
  if (fpName == "TiledTextureEffect") {
    return true;
  }
  return false;
}

static PlacementPtr<FragmentProcessor> FlattenToTexture(const FPArgs& args,
                                                        PlacementPtr<FragmentProcessor> fp) {
  auto context = args.context;
  auto drawRect = args.drawRect;
  drawRect.roundOut();
  int width = static_cast<int>(drawRect.width());
  int height = static_cast<int>(drawRect.height());
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto renderTarget = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                              ImageOrigin::TopLeft, BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(fp), args.renderFlags)) {
    return nullptr;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::MakeTrans(-drawRect.x(), -drawRect.y());
  auto allocator = context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy), {}, &uvMatrix);
}
std::shared_ptr<Shader> Shader::MakeBlend(BlendMode mode, std::shared_ptr<Shader> dst,
                                          std::shared_ptr<Shader> src) {
  switch (mode) {
    case BlendMode::Clear:
      return MakeColorShader(Color::Transparent());
    case BlendMode::Dst:
      return dst;
    case BlendMode::Src:
      return src;
    default:
      break;
  }
  if (dst == nullptr || src == nullptr) {
    return nullptr;
  }
  auto shader = std::make_shared<BlendShader>(mode, std::move(dst), std::move(src));
  shader->weakThis = shader;
  return shader;
}

std::shared_ptr<Shader> BlendShader::makeWithMatrix(const Matrix& viewMatrix) const {
  auto dstShader = dst->makeWithMatrix(viewMatrix);
  auto srcShader = src->makeWithMatrix(viewMatrix);
  return Shader::MakeBlend(mode, dstShader, srcShader);
}

bool BlendShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::Blend) {
    return false;
  }
  auto other = static_cast<const BlendShader*>(shader);
  return mode == other->mode && dst->isEqual(other->dst.get()) && src->isEqual(other->src.get());
}

PlacementPtr<FragmentProcessor> BlendShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  auto fpA = FragmentProcessor::Make(dst, args, uvMatrix, dstColorSpace);
  if (fpA == nullptr) {
    return nullptr;
  }
  auto fpB = FragmentProcessor::Make(src, args, uvMatrix, dstColorSpace);
  if (fpB == nullptr) {
    return nullptr;
  }
  if (!IsSimpleBlendChild(fpA.get())) {
    fpA = FlattenToTexture(args, std::move(fpA));
    if (fpA == nullptr) {
      return nullptr;
    }
  }
  if (!IsSimpleBlendChild(fpB.get())) {
    fpB = FlattenToTexture(args, std::move(fpB));
    if (fpB == nullptr) {
      return nullptr;
    }
  }
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(args.context->drawingAllocator(),
                                                          std::move(fpB), std::move(fpA), mode);
}
}  // namespace tgfx
