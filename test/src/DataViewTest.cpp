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
#include <tgfx/core/DataView.h>
#include <tgfx/core/Stream.h>
#include <tgfx/core/UTF.h>
#include "tgfx/core/Buffer.h"
#include "utils/TestUtils.h"

namespace tgfx{
TGFX_TEST(DataViewTest, PNGDataCheck) {
  auto stream =
      Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/test_timestretch.png"));
  ASSERT_TRUE(stream != nullptr && stream->size() >= 14);
  Buffer buffer(14);
  ASSERT_TRUE(stream->read(buffer.data(), 14) == 14);
  auto data = DataView(buffer.bytes(), buffer.size());
  auto secondByte = data.getUint8(1);
  auto thirdByte = data.getUint8(2);
  auto fourthByte = data.getUint8(3);
  ASSERT_TRUE(secondByte == 'P' && thirdByte == 'N' && fourthByte == 'G');
  buffer.clear();
}

TGFX_TEST(DataViewTest, ReadString) {
  Buffer buffer(100);
  std::string text = "Hello TGFX 123";
  auto dataView = DataView(buffer.bytes(), buffer.size());
  const char* textStart = text.data();
  const char* textStop = textStart + text.size();
  while (textStart < textStop) {
    auto offset = text.size() - static_cast<size_t>(textStop - textStart);
    auto unichar = UTF::NextUTF8(&textStart, textStop);
    dataView.setInt32(offset, unichar);
  }
  ASSERT_EQ(std::string((char*)buffer.bytes()), text);
}

TGFX_TEST(DataViewTest, ReadWriteData) {
  Buffer buffer(100);
  auto dataView = DataView(buffer.bytes(), buffer.size());
  dataView.setInt8(0, 'T');
  dataView.setUint8(1, 0xFF);
  dataView.setInt16(2, 'G');
  dataView.setUint16(4, 0xFFFF);
  dataView.setInt32(6, 'F');
  dataView.setUint32(10, 0xFFFFFFFF);
  dataView.setInt64(14, 'X');
  dataView.setUint64(22, 0xFFFFFFFFFFFFFFFF);
  dataView.setFloat(30, 1.123f);
  dataView.setDouble(34, 1.0E+39);
  ASSERT_TRUE(dataView.getInt8(0) == 'T');
  ASSERT_TRUE(dataView.getUint8(1) == 0xFF);
  ASSERT_TRUE(dataView.getInt16(2) == 'G');
  ASSERT_TRUE(dataView.getUint16(4) == 0xFFFF);
  ASSERT_TRUE(dataView.getInt32(6) == 'F');
  ASSERT_TRUE(dataView.getUint32(10) == 0xFFFFFFFF);
  ASSERT_TRUE(dataView.getInt64(14) == 'X');
  ASSERT_TRUE(dataView.getUint64(22) == 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(dataView.getFloat(30) == 1.123f);
  ASSERT_TRUE(dataView.getDouble(34) == 1.0E+39);
  buffer.clear();
  dataView.setByteOrder(ByteOrder::BigEndian);
  dataView.setUint16(0, 0x1234);
  ASSERT_TRUE(dataView.getUint16(0) == 0x1234);
  dataView.setByteOrder(ByteOrder::LittleEndian);
  ASSERT_TRUE(dataView.getUint16(0) == 0x3412);
}
}