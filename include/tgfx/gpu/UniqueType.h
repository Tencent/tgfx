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

#include <cstdint>

namespace tgfx {
class UniqueDomain;

/**
 * UniqueType is a unique identifier for a type of object. It is currently used to identify the type
 * of RuntimeEffect instances.
 */
class UniqueType {
 public:
  /**
   * Creates a new UniqueType with a valid id.
   */
  static UniqueType Next();

  /**
   * Creates an empty UniqueType.
   */
  UniqueType() = default;

  UniqueType(const UniqueType& type);

  UniqueType(UniqueType&& type) noexcept;

  virtual ~UniqueType();

  /**
   * Returns the ID of the UniqueType. Returns 0 if the UniqueType is empty.
   */
  uint32_t uniqueID() const;

  UniqueType& operator=(const UniqueType& type);

  UniqueType& operator=(UniqueType&& type) noexcept;

 private:
  UniqueDomain* domain = nullptr;

  explicit UniqueType(UniqueDomain* domain);

  void addReference();

  void releaseReference();

  friend class RuntimeEffect;
  friend class UniqueKey;
};
}  // namespace tgfx
