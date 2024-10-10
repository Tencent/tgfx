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
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/utils/StrokeKey.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {
class PathTriangles : public DataProvider {
 public:
  static std::shared_ptr<PathTriangles> Make(Path path, const Matrix& matrix, const Stroke* stroke,
                                             AAType aaType) {
    if (path.isEmpty()) {
      return nullptr;
    }
    return std::shared_ptr<PathTriangles>(
        new PathTriangles(std::move(path), matrix, stroke, aaType));
  }

  ~PathTriangles() override {
    delete stroke;
  }

  std::shared_ptr<Data> getData() const override {
    std::vector<float> vertices = {};
    auto finalPath = path;
    auto effect = PathEffect::MakeStroke(stroke);
    if (effect != nullptr) {
      effect->applyTo(&finalPath);
    }
    finalPath.transform(matrix);
    auto clipBounds = finalPath.getBounds();
    size_t count = 0;
    if (aaType == AAType::Coverage) {
      count = PathTriangulator::ToAATriangles(finalPath, clipBounds, &vertices);
    } else {
      count = PathTriangulator::ToTriangles(finalPath, clipBounds, &vertices);
    }
    if (count == 0) {
      // The path is not a filled path, or it is invisible.
      return nullptr;
    }
    return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
  }

 private:
  Path path = {};
  Matrix matrix = Matrix::I();
  Stroke* stroke = nullptr;
  AAType aaType = AAType::None;

  PathTriangles(Path path, const Matrix& matrix, const Stroke* s, AAType aaType)
      : path(std::move(path)), matrix(matrix), aaType(aaType) {
    if (s) {
      stroke = new Stroke(*s);
    }
  }
};

std::unique_ptr<TriangulatingPathOp> TriangulatingPathOp::Make(Color color, const Path& path,
                                                               const Matrix& viewMatrix,
                                                               const Stroke* stroke,
                                                               uint32_t renderFlags) {
  if (path.isEmpty()) {
    return nullptr;
  }
  return std::unique_ptr<TriangulatingPathOp>(
      new TriangulatingPathOp(color, path, viewMatrix, stroke, renderFlags));
}

TriangulatingPathOp::TriangulatingPathOp(Color color, Path p, const Matrix& viewMatrix,
                                         const Stroke* stroke, uint32_t renderFlags)
    : DrawOp(ClassID()), color(color), path(std::move(p)), viewMatrix(viewMatrix),
      stroke(stroke ? new Stroke(*stroke) : nullptr), renderFlags(renderFlags) {
  auto bounds = path.getBounds();
  viewMatrix.mapRect(&bounds);
  setBounds(bounds);
}

TriangulatingPathOp::~TriangulatingPathOp() {
  delete stroke;
}

bool TriangulatingPathOp::onCombineIfPossible(Op*) {
  return false;
}

void TriangulatingPathOp::prepare(Context* context) {
  static const auto TriangulatingPathType = UniqueID::Next();
  BytesKey bytesKey = {};
  auto scales = viewMatrix.getAxisScales();
  if (scales.x == scales.y) {
    rasterizeMatrix.setScale(scales.x, scales.y);
    bytesKey.reserve(2 + (stroke ? StrokeKeyCount : 0));
    bytesKey.write(TriangulatingPathType);
    bytesKey.write(scales.x);
  } else {
    rasterizeMatrix = viewMatrix;
    rasterizeMatrix.setTranslateX(0);
    rasterizeMatrix.setTranslateY(0);
    bytesKey.reserve(5 + (stroke ? StrokeKeyCount : 0));
    bytesKey.write(TriangulatingPathType);
    bytesKey.write(rasterizeMatrix.getScaleX());
    bytesKey.write(rasterizeMatrix.getSkewX());
    bytesKey.write(rasterizeMatrix.getSkewY());
    bytesKey.write(rasterizeMatrix.getScaleY());
  }
  if (stroke) {
    WriteStrokeKey(&bytesKey, stroke);
  }
  auto uniqueKey = UniqueKey::Combine(PathRef::GetUniqueKey(path), bytesKey);
  auto pathTriangles = PathTriangles::Make(path, rasterizeMatrix, stroke, aa);
  vertexBuffer = context->proxyProvider()->createGpuBufferProxy(uniqueKey, pathTriangles,
                                                                BufferType::Vertex, renderFlags);
}

void TriangulatingPathOp::execute(RenderPass* renderPass) {
  auto buffer = vertexBuffer->getBuffer();
  if (buffer == nullptr) {
    return;
  }
  Matrix uvMatrix = {};
  if (!rasterizeMatrix.invert(&uvMatrix)) {
    return;
  }
  auto realViewMatrix = viewMatrix;
  realViewMatrix.preConcat(uvMatrix);
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), aa,
                                                 realViewMatrix, uvMatrix));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(nullptr, buffer);
  auto vertexCount = aa == AAType::Coverage ? PathTriangulator::GetAATriangleCount(buffer->size())
                                            : PathTriangulator::GetTriangleCount(buffer->size());
  renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
}
}  // namespace tgfx
