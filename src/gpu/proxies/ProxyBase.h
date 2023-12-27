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

namespace tgfx {
/**
 * This class delays the acquisition of instances until they are actually required.
 */
class ProxyBase {
 public:
  virtual ~ProxyBase() = default;

  /**
   * Returns true if the proxy is instantiated.
   */
  virtual bool isInstantiated() const = 0;

  /**
   * Instantiates the proxy if necessary. Returns true if the proxy is instantiated successfully.
   */
  virtual bool instantiate() = 0;
};
}  // namespace tgfx
