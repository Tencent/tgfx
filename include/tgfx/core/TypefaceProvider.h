/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * Implement this interface to provide necessary information for rendering
 * virtual typefaces, such as paths, emoji images, and bounding boxes.
 */
class TypefaceProvider {
 public:
  TypefaceProvider() = default;
  virtual ~TypefaceProvider() = default;

  /**
   * Returns the path for the given glyph ID. If the path is not available, returns false.
   */
  virtual bool getPath(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                       bool fauxItalic, Path* path) const = 0;

  /**
   * Returns the image buffer for the given glyph ID. If the image is not available, returns nullptr.
   * tryHardware is true and there is hardware buffer support on the current platform, a hardware
   * backed PixelRef is allocated. Otherwise, a raster PixelRef is allocated.
   */
  virtual std::shared_ptr<ImageBuffer> getImage(const std::shared_ptr<Typeface>& typeface,
                                                GlyphID glyphID, bool tryHardware) const = 0;

  /**
   * Returns the image transform for the given glyph ID.
   * If the image is not available, returns an empty size.
   * matrixOut is the transform matrix to apply to the image.
   */
  virtual Size getImageTransform(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID,
                                 Matrix* matrixOut) const = 0;

  /**
   * Returns the bounds for the given glyph ID. If the bounds are not available, returns an empty
   * rect. The implementation of fauxBold and fauxItalic is up to the content provider.
   */
  virtual Rect getBounds(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                         bool fauxItalic) const = 0;
};

/**
 * Use this class to register custom virtual TypefaceProvider,
 * which provide the necessary rendering data for virtual Typefaces.
 */
class TypefaceProviderManager final {
 public:
  static TypefaceProviderManager* GetInstance();

  void registerProvider(const std::shared_ptr<TypefaceProvider>& provider);

  const std::shared_ptr<TypefaceProvider>& getProvider() const {
    return _provider;
  }

 private:
  TypefaceProviderManager() = default;
  ~TypefaceProviderManager() = default;

  TypefaceProviderManager(TypefaceProviderManager&&) = delete;
  TypefaceProviderManager(const TypefaceProviderManager&) = delete;
  TypefaceProviderManager& operator=(const TypefaceProviderManager&) = delete;
  TypefaceProviderManager& operator=(TypefaceProviderManager&&) = delete;

  std::shared_ptr<TypefaceProvider> _provider = nullptr;
  std::mutex locker = {};
};
}  // namespace tgfx
