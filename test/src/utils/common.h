/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <iostream>

namespace tgfx {

const std::array<float, 20> lumaColorMatrix = {0,
                                               0,
                                               0,
                                               0,
                                               0,  // red
                                               0,
                                               0,
                                               0,
                                               0,
                                               0,  // green
                                               0,
                                               0,
                                               0,
                                               0,
                                               0,  // blue
                                               0.21260000000000001f,
                                               0.71519999999999995f,
                                               0.0722f,
                                               0,
                                               0};

const std::array<float, 20> alphaColorMatrix = {0, 0, 0, 0,    0,  // red
                                                0, 0, 0, 0,    0,  // green
                                                0, 0, 0, 0,    0,  // blue
                                                0, 0, 0, 1.0f, 0};

}  // namespace tgfx
