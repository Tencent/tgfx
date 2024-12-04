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

#include "tgfx/core/Typeface.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
class TypefaceProvider {
 public:
  TypefaceProvider() = default;
  virtual ~TypefaceProvider() = default;

  virtual bool getPath(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                       bool fauxItalic, Path* path) const = 0;
  virtual std::shared_ptr<ImageBuffer> getImage(const std::shared_ptr<Typeface>& typeface,
                                                GlyphID glyphID, bool tryHardware) const = 0;
  virtual Size getImageTransform(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID,
                                 Matrix* matrixOut) const = 0;
  virtual Rect getBounds(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                         bool fauxItalic) const = 0;
};

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
};
}  // namespace tgfx
