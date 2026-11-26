#pragma once

#include <memory>
#include "tgfx/core/EncodedFormat.h"

namespace tgfx {

class Data;
class ImageInfo;
class Pixmap;
class WriteStream;

class ScanlineEncoder {
 public:
  static std::unique_ptr<ScanlineEncoder> Make(WriteStream* stream, const ImageInfo& info,
                                               EncodedFormat format, int quality,
                                               bool optimizeJPEG);
  static std::shared_ptr<Data> Encode(const Pixmap& pixmap, EncodedFormat format, int quality);

  virtual ~ScanlineEncoder() = default;
  virtual bool appendRows(const Pixmap& band) = 0;
  virtual bool finish() = 0;
};

}  // namespace tgfx
