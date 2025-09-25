/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/GPUSampler.h"
#include "tgfx/gpu/opengl/GLDefines.h"

namespace tgfx {
class GLSampler : public GPUSampler {
 public:
  explicit GLSampler(int wrapS, int wrapT, int minFilter, int magFilter)
      : _wrapS(wrapS), _wrapT(wrapT), _minFilter(minFilter), _magFilter(magFilter) {
  }

  int wrapS() const {
    return _wrapS;
  }

  int wrapT() const {
    return _wrapT;
  }

  int minFilter() const {
    return _minFilter;
  }

  int magFilter() const {
    return _magFilter;
  }

 private:
  int _wrapS = 0;
  int _wrapT = 0;
  int _minFilter = 0;
  int _magFilter = 0;
};
}  // namespace tgfx
