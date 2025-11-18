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

#pragma once

#include <list>
#include "RectPackSkyline.h"
#include "../../include/tgfx/core/Log.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
enum class MaskFormat : int { A8, RGBA, BGRA, Last = BGRA };

static constexpr int MaskFormatCount = static_cast<int>(MaskFormat::Last) + 1;

/**
 * Keep track of generation number for atlases and Plots.
 */
class AtlasGenerationCounter {
 public:
  constexpr static uint64_t kInvalidGeneration = 0;

  uint64_t next() {
    return generation++;
  }

 private:
  uint64_t generation = 1;
};

class AtlasToken {
 public:
  static AtlasToken InvalidToken() {
    return AtlasToken(0);
  }

  AtlasToken() = delete;

  bool operator==(const AtlasToken& other) const {
    return sequenceNumber == other.sequenceNumber;
  }
  bool operator!=(const AtlasToken& other) const {
    return !(*this == other);
  }
  bool operator<(const AtlasToken& other) const {
    return sequenceNumber < other.sequenceNumber;
  }
  bool operator<=(const AtlasToken& other) const {
    return sequenceNumber <= other.sequenceNumber;
  }
  bool operator>(const AtlasToken& other) const {
    return sequenceNumber > other.sequenceNumber;
  }

  bool operator>=(const AtlasToken& other) const {
    return sequenceNumber >= other.sequenceNumber;
  }

  AtlasToken& operator++() {
    sequenceNumber++;
    return *this;
  }
  AtlasToken operator++(int) {
    AtlasToken copy = *this;
    sequenceNumber++;
    return copy;
  }

  AtlasToken next() const {
    return AtlasToken(sequenceNumber + 1);
  }

  bool isInterval(const AtlasToken& start, const AtlasToken& end) const {
    return *this >= start && *this < end;
  }

 private:
  explicit AtlasToken(uint64_t sequenceNumber) : sequenceNumber(sequenceNumber) {
  }
  uint64_t sequenceNumber = 0;
};

class AtlasTokenTracker {
 public:
  // Get the next flush token. in DrawingManger::flush()
  AtlasToken nextToken() const {
    return currentToken.next();
  }
  void advanceToken() {
    ++currentToken;
  }

 private:
  AtlasToken currentToken = AtlasToken::InvalidToken();
};

class PlotLocator {
 public:
  static constexpr uint32_t MaxResidentPages = 4;
  static constexpr uint32_t MaxPlots = 32;

  PlotLocator(uint32_t pageIndex, uint32_t plotIndex, uint64_t generation)
      : _genID(generation), _plotIndex(plotIndex), _pageIndex(pageIndex) {
    DEBUG_ASSERT(pageIndex < MaxResidentPages);
    DEBUG_ASSERT(plotIndex < MaxPlots);
    DEBUG_ASSERT(generation < static_cast<uint64_t>(1) << 48);
  }

  PlotLocator() : _genID(AtlasGenerationCounter::kInvalidGeneration), _plotIndex(0), _pageIndex(0) {
  }

  bool isValid() const {
    return _genID != AtlasGenerationCounter::kInvalidGeneration || _plotIndex != 0 ||
           _pageIndex != 0;
  }

  bool operator==(const PlotLocator& other) const {
    return _genID == other._genID && _plotIndex == other._plotIndex &&
           _pageIndex == other._pageIndex;
  }

  uint32_t pageIndex() const {
    return _pageIndex;
  }

  uint32_t plotIndex() const {
    return _plotIndex;
  }

  uint64_t genID() const {
    return _genID;
  }

 private:
  uint64_t _genID : 48;
  uint64_t _plotIndex : 8;
  uint64_t _pageIndex : 8;
};

class AtlasLocator {
 public:
  const Rect& getLocation() const {
    return location;
  }

  const PlotLocator& plotLocator() const {
    return _plotLocator;
  }

  uint32_t pageIndex() const {
    return _plotLocator.pageIndex();
  }
  uint32_t plotIndex() const {
    return _plotLocator.plotIndex();
  }

  uint64_t genID() const {
    return _plotLocator.genID();
  }

  void updateRect(const Rect& rect) {
    location = rect;
  }

  void setPlotLocator(const PlotLocator& plotLocator) {
    this->_plotLocator = plotLocator;
  }

 private:
  PlotLocator _plotLocator;
  Rect location = {};
};

class PlotUseUpdater {
 public:
  bool add(const PlotLocator& plotLocator);

  void reset() {
    plotAlreadyUpdated.clear();
  }

 private:
  bool find(uint32_t pageIndex, uint32_t plotIndex) const;

  void set(uint32_t pageIndex, uint32_t plotIndex);

  std::vector<uint32_t> plotAlreadyUpdated = {};
};

class Plot {
 public:
  static constexpr int CellPadding = 1;

  Plot(uint32_t pageIndex, uint32_t plotIndex, AtlasGenerationCounter* generationCounter,
       int offsetX, int offsetY, int width, int height);

  uint32_t pageIndex() const {
    return _pageIndex;
  }

  uint32_t plotIndex() const {
    return _plotIndex;
  }

  uint64_t genID() const {
    return _genID;
  }

  const PlotLocator& plotLocator() const {
    return _plotLocator;
  }

  const Point& pixelOffset() const {
    return _pixelOffset;
  }

  bool addRect(int with, int height, AtlasLocator& atlasLocator);

  void resetRects();

  AtlasToken lastUseToken() const {
    return _lastUseToken;
  }
  void setLastUseToken(AtlasToken token) {
    _lastUseToken = token;
  }

  uint32_t flushesSinceLastUsed() const {
    return _flushesSinceLastUsed;
  }
  void resetFlushesSinceLastUsed() {
    _flushesSinceLastUsed = 0;
  }
  void increaseFlushesSinceLastUsed() {
    ++_flushesSinceLastUsed;
  }

 private:
  AtlasToken _lastUseToken = AtlasToken::InvalidToken();
  uint32_t _flushesSinceLastUsed = 0;

  AtlasGenerationCounter* const generationCounter = nullptr;
  const uint32_t _pageIndex = 0;
  const uint32_t _plotIndex = 0;
  uint64_t _genID = 0;
  const Point _pixelOffset = {};
  RectPackSkyline rectPack;
  PlotLocator _plotLocator;
};

using PlotList = std::list<Plot*>;
}  // namespace tgfx
