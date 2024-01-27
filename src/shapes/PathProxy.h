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

#pragma once

#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
class PathProxy {
 public:
  static std::shared_ptr<PathProxy> MakeFromFill(const Path& path);

  static std::shared_ptr<PathProxy> MakeFromFill(std::shared_ptr<TextBlob> textBlob);

  static std::shared_ptr<PathProxy> MakeFromStroke(std::shared_ptr<TextBlob> textBlob,
                                                   const Stroke& stroke);

  virtual ~PathProxy() = default;

  virtual Rect getBounds(float scale) const = 0;

  virtual Path getPath(float scale) const = 0;
};
}  // namespace tgfx
