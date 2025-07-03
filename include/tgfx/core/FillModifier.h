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

namespace tgfx {
class Fill;

/**
 * FillModifier is an interface for modifying the properties of a Fill used in drawing commands. It
 * allows you to adjust the color, alpha, or other attributes of a Fill before it is applied to a
 * picture record.
 */
class FillModifier {
 public:
  virtual ~FillModifier() = default;

  /**
   * Transforms the given Fill and returns a new Fill with the modifications applied.
   */
  virtual Fill transform(const Fill& fill) const = 0;
};
}  // namespace tgfx
