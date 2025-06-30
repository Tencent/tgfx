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
#include <memory>

namespace inspector {

template <class T>
class Singleton {
 public:
  static T* GetInstance() {
    static T v;
    return &v;
  }
};

template <class T>
class SingletonPtr {
 public:
  static std::shared_ptr<T> GetInstance() {
    static std::shared_ptr<T> v(new T);
    return v;
  }
};
}  // namespace inspector
