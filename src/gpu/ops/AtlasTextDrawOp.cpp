/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AtlasTextDrawOp.h"
#include "core/atlas/AtlasManager.h"
#include "core/utils/PlacementNode.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
PlacementPtr<Pipeline> AtlasTextDrawOp::createPipeline(RenderPass* renderPass,
                                                       std::shared_ptr<TextureProxy> textureProxy,
                                                       PlacementPtr<GeometryProcessor> gp) {
  auto numColorProcessors = colors.size();
  auto fragmentProcessors = std::move(colors);
  fragmentProcessors.reserve(numColorProcessors + coverages.size() + 1);

  auto atlasProcessor = TiledTextureEffect::Make(std::move(textureProxy), TileMode::Clamp,
                                                 TileMode::Clamp, {}, nullptr, true);

  fragmentProcessors.emplace_back(std::move(atlasProcessor));
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(std::move(coverage));
  }
  auto format = renderPass->renderTarget()->format();
  auto context = renderPass->getContext();
  const auto& swizzle = context->caps()->getWriteSwizzle(format);
  return context->drawingBuffer()->make<Pipeline>(std::move(gp), std::move(fragmentProcessors),
                                                  numColorProcessors, std::move(xferProcessor),
                                                  blendMode, &swizzle);
}

PlacementNode<AtlasTextDrawOp> AtlasTextDrawOp::Make(std::shared_ptr<AtlasProxy> atlasProxy,Color color,
                                                     const Matrix& uvMatrix, AAType aaType) {
  if (atlasProxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = atlasProxy->getContext()->drawingBuffer();
  return drawingBuffer->makeNode<AtlasTextDrawOp>(std::move(atlasProxy), color, uvMatrix, aaType);
}

AtlasTextDrawOp::AtlasTextDrawOp(std::shared_ptr<AtlasProxy> proxy, Color color,const Matrix& uvMatrix,
                                 AAType aaType)
    : DrawOp(aaType), atlasProxy(proxy), color(color), uvMatrix(uvMatrix) {
  // Initialize other members if needed
}

void AtlasTextDrawOp::execute(RenderPass* renderPass) {
  if (atlasProxy == nullptr || atlasProxy->getContext() == nullptr || renderPass == nullptr) {
    return;
  }
  auto atlasManger = atlasProxy->getContext()->atlasManager();
  if (atlasManger == nullptr) {
    return;
  }
  atlasManger->uploadToTexture();

  const auto& geometryProxies = atlasProxy->getGeometryProxies();
  for (const auto& [maskFormat, pageIndex, vertexproxy, indexProxy] : geometryProxies) {
    if (vertexproxy == nullptr || vertexproxy->getBuffer() == nullptr || indexProxy == nullptr ||
        indexProxy->getBuffer() == nullptr) {
      continue;
    }

    auto drawingBuffer = renderPass->getContext()->drawingBuffer();
    auto renderTarget = renderPass->renderTarget();
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(drawingBuffer, renderTarget->width(),
                                                   renderTarget->height(), AAType::None,
                                                   color, true);

    unsigned numActiveProxies = 0;
    auto textureProxy = atlasManger->getTextureProxy(maskFormat, &numActiveProxies);
    if (pageIndex >= numActiveProxies) {
      continue;
    }

    auto pipeLine = createPipeline(renderPass, textureProxy[pageIndex], std::move(gp));
    renderPass->bindProgramAndScissorClip(pipeLine.get(), scissorRect());
    renderPass->bindBuffers(indexProxy->getBuffer(), vertexproxy->getBuffer());
    renderPass->drawIndexed(PrimitiveType::Triangles, 0,
                            indexProxy->getBuffer()->size() / sizeof(uint16_t));
  }

  // Implement the execution logic for the AtlasTextDrawOp
  // This will involve using the atlasProxy and uvMatrix to draw the text
  // on the render pass.
}

}  // namespace tgfx