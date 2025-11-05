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

#include "core/codecs/webp/WebpCodec.h"
#include "core/codecs/webp/WebpUtility.h"
#include "core/utils/ColorSpaceHelper.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {

bool WebpCodec::IsWebp(const std::shared_ptr<Data>& data) {
  const char* bytes = static_cast<const char*>(data->data());
  return data->size() >= 14 && !memcmp(bytes, "RIFF", 4) && !memcmp(&bytes[8], "WEBPVP", 6);
}

std::shared_ptr<ImageCodec> WebpCodec::MakeFrom(const std::string& filePath) {
  auto info = WebpUtility::getDecodeInfo(filePath);
  if (info.width == 0 || info.height == 0) {
    auto data = Data::MakeFromFile(filePath);
    info = WebpUtility::getDecodeInfo(data->data(), data->size());
    if (info.width == 0 || info.height == 0) {
      return nullptr;
    }
  }
  return std::shared_ptr<ImageCodec>(
      new WebpCodec(info.width, info.height, info.orientation, filePath, nullptr, info.colorSpace));
}

std::shared_ptr<ImageCodec> WebpCodec::MakeFrom(std::shared_ptr<Data> imageBytes) {
  if (imageBytes == nullptr) {
    return nullptr;
  }
  auto info = WebpUtility::getDecodeInfo(imageBytes->data(), imageBytes->size());
  if (info.width == 0 || info.height == 0) {
    return nullptr;
  }
  return std::shared_ptr<ImageCodec>(new WebpCodec(info.width, info.height, info.orientation, "",
                                                   std::move(imageBytes), info.colorSpace));
}

static WEBP_CSP_MODE webp_decode_mode(ColorType dstCT, bool premultiply) {
  switch (dstCT) {
    case ColorType::BGRA_8888:
      return premultiply ? MODE_bgrA : MODE_BGRA;
    case ColorType::RGBA_8888:
      return premultiply ? MODE_rgbA : MODE_RGBA;
    default:
      return MODE_LAST;
  }
}

bool WebpCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                             std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const {
  if (dstPixels == nullptr) {
    return false;
  }
  auto byteData = fileData;
  if (byteData == nullptr) {
    byteData = Data::MakeFromFile(filePath);
  }
  if (byteData == nullptr) {
    return false;
  }
  auto info = WebpUtility::getDecodeInfo(byteData->data(), byteData->size());
  WebPDecoderConfig config;
  if (!WebPInitDecoderConfig(&config)) {
    return false;
  }
  if (WebPGetFeatures(byteData->bytes(), byteData->size(), &config.input) != VP8_STATUS_OK) {
    return false;
  }
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  config.output.is_external_memory = 1;
  config.output.colorspace = webp_decode_mode(colorType, alphaType == AlphaType::Premultiplied);
  bool decodeSuccess = true;
  if (config.output.colorspace == MODE_LAST) {
    // decode to RGBA_8888
    auto info =
        ImageInfo::Make(width(), height(), ColorType::RGBA_8888, alphaType, 0, colorSpace());
    config.output.colorspace =
        webp_decode_mode(info.colorType(), info.alphaType() == AlphaType::Premultiplied);
    config.output.u.RGBA.stride = static_cast<int>(info.rowBytes());
    config.output.u.RGBA.size = info.byteSize();
    Buffer buffer(info.byteSize());
    auto pixels = buffer.bytes();
    if (pixels) {
      config.output.u.RGBA.rgba = pixels;
      decodeSuccess = WebPDecode(byteData->bytes(), byteData->size(), &config) == VP8_STATUS_OK;
      if (decodeSuccess) {
        Pixmap pixmap(info, pixels);
        decodeSuccess = pixmap.readPixels(dstInfo, dstPixels);
      }
    }
  } else {
    auto outPixels = dstPixels;
    Buffer buffer;
    Pixmap tempPixelMap;
    if (!ColorSpaceIsEqual(colorSpace(), dstColorSpace)) {
      auto tempImageInfo =
          ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, colorSpace());
      buffer.alloc(tempImageInfo.byteSize());
      tempPixelMap.reset(tempImageInfo, buffer.data());
      outPixels = tempPixelMap.writablePixels();
    }
    config.output.u.RGBA.rgba = reinterpret_cast<uint8_t*>(outPixels);
    config.output.u.RGBA.stride = static_cast<int>(dstRowBytes);
    config.output.u.RGBA.size = dstRowBytes * static_cast<size_t>(height());
    auto code = WebPDecode(byteData->bytes(), byteData->size(), &config);
    decodeSuccess = (code == VP8_STATUS_OK);
    if (decodeSuccess && !tempPixelMap.isEmpty()) {
      tempPixelMap.readPixels(dstInfo, dstPixels);
    }
  }
  WebPFreeDecBuffer(&config.output);
  return decodeSuccess;
}

std::shared_ptr<Data> WebpCodec::getEncodedData() const {
  if (fileData) {
    return fileData;
  }
  if (!filePath.empty()) {
    return Data::MakeFromFile(filePath);
  }
  return nullptr;
}

#ifdef TGFX_USE_WEBP_ENCODE
struct WebpWriter {
  unsigned char* data = nullptr;
  size_t length = 0;
};

static int webp_reader_write_data(const uint8_t* data, size_t data_size,
                                  const WebPPicture* picture) {
  auto writer = static_cast<WebpWriter*>(picture->custom_ptr);
  size_t nsize = writer->length + data_size;
  if (writer->data) {
    writer->data = static_cast<unsigned char*>(realloc(writer->data, nsize));
  } else {
    writer->data = static_cast<unsigned char*>(malloc(nsize));
  }
  memcpy(writer->data + writer->length, data, data_size);
  writer->length += data_size;
  return 1;
}

std::shared_ptr<Data> WebpCodec::Encode(const Pixmap& pixmap, int quality) {
  const uint8_t* srcPixels = static_cast<uint8_t*>(const_cast<void*>(pixmap.pixels()));
  auto srcInfo = pixmap.info();
  Buffer tempBuffer = {};
  if (pixmap.alphaType() == AlphaType::Premultiplied ||
      (pixmap.colorType() != ColorType::RGBA_8888 && pixmap.colorType() != ColorType::BGRA_8888)) {
    auto alphaType =
        pixmap.alphaType() == AlphaType::Opaque ? AlphaType::Opaque : AlphaType::Unpremultiplied;
    srcInfo = ImageInfo::Make(pixmap.width(), pixmap.height(), ColorType::RGBA_8888, alphaType, 0,
                              pixmap.colorSpace());
    tempBuffer.alloc(srcInfo.byteSize());
    srcPixels = tempBuffer.bytes();
    if (!pixmap.readPixels(srcInfo, tempBuffer.data())) {
      return nullptr;
    }
  }
  WebPConfig webp_config;
  bool isLossless = false;
  if (quality == 100) {
    quality = 75;
    isLossless = true;
  }
  if (!WebPConfigPreset(&webp_config, WEBP_PRESET_DEFAULT, static_cast<float>(quality))) {
    return nullptr;
  }
  WebPPicture pic;
  WebPPictureInit(&pic);
  pic.width = srcInfo.width();
  pic.height = srcInfo.height();
  pic.writer = webp_reader_write_data;
  if (isLossless) {
    webp_config.lossless = 1;
    webp_config.method = 1;
    pic.use_argb = 1;
  } else {
    webp_config.lossless = 0;
    webp_config.method = 3;
    pic.use_argb = 0;
  }

  WebpWriter webpWriter;
  pic.custom_ptr = &webpWriter;
  auto importProc = WebPPictureImportRGBX;
  if (ColorType::RGBA_8888 == srcInfo.colorType()) {
    if (AlphaType::Opaque == srcInfo.alphaType()) {
      importProc = WebPPictureImportRGBX;
    } else {
      importProc = WebPPictureImportRGBA;
    }
  } else if (ColorType::BGRA_8888 == srcInfo.colorType()) {
    if (AlphaType::Opaque == srcInfo.alphaType()) {
      importProc = WebPPictureImportBGRX;
    } else {
      importProc = WebPPictureImportBGRA;
    }
  }
  auto rowBytes = static_cast<int>(srcInfo.rowBytes());
  if (!importProc(&pic, srcPixels, rowBytes) || !WebPEncode(&webp_config, &pic)) {
    WebPPictureFree(&pic);
    if (webpWriter.data) {
      free(webpWriter.data);
    }
    return nullptr;
  }
  WebPPictureFree(&pic);
  auto encodedData = Data::MakeAdopted(webpWriter.data, webpWriter.length, Data::FreeProc);
  if (srcInfo.colorSpace()) {
    auto icc = srcInfo.colorSpace()->toICCProfile();
    if (icc) {
      WebPData encoded = {encodedData->bytes(), encodedData->size()};
      WebPData iccChunk = {icc->bytes(), icc->size()};
      WebPMux* mux = WebPMuxNew();
      if (WEBP_MUX_OK != WebPMuxSetImage(mux, &encoded, 1)) {
        WebPMuxDelete(mux);
        return nullptr;
      }
      if (WEBP_MUX_OK != WebPMuxSetChunk(mux, "ICCP", &iccChunk, 1)) {
        WebPMuxDelete(mux);
        return nullptr;
      }
      WebPData assembled;
      if (WEBP_MUX_OK != WebPMuxAssemble(mux, &assembled)) {
        WebPMuxDelete(mux);
        WebPDataClear(&assembled);
        return nullptr;
      }
      auto encodedDataWithICC = Data::MakeWithCopy(assembled.bytes, assembled.size);
      WebPMuxDelete(mux);
      WebPDataClear(&assembled);
      return encodedDataWithICC;
    }
  }
  return encodedData;
}
#endif

}  // namespace tgfx
