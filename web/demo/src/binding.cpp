/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <emscripten/bind.h>
#include "TGFXThreadsView.h"
#include "TGFXView.h"

using namespace hello2d;
using namespace emscripten;

EMSCRIPTEN_BINDINGS(TGFXDemo) {

  class_<TGFXBaseView>("TGFXBaseView")
      .function("updateSize", &TGFXBaseView::updateSize)
      .function("flush", &TGFXBaseView::flush)
      .function("changeBlendMode", &TGFXBaseView::changeBlendMode)
      .function("click", &TGFXBaseView::click);

  class_<TGFXView, base<TGFXBaseView>>("TGFXView")
      .smart_ptr<std::shared_ptr<TGFXView>>("TGFXView")
      .class_function("MakeFrom",
                      optional_override([](const std::string& canvasID, const val& nativeImage) {
                        if (canvasID.empty()) {
                          return std::shared_ptr<TGFXView>(nullptr);
                        }
                        return std::make_shared<TGFXView>(canvasID, nativeImage);
                      }));

  class_<TGFXThreadsView, base<TGFXBaseView>>("TGFXThreadsView")
      .smart_ptr<std::shared_ptr<TGFXThreadsView>>("TGFXThreadsView")
      .class_function("MakeFrom",
                      optional_override([](const std::string& canvasID, const val& nativeImage) {
                        if (canvasID.empty()) {
                          return std::shared_ptr<TGFXThreadsView>(nullptr);
                        }
                        return std::make_shared<TGFXThreadsView>(canvasID, nativeImage);
                      }))
      .function("registerFonts", &TGFXThreadsView::registerFonts);
}
