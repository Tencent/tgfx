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
namespace inspector {

inline constexpr int broadcastNum = 5;

inline constexpr uint8_t WelcomeMessageProgramNameSize = 64;
inline constexpr uint8_t ProtocolVersion = 1;
inline constexpr uint16_t BroadcastVersion = 1;

enum class MsgType : uint8_t { FrameCapture = 0, LayerTree = 1 };

struct BroadcastMessage {
  uint8_t type;
  uint16_t listenPort;
  uint32_t protocolVersion;
  uint64_t pid;
  int32_t activeTime;  // in seconds
  char programName[WelcomeMessageProgramNameSize];
};

}  // namespace inspector