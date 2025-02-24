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
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "ViewData.h"

class UserData{
public:
  UserData();
  UserData(const char* program, uint64_t time);

  bool Valid() const { return !program.empty(); }

  void LoadState(ViewData& data);
  void SaveState(const ViewData& data);
  void StateShouldBePreserved();

private:
  FILE* OpenFile(const char* filename, bool write);
  void Remove(const char* filename);

  std::string program;
  uint64_t time;

  std::string description;

  bool preserveState;
};