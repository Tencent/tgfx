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

#include <memory>
#include <vector>
#include "tgfx/svg/node/SvgNode.h"

namespace tgfx {

/**
 * SvgContainer is the base class for SVG elements that can contain other nodes.
 */
class SvgContainer : public SvgNode {
 public:
  virtual ~SvgContainer() = default;

  /**
    * get children node of the container node 
    */
  const std::vector<std::shared_ptr<SvgNode>>& getChildren();
  void appendChild(std::shared_ptr<SvgNode>);

 protected:
  explicit SvgContainer(SvgTag);

  std::vector<std::shared_ptr<SvgNode>> _children;
};
}  // namespace tgfx