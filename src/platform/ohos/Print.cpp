/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/platform/Print.h"
#include <hilog/log.h>
#include <cstdio>

namespace tgfx {
#define LOG_PRINT_TAG "tgfx"
#define LOG_PRINT_DOMAIN 0xFF00
#define MAX_LOG_LENGTH 4096

void PrintLog(const char format[], ...) {
  char buffer[MAX_LOG_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, MAX_LOG_LENGTH, format, args);
  va_end(args);
  OH_LOG_Print(LOG_APP, LOG_INFO, LOG_PRINT_DOMAIN, LOG_PRINT_TAG, "%{public}s", buffer);
}

void PrintError(const char format[], ...) {
  char buffer[MAX_LOG_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, MAX_LOG_LENGTH, format, args);
  va_end(args);
  OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_PRINT_DOMAIN, LOG_PRINT_TAG, "%{public}s", buffer);
}

void PrintWarn(const char format[], ...) {
  char buffer[MAX_LOG_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, MAX_LOG_LENGTH, format, args);
  va_end(args);
  OH_LOG_Print(LOG_APP, LOG_WARN, LOG_PRINT_DOMAIN, LOG_PRINT_TAG, "%{public}s", buffer);
}
}  // namespace tgfx