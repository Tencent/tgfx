/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TriangulatingPathOp.h"
#include "core/PathTriangulator.h"
#include "gpu/Gpu.h"
#include "gpu/processors/DefaultGeometryProcessor.h"

namespace tgfx {
class PathPaint {
 public:
  PathPaint(Path path, const Rect& clipBounds) : path(std::move(path)), clipBounds(clipBounds) {
  }

  Path path;
  Rect clipBounds;
};

class TriangulatingPathVertices : public DataProvider {
 public:
  explicit TriangulatingPathVertices(std::vector<std::shared_ptr<PathPaint>> pathPaints)
      : pathPaints(std::move(pathPaints)) {
  }

  std::shared_ptr<Data> getData() const override {
    std::vector<float> mergedVertices = {};
    for (auto& pathPaint : pathPaints) {
      std::vector<float> vertices = {};
      auto count =
          PathTriangulator::ToAATriangles(pathPaint->path, pathPaint->clipBounds, &vertices);
      if (count > 0) {
        mergedVertices.insert(mergedVertices.end(), std::make_move_iterator(vertices.begin()),
                              std::make_move_iterator(vertices.end()));
      }
    }
    return Data::MakeWithCopy(mergedVertices.data(), mergedVertices.size() * sizeof(float));
  }

 private:
  std::vector<std::shared_ptr<PathPaint>> pathPaints = {};
};

std::unique_ptr<TriangulatingPathOp> TriangulatingPathOp::Make(Color color, const Path& path,
                                                               const Rect& clipBounds,
                                                               const Matrix& localMatrix) {
  if (path.isEmpty() || path.countVerbs() > AA_TESSELLATOR_MAX_VERB_COUNT) {
    return nullptr;
  }
  return std::make_unique<TriangulatingPathOp>(color, path, clipBounds, Matrix::I(), localMatrix);
}

TriangulatingPathOp::TriangulatingPathOp(Color color, std::shared_ptr<GpuBufferProxy> bufferProxy,
                                         const Rect& bounds, const Matrix& viewMatrix,
                                         const Matrix& localMatrix)
    : DrawOp(ClassID()), color(color), bufferProxy(std::move(bufferProxy)), viewMatrix(viewMatrix),
      localMatrix(localMatrix) {
  setBounds(bounds);
}

TriangulatingPathOp::TriangulatingPathOp(Color color, Path path, const Rect& bounds,
                                         const Matrix& viewMatrix, const Matrix& localMatrix)
    : DrawOp(ClassID()), color(color), viewMatrix(viewMatrix), localMatrix(localMatrix) {
  auto pathPaint = std::make_shared<PathPaint>(std::move(path), bounds);
  pathPaints.push_back(std::move(pathPaint));
  setBounds(bounds);
}

bool TriangulatingPathOp::onCombineIfPossible(Op* op) {
  if (!DrawOp::onCombineIfPossible(op)) {
    return false;
  }
  auto* that = static_cast<TriangulatingPathOp*>(op);
  if (viewMatrix != that->viewMatrix || localMatrix != that->localMatrix || color != that->color ||
      bufferProxy != nullptr || that->bufferProxy != nullptr) {
    return false;
  }
  pathPaints.insert(pathPaints.end(), that->pathPaints.begin(), that->pathPaints.end());
  return true;
}

void TriangulatingPathOp::prepare(Context* context) {
  if (bufferProxy == nullptr) {
    auto dataProvider = std::make_shared<TriangulatingPathVertices>(pathPaints);
    bufferProxy = GpuBufferProxy::MakeFrom(context, std::move(dataProvider), BufferType::Vertex);
  }
}

void TriangulatingPathOp::execute(RenderPass* renderPass) {
  if (bufferProxy == nullptr) {
    return;
  }
  auto buffer = bufferProxy->getBuffer();
  if (buffer == nullptr) {
    return;
  }
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), viewMatrix,
                                                 localMatrix));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(nullptr, buffer);
  auto vertexCount = PathTriangulator::GetAATriangleCount(buffer->size());
  renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
}
}  // namespace tgfx
