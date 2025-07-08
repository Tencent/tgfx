/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/Image.h"
#include "tgfx/gpu/RuntimeProgram.h"
#include "tgfx/gpu/UniqueType.h"

namespace tgfx {
#define DEFINE_RUNTIME_EFFECT_TYPE               \
  static auto Type() {                           \
    static auto type = tgfx::UniqueType::Next(); \
    return type;                                 \
  }

/**
 * RuntimeEffect supports creating custom ImageFilter objects using the shading language of the
 * current GPU backend.
 */
class RuntimeEffect {
 public:
  /**
   * Constructs a RuntimeEffect with the given UniqueType. Use the DEFINE_RUNTIME_EFFECT_TYPE macro
   * to define the UniqueType. The UniqueType should be static for each effect class, ensuring all
   * instances of the same class share the same UniqueType. This allows the RuntimeProgram created
   * by the effect to be cached and reused.
   * @param type The UniqueType of the effect. Must use the DEFINE_RUNTIME_EFFECT_TYPE macro to
   * define it.
   * @param extraInputs A collection of additional input images used during rendering. When the
   * onDraw() method is called, these extraInputs will be converted to inputTextures.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  explicit RuntimeEffect(UniqueType type,
                         const std::vector<std::shared_ptr<Image>>& extraInputs = {});

  virtual ~RuntimeEffect();

  /**
   * Returns the UniqueType of the effect.
   */
  UniqueType type() const {
    return uniqueType;
  }

  /**
   * Returns the sample count requested by the effect. The default value is 1. Override this method
   * to return a value > 1 if the effect requires MSAA (multisampling antialiasing).
   */
  virtual int sampleCount() const {
    return 1;
  }

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds.
   */
  virtual Rect filterBounds(const Rect& srcRect) const {
    return srcRect;
  }

 protected:
  /**
   * Creates a new RuntimeProgram for the effect. The program will be cached in the GPU context and
   * reused for all instances of the effect.
   */
  virtual std::unique_ptr<RuntimeProgram> onCreateProgram(Context* context) const = 0;

  /**
   * Applies the effect to the input textures and draws the result to the specified render target.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  virtual bool onDraw(const RuntimeProgram* program,
                      const std::vector<BackendTexture>& inputTextures,
                      const BackendRenderTarget& target, const Point& offset) const = 0;

 private:
  // Each effect instance holds a valid reference to the UniqueType, so the corresponding
  // RuntimeProgram will not be released.
  UniqueType uniqueType = {};

  std::vector<std::shared_ptr<Image>> extraInputs = {};

  friend class RuntimeDrawTask;
  friend class RuntimeImageFilter;
};
}  // namespace tgfx
