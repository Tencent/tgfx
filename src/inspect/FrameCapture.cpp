/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//
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
#include "FrameCapture.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include "ProcessUtils.h"
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#ifdef TGFX_USE_JPEG_ENCODE
#include "core/codecs/jpeg/JpegCodec.h"
#elif TGFX_USE_WEBP_ENCODE
#include "core/codecs/webp/WebpCodec.h"
#elif TGFX_USE_PNG_ENCODE
#include "core/codecs/png/PngCodec.h"
#endif
#include "core/PathTriangulator.h"
#include "core/ShapeRasterizer.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/glsl/GLSLProgramBuilder.h"
#include "gpu/ops/RectDrawOp.h"
#include "lz4.h"
#include "tgfx/core/Clock.h"
#include "tgfx/core/DataView.h"

namespace tgfx::inspect {
class RectIndicesProvider;

FrameCapture::FrameCapture()
    : epoch(Clock::Now()), initTime(Clock::Now()), dataBuffer(TargetFrameSize * 3),
      lz4Handler(LZ4CompressionHandler::Make()), broadcast(BroadcastCount) {
  spawnWorkerThreads();
}

FrameCapture::~FrameCapture() {
  shutdown.store(true, std::memory_order_relaxed);
  if (messageThread) {
    messageThread->join();
    messageThread.reset();
  }
  if (decodeThread) {
    decodeThread->join();
    decodeThread.reset();
  }
  broadcast.clear();
}

uint64_t FrameCapture::NextTextureID() {
  static std::atomic<uint32_t> nextID{1};
  uint32_t id;
  do {
    id = nextID.fetch_add(1, std::memory_order_relaxed);
  } while (id == 0);
  return id;
}

void FrameCapture::queueSerialFinish(const FrameCaptureMessageItem& item) {
  serialConcurrentQueue.enqueue(item);
}

void FrameCapture::sendAttributeData(const char* name, const Rect& rect) {
  float value[4] = {rect.left, rect.right, rect.top, rect.bottom};
  sendAttributeData(name, value, 4);
}

void FrameCapture::sendAttributeData(const char* name, const Matrix& matrix) {
  float value[6] = {1, 0, 0, 0, 1, 0};
  value[0] = matrix.getScaleX();
  value[1] = matrix.getSkewX();
  value[2] = matrix.getTranslateX();
  value[3] = matrix.getSkewY();
  value[4] = matrix.getScaleY();
  value[5] = matrix.getTranslateY();
  sendAttributeData(name, value, 6);
}

void FrameCapture::sendAttributeData(const char* name, const std::optional<Matrix>& matrix) {
  auto value = Matrix::MakeAll(1, 0, 0, 0, 1, 0);
  if (matrix.has_value()) {
    value = matrix.value();
  }
  sendAttributeData(name, value);
}

void FrameCapture::sendAttributeData(const char* name, const Color& color) {
  auto r = static_cast<uint8_t>(color.red * 255.f);
  auto g = static_cast<uint8_t>(color.green * 255.f);
  auto b = static_cast<uint8_t>(color.blue * 255.f);
  auto a = static_cast<uint8_t>(color.alpha * 255.f);
  auto value = static_cast<uint32_t>(r | g << 8 | b << 16 | a << 24);
  sendAttributeData(name, value, FrameCaptureMessageType::ValueDataColor);
}

void FrameCapture::sendAttributeData(const char* name, const std::optional<Color>& color) {
  auto value = Color::FromRGBA(255, 255, 255, 255);
  if (color.has_value()) {
    value = color.value();
  }
  sendAttributeData(name, value);
}

void FrameCapture::sendFrameMark(const char* name) {
  if (!isConnected()) {
    return;
  }
  if (!name) {
    frameCount.fetch_add(1, std::memory_order_relaxed);
  }
  _currentFrameShouldCaptrue.store(false, std::memory_order_relaxed);
  if (captureFrameCount > 0) {
    captureFrameCount--;
    _currentFrameShouldCaptrue.store(true, std::memory_order_relaxed);
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::FrameMarkMessage;
  item.frameMark.captured = currentFrameShouldCaptrue();
  item.frameMark.usTime = Clock::Now();
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, int val) {
  if (!isConnected()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataInt;
  item.attributeDataInt.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataInt.value = val;
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, float val) {
  if (!isConnected()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataFloat;
  item.attributeDataFloat.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataFloat.value = val;
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, bool val) {
  if (!isConnected()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataBool;
  item.attributeDataBool.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataBool.value = val;
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, uint8_t val, uint8_t type) {
  if (!isConnected()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataEnum;
  item.attributeDataEnum.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataEnum.value = static_cast<uint16_t>(type << 8 | val);
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, uint32_t val, FrameCaptureMessageType type) {
  if (!isConnected()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.attributeDataUint32.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataUint32.value = val;
  queueSerialFinish(item);
}

void FrameCapture::sendAttributeData(const char* name, float* val, int size) {
  if (!isConnected()) {
    return;
  }
  if (size == 4) {
    FrameCaptureMessageItem item = {};
    item.hdr.type = FrameCaptureMessageType::ValueDataFloat4;
    item.attributeDataFloat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
    queueSerialFinish(item);
  } else if (size == 6) {
    FrameCaptureMessageItem item = {};
    item.hdr.type = FrameCaptureMessageType::ValueDataMat3;
    item.attributeDataMat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
    queueSerialFinish(item);
  }
}

void FrameCapture::sendTextureID(uint64_t textureId, FrameCaptureMessageType type) {
  if (!isConnected()) {
    return;
  }
  if (!currentFrameShouldCaptrue() || textureId == 0) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.textureSampler.textureId = textureId;
  queueSerialFinish(item);
}

void FrameCapture::sendInputTextureID(uint64_t textureId) {
  sendTextureID(textureId, FrameCaptureMessageType::InputTexture);
}

void FrameCapture::sendOutputTextureID(uint64_t textureId) {
  sendTextureID(textureId, FrameCaptureMessageType::OutputTexture);
}

void FrameCapture::sendFrameCaptureTexture(
    std::shared_ptr<FrameCaptureTexture> frameCaptureTexture) {
  if (!isConnected()) {
    return;
  }
  if (frameCaptureTexture == nullptr) {
    return;
  }
  imageQueue.enqueue(std::move(frameCaptureTexture));
}

void FrameCapture::sendProgramKey(const BytesKey& programKey) {
  auto size = programKey.size() * sizeof(uint32_t);
  if (size == 0) {
    return;
  }
  auto item = copyDataToDirectlySendDataMessage(programKey.data(), size);
  item.hdr.type = FrameCaptureMessageType::ProgramKey;
  queueSerialFinish(item);
}

void FrameCapture::sendUniformValue(const std::string& name, const void* data, size_t size) {
  if (!currentFrameShouldCaptrue()) {
    return;
  }
  auto item = copyDataToDirectlySendDataMessage(name.c_str(), name.size());
  item.hdr.type = FrameCaptureMessageType::UniformValue;
  auto value = new (std::nothrow) uint8_t[size];
  memcpy(value, data, size);
  item.uniformValueMessage.valuePtr = reinterpret_cast<uint64_t>(value);
  item.uniformValueMessage.valueSize = size;
  queueSerialFinish(item);
}

void FrameCapture::sendOpPtr(DrawOp* drawOp) {
  if (!currentFrameShouldCaptrue()) {
    return;
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::OperatePtr;
  item.drawOpPtrMessage.drawOpPtr = reinterpret_cast<uint64_t>(drawOp);
  queueSerialFinish(item);
}

void FrameCapture::sendRectMeshData(DrawOp* drawOp, RectsVertexProvider* provider) {
  RectMeshInfo rectMeshData = {};
  auto meshType = static_cast<uint8_t>(VertexProviderType::RectsVertexProvider);
  rectMeshData.drawOpPtr = reinterpret_cast<uint64_t>(drawOp);
  rectMeshData.rectCount = provider->rectCount();
  rectMeshData.aaType = static_cast<uint8_t>(provider->aaType());
  rectMeshData.hasUVCoord = provider->hasUVCoord();
  rectMeshData.hasColor = provider->hasColor();
  rectMeshData.hasSubset = provider->hasSubset();
  auto extraDataSize = sizeof(RectMeshInfo) + sizeof(uint8_t);
  auto data = new (std::nothrow) uint8_t[extraDataSize];
  if (data == nullptr) {
    return;
  }
  memcpy(data, &meshType, sizeof(uint8_t));
  memcpy(data + sizeof(uint8_t), &rectMeshData, sizeof(RectMeshInfo));
  sendMeshData(provider, reinterpret_cast<uint64_t>(data), extraDataSize);
}

void FrameCapture::sendRRectMeshData(DrawOp* drawOp, RRectsVertexProvider* provider) {
  RRectMeshInfo rrectMeshData = {};
  auto meshType = static_cast<uint8_t>(VertexProviderType::RRectsVertexProvider);
  rrectMeshData.drawOpPtr = reinterpret_cast<uint64_t>(drawOp);
  rrectMeshData.rectCount = provider->rectCount();
  rrectMeshData.hasColor = provider->hasColor();
  rrectMeshData.useScale = provider->useScale();
  rrectMeshData.hasStroke = provider->hasStroke();
  auto extraDataSize = sizeof(RRectMeshInfo) + sizeof(uint8_t);
  auto data = new (std::nothrow) uint8_t[extraDataSize];
  if (data == nullptr) {
    return;
  }
  memcpy(data, &meshType, sizeof(meshType));
  memcpy(data + sizeof(uint8_t), &rrectMeshData, sizeof(RRectMeshInfo));
  sendMeshData(provider, reinterpret_cast<uint64_t>(data), extraDataSize);
}

void FrameCapture::sendShapeMeshData(DrawOp* drawOp, std::shared_ptr<StyledShape> styledShape,
                                     AAType aaType, const Rect& clipBounds) {
  if (!currentFrameShouldCaptrue() || styledShape == nullptr) {
    return;
  }
  auto path = styledShape->getPath();
  if (!PathTriangulator::ShouldTriangulatePath(path)) {
    return;
  }
  Matrix drawingMatrix = {};
  auto shape = styledShape->shape();
  auto isInverseFillType = shape->isInverseFillType();
  auto matrix = styledShape->matrix();
  if (!matrix.isIdentity() && !isInverseFillType) {
    auto scales = matrix.getAxisScales();
    if (scales.x == scales.y) {
      DEBUG_ASSERT(scales.x != 0);
      drawingMatrix = matrix;
      drawingMatrix.preScale(1.0f / scales.x, 1.0f / scales.x);
      styledShape->setMatrix(Matrix::MakeScale(scales.x));
    }
  }
  auto shapeBounds = styledShape->getBounds();
  if (aaType != AAType::None) {
    // Add a 1-pixel outset to preserve antialiasing results.
    shapeBounds.outset(1.0f, 1.0f);
  }
  auto bounds = isInverseFillType ? clipBounds : shapeBounds;
  drawingMatrix.preTranslate(bounds.x(), bounds.y());
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  // styledShape->applyMatrix(Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto rasterizer =
      std::make_unique<ShapeRasterizer>(width, height, std::move(styledShape), aaType);
  auto shapeBuffer = rasterizer->getData();
  RectMeshInfo rectMeshData = {};
  auto meshType = static_cast<uint8_t>(VertexProviderType::RectsVertexProvider);
  rectMeshData.drawOpPtr = reinterpret_cast<uint64_t>(drawOp);
  rectMeshData.rectCount = 0;
  rectMeshData.aaType = static_cast<uint8_t>(aaType);
  rectMeshData.hasUVCoord = false;
  rectMeshData.hasColor = false;
  rectMeshData.hasSubset = false;
  auto extraDataSize = sizeof(RectMeshInfo) + sizeof(uint8_t);
  auto extraData = new (std::nothrow) uint8_t[extraDataSize];
  if (extraData == nullptr) {
    return;
  }
  memcpy(extraData, &meshType, sizeof(uint8_t));
  memcpy(extraData + sizeof(uint8_t), &rectMeshData, sizeof(RectMeshInfo));

  auto bytesSize = shapeBuffer->triangles->size();
  auto vertices = new (std::nothrow) uint8_t[bytesSize];
  memcpy(const_cast<void*>(shapeBuffer->triangles->data()), vertices, bytesSize);
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::Mesh;
  item.meshMessage.dataPtr = reinterpret_cast<uint64_t>(vertices);
  item.meshMessage.size = bytesSize;
  item.meshMessage.extraDataPtr = reinterpret_cast<uint64_t>(extraData);
  item.meshMessage.extraDataSize = extraDataSize;
  queueSerialFinish(item);
}


void FrameCapture::sendMeshData(VertexProvider* provider, uint64_t extraDataPtr,
                                size_t extraDataSize) {
  if (!currentFrameShouldCaptrue()) {
    return;
  }
  auto bytesSize = provider->vertexCount() * sizeof(float);
  FrameCaptureMessageItem item = {};
  auto vertices = new (std::nothrow) uint8_t[bytesSize];
  provider->getVertices((float*)vertices);
  item.hdr.type = FrameCaptureMessageType::Mesh;
  item.meshMessage.dataPtr = reinterpret_cast<uint64_t>(vertices);
  item.meshMessage.size = bytesSize;
  item.meshMessage.extraDataPtr = extraDataPtr;
  item.meshMessage.extraDataSize = extraDataSize;
  queueSerialFinish(item);
}

void FrameCapture::captureProgramInfo(const BytesKey& programKey, Context* context,
                                      const ProgramInfo* programInfo) {
  if (!currentFrameShouldCaptrue()) {
    return;
  }
  sendProgramKey(programKey);
  if (programKeys.find(programKey) != programKeys.end()) {
    return;
  }
  programKeys.emplace(programKey);
  GLSLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return;
  }
  auto shaderCaps = context->caps()->shaderCaps();
  if (shaderCaps->usesCustomColorOutputName) {
    builder.fragmentShaderBuilder()->declareCustomOutputColor();
  }
  builder.finalizeShaders();
  GPUShaderModuleDescriptor vertexModule = {};
  vertexModule.code = builder.vertexShaderBuilder()->shaderString();
  vertexModule.stage = ShaderStage::Vertex;
  sendShaderText(vertexModule);

  GPUShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = builder.fragmentShaderBuilder()->shaderString();
  fragmentModule.stage = ShaderStage::Fragment;
  sendShaderText(fragmentModule);

  auto vertexUniformBuffer = builder.uniformHandler()->makeUniformBuffer(ShaderStage::Vertex);
  auto fragmentUniformBuffer = builder.uniformHandler()->makeUniformBuffer(ShaderStage::Fragment);
  sendUniformInfo(vertexUniformBuffer->uniforms());
  sendUniformInfo(fragmentUniformBuffer->uniforms());
}

void FrameCapture::sendShaderText(const GPUShaderModuleDescriptor& shaderDescriptor) {
  if (!isConnected()) {
    return;
  }
  auto size = shaderDescriptor.code.size();
  if (size == 0) {
    return;
  }
  auto item = copyDataToDirectlySendDataMessage(shaderDescriptor.code.c_str(), size);
  item.hdr.type = FrameCaptureMessageType::ShaderText;
  item.shaderTextMessage.type = static_cast<uint8_t>(shaderDescriptor.stage);
  queueSerialFinish(item);
}

void FrameCapture::sendUniformInfo(const std::vector<Uniform>& uniforms) {
  for (const auto& uniform : uniforms) {
    auto item = copyDataToDirectlySendDataMessage(uniform.name().c_str(), uniform.name().size());
    item.hdr.type = FrameCaptureMessageType::UniformInfo;
    item.uniformInfoMessage.format = static_cast<uint8_t>(uniform.format());
    queueSerialFinish(item);
  }
}

void FrameCapture::captureRenderTarget(const RenderTarget* renderTarget) {
  if (!currentFrameShouldCaptrue()) {
    return;
  }
  auto frameCaptureTexture = FrameCaptureTexture::MakeFrom(renderTarget);
  uint64_t textureId = 0;
  if (frameCaptureTexture != nullptr) {
    textureId = frameCaptureTexture->textureId();
    sendFrameCaptureTexture(std::move(frameCaptureTexture));
  }
  sendOutputTextureID(textureId);
}

void FrameCapture::sendFragmentProcessor(
    Context* context, const std::vector<PlacementPtr<FragmentProcessor>>& colors,
    const std::vector<PlacementPtr<FragmentProcessor>>& coverages) {
  if (!isConnected() || !currentFrameShouldCaptrue()) {
    return;
  }
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(colors.size() + coverages.size());
  for (auto& color : colors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  for (const auto& processor : fragmentProcessors) {
    FragmentProcessor::Iter fpIter(processor);
    while (const auto* subFP = fpIter.next()) {
      for (size_t j = 0; j < subFP->numTextureSamplers(); ++j) {
        auto texture = subFP->textureAt(j);
        auto frameCaptureTexture = FrameCaptureTexture::MakeFrom(texture, context);
        uint64_t textureId = 0;
        if (frameCaptureTexture) {
          textureId = frameCaptureTexture->textureId();
          sendFrameCaptureTexture(std::move(frameCaptureTexture));
        } else {
          textureId = FrameCaptureTexture::GetReadedTextureId(texture);
        }
        sendInputTextureID(textureId);
      }
    }
  }
}

void FrameCapture::LaunchWorker(FrameCapture* inspector) {
  inspector->worker();
}

void FrameCapture::clear() {
  dataBufferOffset = 0;
  dataBufferStart = 0;
  captureFrameCount = 0;
  programKeys.clear();
  _currentFrameShouldCaptrue.store(false, std::memory_order_relaxed);
}

bool FrameCapture::shouldExit() {
  return shutdown.load(std::memory_order_relaxed);
}

bool FrameCapture::currentFrameShouldCaptrue() {
  return _currentFrameShouldCaptrue.load(std::memory_order_relaxed);
}

bool FrameCapture::isConnected() {
  return isConnect.load(std::memory_order_acquire);
}

void FrameCapture::spawnWorkerThreads() {
  messageThread = std::make_unique<std::thread>(LaunchWorker, this);
  decodeThread = std::make_unique<std::thread>(LaunchEncodeWorker, this);
  timeBegin.store(Clock::Now(), std::memory_order_relaxed);
}

bool FrameCapture::handleServerQuery() {
  ServerQueryPacket payload = {};
  if (!sock->readData(&payload, sizeof(payload), 10)) {
    return false;
  }
  auto type = payload.type;
  auto ptr = payload.ptr;
  switch (type) {
    case ServerQuery::String: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), FrameCaptureMessageType::StringData);
    }
    case ServerQuery::ValueName: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), FrameCaptureMessageType::ValueName);
    }
    case ServerQuery::CaptureFrame: {
      captureFrameCount += payload.extra;
    }
    default:
      break;
  }
  return true;
}

void FrameCapture::sendString(uint64_t stringPtr, const char* str, size_t len,
                              FrameCaptureMessageType type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = stringPtr;

  auto dataLen = static_cast<uint16_t>(len);
  needDataSize(FrameCaptureMessageDataSize[static_cast<int>(type)] + sizeof(uint16_t) + dataLen);
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint16_t));
  appendDataUnsafe(str, dataLen);
}

void FrameCapture::sendString(uint64_t stringPtr, const char* str, FrameCaptureMessageType type) {
  sendString(stringPtr, str, strlen(str), type);
}

void FrameCapture::sendStringWithExtraData(uint64_t str, const char* ptr, size_t len,
                                           std::shared_ptr<Data> extraData,
                                           FrameCaptureMessageType type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  auto dataLen = static_cast<uint16_t>(len);
  auto extraDataLen = static_cast<uint16_t>(extraData->size());
  needDataSize(FrameCaptureMessageDataSize[static_cast<int>(type)] + sizeof(uint16_t) + dataLen +
               sizeof(extraDataLen) + extraData->size());
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint16_t));
  appendDataUnsafe(ptr, dataLen);
  appendDataUnsafe(&extraDataLen, sizeof(uint16_t));
  appendDataUnsafe(extraData->bytes(), extraDataLen);
}

void FrameCapture::sendLongStringWithExtraData(uint64_t str, const char* ptr, size_t len,
                                               std::shared_ptr<Data> extraData,
                                               FrameCaptureMessageType type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  auto dataLen = static_cast<uint32_t>(len);
  auto extraDataLen = static_cast<uint32_t>(extraData->size());
  needDataSize(FrameCaptureMessageDataSize[static_cast<int>(type)] + sizeof(uint32_t) + dataLen +
               sizeof(extraDataLen) + extraData->size());
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint32_t));
  appendDataUnsafe(ptr, dataLen);
  appendDataUnsafe(&extraDataLen, sizeof(uint32_t));
  appendDataUnsafe(extraData->bytes(), extraDataLen);
}

void FrameCapture::sendPixelsData(uint64_t pixelsPtr, const char* pixels, size_t len,
                                  FrameCaptureMessageType type) {
  ASSERT(type == FrameCaptureMessageType::PixelsData);
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = pixelsPtr;
  auto dataLen = static_cast<uint32_t>(len);
  commitData();
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint32_t));
  appendDataUnsafe(pixels, dataLen);
}

void FrameCapture::worker() {
  std::string addr = "255.255.255.255";
  auto dataPort = TCPPortProvider::Get().getValidPort();
  if (dataPort == 0) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);

  while (timeBegin.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto welcome = WelcomeMessage();
  welcome.initBegin = initTime;
  welcome.initEnd = timeBegin.load(std::memory_order_relaxed);

  auto listen = ListenSocket{};
  bool isListening = false;
  for (uint16_t i = 0; i < 20; ++i) {
    if (listen.listenSock(dataPort + i, 4)) {
      dataPort += i;
      isListening = true;
      break;
    }
  }
  if (!isListening) {
    while (true) {
      if (shouldExit()) {
        shutdown.store(true, std::memory_order_relaxed);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  for (uint16_t i = 0; i < BroadcastCount; i++) {
    broadcast[i] = std::make_shared<UDPBroadcast>();
    if (!broadcast[i]->openConnect(addr.c_str(), BroadcastPort + i)) {
      broadcast[i].reset();
    }
  }

  size_t broadcastLen = 0;
  auto broadcastMessage =
      GetBroadcastMessage(procname, pnsz, broadcastLen, dataPort, ToolType::FrameCapture);
  long long lastBroadcast = 0;
  while (true) {
    welcome.refTime = refTimeThread;
    clear();
    while (true) {
      if (shouldExit()) {
        for (uint16_t i = 0; i < BroadcastCount; i++) {
          if (broadcast[i]) {
            broadcastMessage.activeTime = -1;
            broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
          }
        }
        return;
      }

      sock = listen.acceptSock();
      if (sock) {
        break;
      }

      const auto currentTime = Clock::Now();
      if (currentTime - lastBroadcast > BroadcastHeartbeatUSTime) {
        lastBroadcast = currentTime;
        for (uint16_t i = 0; i < BroadcastCount; i++) {
          if (broadcast[i]) {
            programNameLock.lock();
            if (programName) {
              broadcastMessage = GetBroadcastMessage(programName, strlen(programName), broadcastLen,
                                                     dataPort, ToolType::FrameCapture);
              programName = nullptr;
            }
            programNameLock.unlock();

            const auto activeTime = Clock::Now();
            broadcastMessage.activeTime = static_cast<int32_t>(activeTime - epoch);
            broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
          }
        }
      }
    }

    for (uint16_t i = 0; i < BroadcastCount; i++) {
      if (broadcast[i]) {
        lastBroadcast = 0;
        broadcastMessage.activeTime = -1;
        broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
      }
    }

    if (!confirmProtocol()) {
      continue;
    }

    handleConnect(welcome);
    if (shouldExit()) {
      break;
    }
    isConnect.store(false, std::memory_order_release);
    sock.reset();
  }
}

void FrameCapture::encodeWorker() {
  while (true) {
    if (shouldExit()) {
      return;
    }
    std::shared_ptr<FrameCaptureTexture> frameCaputreTexture = nullptr;
    while (imageQueue.try_dequeue(frameCaputreTexture)) {
      const auto width = frameCaputreTexture->width();
      const auto height = frameCaputreTexture->height();
      auto colorType = PixelFormatToColorType(frameCaputreTexture->format());
      auto imageInfo = ImageInfo::Make(width, height, colorType, AlphaType::Premultiplied,
                                       frameCaputreTexture->rowBytes());
#ifdef TGFX_USE_JPEG_ENCODE
      auto encodeFormat = EncodedFormat::JPEG;
#elif TGFX_USE_WEBP_ENCODE
      auto encodeFormat = EncodedFormat::WEBP;
#elif TGFX_USE_PNG_ENCODE
      auto encodeFormat = EncodedFormat::PNG;
#endif
      auto jpgBuffer = ImageCodec::Encode(
          Pixmap(imageInfo, frameCaputreTexture->imageBuffer()->bytes()), encodeFormat, 100);
      auto size = jpgBuffer->size();
      auto pxielsBuffer = static_cast<uint8_t*>(malloc(size));
      memcpy(pxielsBuffer, jpgBuffer->bytes(), size);

      FrameCaptureMessageItem item = {};
      item.hdr.type = FrameCaptureMessageType::TextureData;
      item.textureData.isInput = frameCaputreTexture->isInput();
      item.textureData.textureId = frameCaputreTexture->textureId();
      item.textureData.width = frameCaputreTexture->width();
      item.textureData.height = frameCaputreTexture->height();
      item.textureData.rowBytes = frameCaputreTexture->rowBytes();
      item.textureData.format = frameCaputreTexture->format();
      item.textureData.pixels = reinterpret_cast<uint64_t>(pxielsBuffer);
      item.textureData.pixelsSize = size;
      queueSerialFinish(item);
    }
  }
}

bool FrameCapture::appendData(const void* data, size_t len) {
  const auto result = needDataSize(len);
  appendDataUnsafe(data, len);
  return result;
}

bool FrameCapture::needDataSize(size_t len) {
  bool result = true;
  if (static_cast<size_t>(dataBufferOffset - dataBufferStart) + len > TargetFrameSize) {
    result = commitData();
  }
  return result;
}

void FrameCapture::appendDataUnsafe(const void* data, size_t len) {
  if (dataBuffer.size() < len + static_cast<size_t>(dataBufferOffset)) {
    auto tempData = Data::MakeWithCopy(dataBuffer.data(), dataBuffer.size());
    dataBuffer.clear();
    dataBuffer.alloc(len + static_cast<size_t>(dataBufferOffset - dataBufferStart));
    memcpy(dataBuffer.bytes(), tempData->bytes(), tempData->size());
  }
  memcpy(dataBuffer.bytes() + dataBufferOffset, data, len);
  dataBufferOffset += static_cast<int>(len);
}

bool FrameCapture::commitData() {
  bool result = sendData(static_cast<const uint8_t*>(dataBuffer.data()) + dataBufferStart,
                         static_cast<size_t>(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return result;
}

static bool IsEncodeImage(const uint8_t* data, size_t size) {
  auto offset =
      sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage) + sizeof(uint32_t);
  const auto pixelsData = Data::MakeWithoutCopy(data + offset, size);
#ifdef TGFX_USE_JPEG_ENCODE
  return JpegCodec::IsJpeg(pixelsData);
#elif TGFX_USE_WEBP_ENCODE
  return WebpCodec::IsWebp(pixelsData);
#elif TGFX_USE_PNG_ENCODE
  return PngCodec::IsPng(pixelsData);
#endif
}

bool FrameCapture::sendData(const uint8_t* data, size_t len) {
  if (len == 0 || data == nullptr) {
    return true;
  }
  auto headSize = sizeof(bool) + sizeof(size_t);
  auto maxOutputSize = LZ4CompressionHandler::GetMaxOutputSize(len);
  if (lz4Buf.size() < maxOutputSize) {
    lz4Buf.clear();
    lz4Buf.alloc(maxOutputSize + headSize);
    if (lz4Buf.isEmpty()) {
      LOGE("Inspector failed to send data!");
      return false;
    }
  }
  auto lz4Size = len;
  bool isLz4Encode = false;
  if (IsEncodeImage(data, len) || len < MinLZ4EncodeSize) {
    memcpy(lz4Buf.bytes() + headSize, data, len);
  } else {
    isLz4Encode = true;
    lz4Size = lz4Handler->encode(lz4Buf.bytes() + headSize, maxOutputSize, data, len);
  }
  memcpy(lz4Buf.bytes(), &isLz4Encode, sizeof(bool));
  memcpy(lz4Buf.bytes() + sizeof(bool), &lz4Size, sizeof(size_t));
  return sock->sendData(lz4Buf.bytes(), lz4Size + sizeof(bool) + sizeof(size_t)) != -1;
}

bool FrameCapture::confirmProtocol() {
  char shibboleth[HandshakeShibbolethSize] = "";
  auto res = sock->readRaw(shibboleth, HandshakeShibbolethSize, 2000);
  if (!res || memcmp(shibboleth, HandshakeShibboleth, HandshakeShibbolethSize) != 0) {
    sock.reset();
    return false;
  }
  uint32_t protocolVersion = 0;
  res = sock->readRaw(&protocolVersion, sizeof(protocolVersion), 2000);
  if (!res) {
    sock.reset();
    return false;
  }
  if (protocolVersion != ProtocolVersion) {
    auto status = HandshakeStatus::HandshakeProtocolMismatch;
    sock->sendData(&status, sizeof(status));
    sock.reset();
    return false;
  }
  return true;
}

FrameCaptureMessageItem FrameCapture::copyDataToDirectlySendDataMessage(const void* src,
                                                                        size_t size) {
  FrameCaptureMessageItem item = {};
  auto data = new (std::nothrow) uint8_t[size];
  memcpy(data, src, size);
  item.directlySendDataMessage.size = size;
  item.directlySendDataMessage.dataPtr = reinterpret_cast<uint64_t>(data);
  return item;
}

void FrameCapture::handleConnect(const WelcomeMessage& welcome) {
  isConnect.store(true, std::memory_order_release);
  auto handshake = HandshakeStatus::HandshakeWelcome;
  sock->sendData(&handshake, sizeof(handshake));

  lz4Handler->reset();
  sock->sendData(&welcome, sizeof(welcome));

  auto keepAlive = 0;
  while (true) {
    const auto serialStatus = dequeueSerial();
    if (serialStatus == DequeueStatus::ConnectionLost) {
      break;
    } else if (serialStatus == DequeueStatus::QueueEmpty) {
      if (shouldExit()) {
        break;
      }
      if (dataBufferOffset != dataBufferStart) {
        if (!commitData()) {
          break;
        }
        if (keepAlive == 500) {
          FrameCaptureMessageItem ka = {};
          ka.hdr.type = FrameCaptureMessageType::KeepAlive;
          appendData(&ka, FrameCaptureMessageDataSize[ka.hdr.idx]);
          if (!commitData()) {
            break;
          }
          keepAlive = 0;
        } else if (!sock->hasData()) {
          ++keepAlive;
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      } else {
        keepAlive = 0;
      }

      bool connActive = true;
      while (sock->hasData()) {
        connActive = handleServerQuery();
        if (!connActive) {
          break;
        }
      }
      if (!connActive) {
        break;
      }
    }
  }
}

FrameCapture::DequeueStatus FrameCapture::dequeueSerial() {
  const auto queueSize = serialConcurrentQueue.size_approx();
  if (queueSize > 0) {
    auto refThread = refTimeThread;
    FrameCaptureMessageItem item = {};
    for (; serialConcurrentQueue.try_dequeue(item);) {
      auto idx = item.hdr.idx;
      switch (static_cast<FrameCaptureMessageType>(idx)) {
        case FrameCaptureMessageType::TextureData: {
          auto ptr = item.textureData.pixels;
          auto size = item.textureData.pixelsSize;
          sendPixelsData(ptr, reinterpret_cast<const char*>(ptr), size,
                         FrameCaptureMessageType::PixelsData);
          free((void*)ptr);
          break;
        }
        case FrameCaptureMessageType::ProgramKey: {
          auto data = item.directlySendDataMessage.dataPtr;
          auto size = item.directlySendDataMessage.size;
          sendString(data, reinterpret_cast<const char*>(data), size,
                     FrameCaptureMessageType::ProgramKeyData);
          delete[] reinterpret_cast<const uint8_t*>(data);
          continue;
        }
        case FrameCaptureMessageType::ShaderText: {
          auto data = item.directlySendDataMessage.dataPtr;
          auto size = item.directlySendDataMessage.size;
          auto type = FrameCaptureMessageType::VertexShaderTextData;
          if (item.shaderTextMessage.type == static_cast<uint8_t>(ShaderStage::Fragment)) {
            type = FrameCaptureMessageType::FragmentShaderTextData;
          }
          sendString(data, reinterpret_cast<const char*>(data), size, type);
          delete[] reinterpret_cast<const uint8_t*>(data);
          continue;
        }
        case FrameCaptureMessageType::UniformInfo: {
          auto data = item.directlySendDataMessage.dataPtr;
          auto size = item.directlySendDataMessage.size;
          auto formatData = Data::MakeWithCopy(&item.uniformInfoMessage.format, sizeof(uint8_t));
          sendStringWithExtraData(data, reinterpret_cast<const char*>(data), size,
                                  std::move(formatData), FrameCaptureMessageType::UniformInfoData);
          delete[] reinterpret_cast<const uint8_t*>(data);
          continue;
        }
        case FrameCaptureMessageType::UniformValue: {
          auto data = item.directlySendDataMessage.dataPtr;
          auto size = item.directlySendDataMessage.size;
          auto valueData =
              Data::MakeWithCopy(reinterpret_cast<uint8_t*>(item.uniformValueMessage.valuePtr),
                                 item.uniformValueMessage.valueSize);
          sendStringWithExtraData(data, reinterpret_cast<const char*>(data), size,
                                  std::move(valueData), FrameCaptureMessageType::UniformValueData);
          delete[] reinterpret_cast<const uint8_t*>(data);
          delete[] reinterpret_cast<const uint8_t*>(item.uniformValueMessage.valuePtr);
          continue;
        }
        case FrameCaptureMessageType::Mesh: {
          auto data = item.directlySendDataMessage.dataPtr;
          auto size = item.directlySendDataMessage.size;
          auto extraData =
              Data::MakeWithCopy(reinterpret_cast<uint8_t*>(item.meshMessage.extraDataPtr),
                                 item.meshMessage.extraDataSize);
          sendLongStringWithExtraData(data, reinterpret_cast<const char*>(data), size,
                                      std::move(extraData), FrameCaptureMessageType::MeshData);
          LOGI("send Shape mesh size = %ld", size);
          delete[] reinterpret_cast<const uint8_t*>(data);
          delete[] reinterpret_cast<const uint8_t*>(item.meshMessage.extraDataPtr);
          continue;
        }
        default:
          break;
      }
      if (!appendData(&item, FrameCaptureMessageDataSize[idx])) {
        return DequeueStatus::ConnectionLost;
      }
    }
    refTimeThread = refThread;
  } else {
    return DequeueStatus::QueueEmpty;
  }
  return DequeueStatus::DataDequeued;
}
}  // namespace tgfx::inspect
