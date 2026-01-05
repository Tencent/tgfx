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

#include "NativeCodec.h"
#include <combaseapi.h>
#include <wrl/client.h>
#include <vector>
#include "core/utils/ColorSpaceHelper.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Pixmap.h"

using Microsoft::WRL::ComPtr;

namespace tgfx {

static ComPtr<IWICImagingFactory> InitWICFactory() {
  ComPtr<IWICImagingFactory> factory = nullptr;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  }
  return factory;
}

static ComPtr<IWICImagingFactory> GetWICFactory() {
  static ComPtr<IWICImagingFactory> factory = InitWICFactory();
  return factory;
}

static Orientation GetOrientation(IWICBitmapFrameDecode* frame) {
  ComPtr<IWICMetadataQueryReader> metadataReader = nullptr;
  if (FAILED(frame->GetMetadataQueryReader(&metadataReader)) || metadataReader == nullptr) {
    return Orientation::TopLeft;
  }

  // Different image formats store orientation in different metadata paths
  static const wchar_t* orientationPaths[] = {
      L"/app1/ifd/{ushort=274}",  // JPEG EXIF
      L"/ifd/{ushort=274}",       // TIFF, HEIC
      L"/{ushort=274}",           // Some HEIC variants
      L"/xmp/tiff:Orientation",   // XMP embedded (PNG, etc.)
  };

  PROPVARIANT value = {};
  PropVariantInit(&value);

  bool found = false;
  for (const auto* path : orientationPaths) {
    if (SUCCEEDED(metadataReader->GetMetadataByName(path, &value))) {
      found = true;
      break;
    }
  }

  if (!found) {
    PropVariantClear(&value);
    return Orientation::TopLeft;
  }

  Orientation orientation = Orientation::TopLeft;
  if (value.vt == VT_UI2) {
    auto orientationValue = value.uiVal;
    if (orientationValue >= 1 && orientationValue <= 8) {
      orientation = static_cast<Orientation>(orientationValue);
    }
  } else if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
    // XMP stores orientation as string
    auto orientationValue = _wtoi(value.pwszVal);
    if (orientationValue >= 1 && orientationValue <= 8) {
      orientation = static_cast<Orientation>(orientationValue);
    }
  }

  PropVariantClear(&value);
  return orientation;
}

static std::shared_ptr<ColorSpace> GetColorSpace(IWICImagingFactory* factory,
                                                 IWICBitmapFrameDecode* frame) {
  if (factory == nullptr || frame == nullptr) {
    return nullptr;
  }

  // Get the number of color contexts
  UINT colorContextCount = 0;
  if (FAILED(frame->GetColorContexts(0, nullptr, &colorContextCount)) || colorContextCount == 0) {
    return nullptr;
  }

  // Create color contexts for all
  std::vector<ComPtr<IWICColorContext>> colorContexts(colorContextCount);
  std::vector<IWICColorContext*> contextPtrs(colorContextCount);
  for (UINT i = 0; i < colorContextCount; ++i) {
    if (FAILED(factory->CreateColorContext(&colorContexts[i]))) {
      return nullptr;
    }
    contextPtrs[i] = colorContexts[i].Get();
  }

  UINT actualCount = 0;
  if (FAILED(frame->GetColorContexts(colorContextCount, contextPtrs.data(), &actualCount))) {
    return nullptr;
  }

  // Find the first ICC Profile
  for (UINT i = 0; i < actualCount; ++i) {
    WICColorContextType contextType = WICColorContextUninitialized;
    if (FAILED(colorContexts[i]->GetType(&contextType))) {
      continue;
    }
    if (contextType != WICColorContextProfile) {
      continue;
    }
    UINT profileSize = 0;
    if (FAILED(colorContexts[i]->GetProfileBytes(0, nullptr, &profileSize)) || profileSize == 0) {
      continue;
    }
    Buffer iccBuffer = {};
    if (!iccBuffer.alloc(profileSize)) {
      continue;
    }
    if (SUCCEEDED(colorContexts[i]->GetProfileBytes(
            profileSize, static_cast<BYTE*>(iccBuffer.data()), &profileSize))) {
      return ColorSpace::MakeFromICC(iccBuffer.data(), profileSize);
    }
  }

  return nullptr;
}

static WICPixelFormatGUID GetWICPixelFormat(ColorType colorType, AlphaType alphaType) {
  if (colorType == ColorType::BGRA_8888) {
    return alphaType == AlphaType::Premultiplied ? GUID_WICPixelFormat32bppPBGRA
                                                 : GUID_WICPixelFormat32bppBGRA;
  }
  if (colorType == ColorType::RGBA_8888) {
    return alphaType == AlphaType::Premultiplied ? GUID_WICPixelFormat32bppPRGBA
                                                 : GUID_WICPixelFormat32bppRGBA;
  }
  if (colorType == ColorType::ALPHA_8 || colorType == ColorType::Gray_8) {
    return GUID_WICPixelFormat8bppGray;
  }
  return GUID_WICPixelFormatUndefined;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(const std::string& filePath) {
  auto factory = GetWICFactory();
  if (factory == nullptr) {
    return nullptr;
  }
  int pathLength = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
  if (pathLength <= 0) {
    return nullptr;
  }
  std::wstring widePath(static_cast<size_t>(pathLength), 0);
  MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, widePath.data(), pathLength);

  ComPtr<IWICBitmapDecoder> decoder = nullptr;
  if (FAILED(factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder))) {
    return nullptr;
  }
  ComPtr<IWICBitmapFrameDecode> frame = nullptr;
  if (FAILED(decoder->GetFrame(0, &frame))) {
    return nullptr;
  }
  UINT width = 0;
  UINT height = 0;
  if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
    return nullptr;
  }
  auto orientation = GetOrientation(frame.Get());
  auto colorSpace = GetColorSpace(factory.Get(), frame.Get());
  auto codec = new NativeCodec(static_cast<int>(width), static_cast<int>(height), orientation,
                               std::move(colorSpace));
  codec->imagePath = filePath;
  return std::shared_ptr<ImageCodec>(codec);
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(std::shared_ptr<Data> imageBytes) {
  if (imageBytes == nullptr || imageBytes->size() == 0) {
    return nullptr;
  }
  auto factory = GetWICFactory();
  if (factory == nullptr) {
    return nullptr;
  }
  ComPtr<IWICStream> stream = nullptr;
  if (FAILED(factory->CreateStream(&stream))) {
    return nullptr;
  }
  if (FAILED(stream->InitializeFromMemory(
          const_cast<BYTE*>(static_cast<const BYTE*>(imageBytes->data())),
          static_cast<DWORD>(imageBytes->size())))) {
    return nullptr;
  }
  ComPtr<IWICBitmapDecoder> decoder = nullptr;
  if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                              &decoder))) {
    return nullptr;
  }
  ComPtr<IWICBitmapFrameDecode> frame = nullptr;
  if (FAILED(decoder->GetFrame(0, &frame))) {
    return nullptr;
  }
  UINT width = 0;
  UINT height = 0;
  if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
    return nullptr;
  }
  auto orientation = GetOrientation(frame.Get());
  auto colorSpace = GetColorSpace(factory.Get(), frame.Get());
  auto codec = new NativeCodec(static_cast<int>(width), static_cast<int>(height), orientation,
                               std::move(colorSpace));
  codec->imageBytes = imageBytes;
  return std::shared_ptr<ImageCodec>(codec);
}

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(NativeImageRef) {
  // Windows does not have a native image type like CGImageRef or Android Bitmap.
  return nullptr;
}

NativeCodec::~NativeCodec() = default;

bool NativeCodec::decodePixels(IWICBitmapSource* source, ColorType colorType, AlphaType alphaType,
                               size_t dstRowBytes, void* dstPixels) const {
  auto factory = GetWICFactory();
  if (factory == nullptr || source == nullptr) {
    return false;
  }

  auto targetFormat = GetWICPixelFormat(colorType, alphaType);
  if (IsEqualGUID(targetFormat, GUID_WICPixelFormatUndefined)) {
    return false;
  }

  WICPixelFormatGUID sourceFormat = {};
  if (FAILED(source->GetPixelFormat(&sourceFormat))) {
    return false;
  }

  ComPtr<IWICBitmapSource> convertedSource = nullptr;
  if (!IsEqualGUID(sourceFormat, targetFormat)) {
    ComPtr<IWICFormatConverter> converter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&converter))) {
      return false;
    }
    if (FAILED(converter->Initialize(source, targetFormat, WICBitmapDitherTypeNone, nullptr, 0.0f,
                                     WICBitmapPaletteTypeCustom))) {
      return false;
    }
    convertedSource = converter;
  } else {
    convertedSource = source;
  }

  WICRect rect = {0, 0, width(), height()};
  return SUCCEEDED(convertedSource->CopyPixels(&rect, static_cast<UINT>(dstRowBytes),
                                               static_cast<UINT>(dstRowBytes * height()),
                                               static_cast<BYTE*>(dstPixels)));
}

bool NativeCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                               std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const {
  if (dstPixels == nullptr) {
    return false;
  }
  auto factory = GetWICFactory();
  if (factory == nullptr) {
    return false;
  }

  ComPtr<IWICBitmapDecoder> decoder = nullptr;
  ComPtr<IWICStream> stream = nullptr;

  if (!imagePath.empty()) {
    int pathLength = MultiByteToWideChar(CP_UTF8, 0, imagePath.c_str(), -1, nullptr, 0);
    if (pathLength <= 0) {
      return false;
    }
    std::wstring widePath(static_cast<size_t>(pathLength), 0);
    MultiByteToWideChar(CP_UTF8, 0, imagePath.c_str(), -1, widePath.data(), pathLength);
    if (FAILED(factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder))) {
      return false;
    }
  } else if (imageBytes != nullptr) {
    if (FAILED(factory->CreateStream(&stream))) {
      return false;
    }
    if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(static_cast<const BYTE*>(imageBytes->data())),
            static_cast<DWORD>(imageBytes->size())))) {
      return false;
    }
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                WICDecodeMetadataCacheOnDemand, &decoder))) {
      return false;
    }
  } else {
    return false;
  }

  ComPtr<IWICBitmapFrameDecode> frame = nullptr;
  if (FAILED(decoder->GetFrame(0, &frame))) {
    return false;
  }

  // Handle color type conversion
  if ((colorType != ColorType::RGBA_8888 && colorType != ColorType::BGRA_8888 &&
       colorType != ColorType::ALPHA_8 && colorType != ColorType::Gray_8) ||
      NeedConvertColorSpace(colorSpace(), dstColorSpace)) {
    // Decode to BGRA_8888 first, then convert
    auto tempInfo =
        ImageInfo::Make(width(), height(), ColorType::BGRA_8888, alphaType, 0, colorSpace());
    Buffer tempBuffer = {};
    if (!tempBuffer.alloc(tempInfo.byteSize())) {
      return false;
    }
    if (!decodePixels(frame.Get(), ColorType::BGRA_8888, alphaType, tempInfo.rowBytes(),
                      tempBuffer.data())) {
      return false;
    }
    auto dstInfo =
        ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
    return Pixmap(tempInfo, tempBuffer.data()).readPixels(dstInfo, dstPixels);
  }

  return decodePixels(frame.Get(), colorType, alphaType, dstRowBytes, dstPixels);
}

}  // namespace tgfx
