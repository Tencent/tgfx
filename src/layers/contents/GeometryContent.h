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

#include <memory>
#include "layers/contents/LayerContent.h"

namespace tgfx {

class Shader;

/**
 * GeometryContent is the abstract base class for geometry-based layer contents.
 */
class GeometryContent : public LayerContent {
 public:
  /**
   * Returns the shader used by this content.
   */
  virtual const std::shared_ptr<Shader>& getShader() const = 0;

  /**
   * Returns true if this content has the same geometry as the other content.
   */
  virtual bool hasSameGeometry(const GeometryContent* other) const = 0;
};

}  // namespace tgfx
