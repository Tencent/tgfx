/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

struct LayerPerspectiveInfo {
  float xRotation = 0.0f;
  float yRotation = 0.0f;
  float zRotation = 0.0f;

  float depth = 0.0f;

  bool operator==(const LayerPerspectiveInfo& other) const {
    return xRotation == other.xRotation && yRotation == other.yRotation &&
           zRotation == other.zRotation && depth == other.depth;
  }
};

class PerspectiveFilter final : public LayerFilter {
 public:
  static std::shared_ptr<PerspectiveFilter> Make(const LayerPerspectiveInfo& info);

  const LayerPerspectiveInfo& info() const {
    return _info;
  }

  void setInfo(const LayerPerspectiveInfo& info);

  float xRotation() const {
    return _info.xRotation;
  }

  void setXRotation(float xRotation);

  float yRotation() const {
    return _info.yRotation;
  }

  void setYRotation(float yRotation);

  float zRotation() const {
    return _info.zRotation;
  }

  void setZRotation(float zRotation);

  float depth() const {
    return _info.depth;
  }

  void setDepth(float depth);

 private:
  explicit PerspectiveFilter(const LayerPerspectiveInfo& info);

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

  LayerPerspectiveInfo _info;
};

}  // namespace tgfx
