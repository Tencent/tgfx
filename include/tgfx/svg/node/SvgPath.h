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

#include <memory>
#include "tgfx/core/path.h"
#include "tgfx/svg/node/SvgNode.h"

namespace tgfx {
class SvgPath : public SvgNode {
 public:
  static std::shared_ptr<SvgPath> Make() {
    return std::make_shared<SvgPath>();
  }

 private:
  SvgPath() : SvgNode(SvgTag::kPath) {
  }

 public:
  bool setAttribute(const std::string& attributeName, const std::string& attributeValue) override;

  const Path& getPath() const {
    return _path;
  }

 private:
  Path _path;
};
}  // namespace tgfx