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

#define DEFINE_RUNTIME_EFFECT_PROGRAM_ID                             \
  uint32_t programID() const override {                              \
    static uint32_t UniqueID = tgfx::RuntimeEffect::NextProgramID(); \
    return UniqueID;                                                 \
  }

namespace tgfx {

/**
 * RuntimeEffect supports creating custom ImageFilter objects using the shading language of the
 * current GPU backend.
 */
class RuntimeEffect {
 public:
  /**
   * Generates a globally unique program ID for the custom runtime effect. This ID is used by the
   * RuntimeEffect::programID() method.
   */
  static uint32_t NextProgramID();

  /**
   * Constructs a RuntimeEffect with the given extra input images.
   * @param extraInputs A collection of additional input images used during rendering. When the
   * onDraw() method is called, these extraInputs will be converted to inputTextures.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  explicit RuntimeEffect(const std::vector<std::shared_ptr<Image>>& extraInputs = {})
      : extraInputs(extraInputs) {
  }

  virtual ~RuntimeEffect() = default;

  /**
   * Returns the unique program ID for this effect. This ID identifies the RuntimeProgram created
   * by the effect and is used to cache it in the GPU context. Make sure the program ID stays the
   * same for all instances of the same effect class so the GPU context can reuse the program.
   *
   * Use the DEFINE_RUNTIME_EFFECT_PROGRAM_ID macro to implement the programID() method for
   * subclasses.
   */
  virtual uint32_t programID() const = 0;

  /**
   * Returns the sample count requested by the effect. The default value is 1. Override this method
   * to return a value > 1 if the effect requires MSAA (multisampling antialiasing).
   */
  virtual int sampleCount() const {
    return 1;
  }

  enum MapDirection {
    Forward,
    Reverse,
  };
  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
  * image of the given bounds. MapDirection::Forward is used to determine which pixels of the
   * destination canvas a source image rect would touch after filtering. MapDirection::Reverse
   * is used to determine which rect of the source image would be required to fill the given
   * rect (typically, clip bounds).
   */
  virtual Rect filterBounds(const Rect& srcRect, MapDirection) const {
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
  std::vector<std::shared_ptr<Image>> extraInputs = {};

  friend class RuntimeProgramCreator;
  friend class RuntimeDrawTask;
  friend class RuntimeImageFilter;
};
}  // namespace tgfx
