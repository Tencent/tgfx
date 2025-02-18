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

#include "TransformContext.h"

namespace tgfx {
class MatrixDrawContext : public TransformContext {
 public:
  MatrixDrawContext(DrawContext* drawContext, const Matrix& matrix)
      : TransformContext(drawContext), initMatrix(matrix) {
  }

 protected:
  MCState transform(const MCState& state) override {
    auto newState = state;
    newState.matrix.postConcat(initMatrix);
    newState.clip.transform(initMatrix);
    return newState;
  }

 private:
  Matrix initMatrix = {};
};

class ClipDrawContext : public TransformContext {
 public:
  ClipDrawContext(DrawContext* drawContext, Path clip)
      : TransformContext(drawContext), initClip(std::move(clip)) {
  }

 protected:
  MCState transform(const MCState& state) override {
    MCState newState(state.matrix, lastIntersectedClip);
    if (state.clip != lastClip) {
      lastClip = state.clip;
      lastIntersectedClip = initClip;
      lastIntersectedClip.addPath(state.clip, PathOp::Intersect);
      newState.clip = lastIntersectedClip;
    }
    return newState;
  }

 private:
  Path initClip = {};
  Path lastClip = {};
  Path lastIntersectedClip = {};
};

class MCStateDrawContext : public ClipDrawContext {
 public:
  MCStateDrawContext(DrawContext* drawContext, const Matrix& matrix, Path clip)
      : ClipDrawContext(drawContext, std::move(clip)), initMatrix(matrix) {
  }

 protected:
  MCState transform(const MCState& state) override {
    auto newState = state;
    newState.matrix.postConcat(initMatrix);
    newState.clip.transform(initMatrix);
    return ClipDrawContext::transform(newState);
  }

 private:
  Matrix initMatrix = {};
};

std::unique_ptr<TransformContext> TransformContext::Make(DrawContext* drawContext,
                                                         const Matrix& matrix) {
  if (drawContext == nullptr || matrix.isIdentity()) {
    return nullptr;
  }
  return std::make_unique<MatrixDrawContext>(drawContext, matrix);
}

std::unique_ptr<TransformContext> TransformContext::Make(DrawContext* drawContext,
                                                         const Matrix& matrix, const Path& clip) {
  if (clip.isEmpty()) {
    if (clip.isInverseFillType()) {
      return Make(drawContext, matrix);
    }
    return nullptr;
  }
  if (drawContext == nullptr) {
    return nullptr;
  }
  if (matrix.isIdentity()) {
    return std::make_unique<ClipDrawContext>(drawContext, clip);
  }
  return std::make_unique<MCStateDrawContext>(drawContext, matrix, clip);
}
}  // namespace tgfx
