
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

#include "tgfx/core/Data.h"

namespace tgfx {
class XMLParser {
 public:
  XMLParser();
  virtual ~XMLParser();

  /** 
    * Returns true for success
    */
  bool parse(const Data& data);

 protected:
  // override in subclasses; return true to stop parsing
  virtual bool onStartElement(const char elem[]);
  virtual bool onAddAttribute(const char name[], const char value[]);
  virtual bool onEndElement(const char elem[]);
  virtual bool onText(const char text[], int len);

 public:
  // public for ported implementation, not meant for clients to call
  bool startElement(const char elem[]);
  bool addAttribute(const char name[], const char value[]);
  bool endElement(const char elem[]);
  bool text(const char text[], int len);
  void* _parser = nullptr;
};
}  // namespace tgfx
