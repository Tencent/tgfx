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

#pragma once

#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/Shape.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * GraphicsLoader is an interface for loading deferred graphics, such as images and shapes, in the
 * ProxyProvider. If a loaded graphic is nullptr, the corresponding drawing will be skipped.
 */
class GraphicsLoader {
 public:
  virtual ~GraphicsLoader() = default;

  /**
   * Loads an ImageBuffer from the given ImageGenerator.
   */
  virtual std::shared_ptr<ImageBuffer> loadImage(std::shared_ptr<ImageGenerator> generator,
                                                 bool tryHardware) = 0;

  /**
   * Loads a Path from the given Shape.
   */
  virtual Path loadShape(std::shared_ptr<Shape> shape) = 0;

 protected:
  /**
   * Called when the GraphicsLoader is attached to the Context. Subclasses can override this
   * method to perform initialization or setup operations.
   */
  virtual void onAttached() = 0;

  /**
   * Called when the GraphicsLoader is detached from the Context. Subclasses can override this
   * method to perform cleanup or reset operations.
   */
  virtual void onDetached() = 0;

  friend class AutoGraphicsLoaderRestore;
};

/**
 * AutoGraphicsLoaderRestore is a helper class that temporarily sets a new GraphicsLoader in the
 * Context, and restores the previous GraphicsLoader when it goes out of scope.
 */
class AutoGraphicsLoaderRestore {
 public:
  AutoGraphicsLoaderRestore(Context* context, GraphicsLoader* loader);

  ~AutoGraphicsLoaderRestore();

 private:
  Context* context = nullptr;
  GraphicsLoader* oldLoader = nullptr;
};
}  // namespace tgfx
