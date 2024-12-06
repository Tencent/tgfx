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

#include <iostream>
#include <string>
#include "base/TGFXTest.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLUtil.h"
#include "gtest/gtest.h"
#include "svg/xml/XMLParser.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/svg/SVGGenerator.h"
#include "tgfx/svg/xml/XMLDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(SVGExportTest, PureColor) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#00F\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();
  ASSERT_EQ(SVGString, compareString);
}

}  // namespace tgfx