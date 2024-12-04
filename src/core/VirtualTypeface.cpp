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

#include "tgfx/core/VirtualTypeface.h"
#include "VirtualScalerContext.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/TypefaceProvider.h"

namespace tgfx {
std::shared_ptr<Typeface> Typeface::MakeVirtual(bool hasColor) {
  const auto& typeface = std::make_shared<VirtualTypeface>(hasColor);
  typeface->weakThis = typeface;
  return typeface;
}

VirtualTypeface::VirtualTypeface(bool hasColor) : _uniqueID(UniqueID::Next()), _hasColor(hasColor) {
}

bool VirtualTypeface::GetPath(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID,
                              bool fauxBold, bool fauxItalic, Path* path) {
  const auto& provider = TypefaceProviderManager::GetInstance()->getProvider();
  if (provider == nullptr) {
    return false;
  }

  return provider->getPath(typeface, glyphID, fauxBold, fauxItalic, path);
}

std::shared_ptr<ImageBuffer> VirtualTypeface::GetImage(const std::shared_ptr<Typeface>& typeface,
                                                       GlyphID glyphID, bool tryHardware) {
  const auto& provider = TypefaceProviderManager::GetInstance()->getProvider();
  if (provider == nullptr) {
    return nullptr;
  }

  return provider->getImage(typeface, glyphID, tryHardware);
}

Size VirtualTypeface::GetImageTransform(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID,
                                        Matrix* matrixOut) {
  const auto& provider = TypefaceProviderManager::GetInstance()->getProvider();
  if (provider == nullptr) {
    return Size::MakeEmpty();
  }

  return provider->getImageTransform(typeface, glyphID, matrixOut);
}

Rect VirtualTypeface::GetBounds(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold, bool fauxItalic) {
  const auto& provider = TypefaceProviderManager::GetInstance()->getProvider();
  if (provider == nullptr) {
    return Rect::MakeEmpty();
  }

  return provider->getBounds(typeface, glyphID, fauxBold, fauxItalic);
}

std::shared_ptr<ScalerContext> VirtualTypeface::createScalerContext(float size) const {
  return std::make_shared<VirtualScalerContext>(weakThis.lock(), size);
}
}  // namespace tgfx
