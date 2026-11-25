/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "layers/BackgroundHandler.h"
#include "layers/TilePool.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {
static TileKey GridKey(int tileX) {
  return TileKey::MakeGrid(tileX, 0, 1000);
}

static TileKey TreeKey(int tileX, int level = 0) {
  return TileKey::MakeTree(tileX, 0, level);
}

TGFX_TEST(TilePoolTest, LevelForZoomScale) {
  struct Sample {
    float zoomScale;
    int level;
  };
  Sample samples[] = {{4.0f, -2}, {2.0f, -1}, {1.0f, 0}, {0.7f, 0},
                      {0.5f, 1},  {0.02f, 5}, {0.01f, 6}};
  for (auto& sample : samples) {
    EXPECT_EQ(GetTileLevel(sample.zoomScale), sample.level) << sample.zoomScale;
    auto tilePixels = 256.0f * sample.zoomScale / GetTileRasterScale(sample.level);
    EXPECT_GT(tilePixels, 128.0f) << sample.zoomScale;
    EXPECT_LE(tilePixels, 256.0f) << sample.zoomScale;
  }
  EXPECT_EQ(GetTileRasterScale(-2), 4.0f);
  EXPECT_EQ(GetTileRasterScale(3), 0.125f);
}

TGFX_TEST(TilePoolTest, KeysKeepLevelsAndTypesApart) {
  TilePool pool = {};
  auto gridTile = std::make_shared<Tile>();
  auto treeTile = std::make_shared<Tile>();
  auto negativeTile = std::make_shared<Tile>();
  pool.insert(GridKey(1), gridTile);
  pool.insert(TreeKey(1), treeTile);
  pool.insert(TileKey::MakeTree(-3, -5, -2), negativeTile);
  EXPECT_EQ(pool.find(GridKey(1)), gridTile);
  EXPECT_EQ(pool.find(TreeKey(1)), treeTile);
  EXPECT_EQ(pool.find(TileKey::MakeTree(-3, -5, -2)), negativeTile);
  EXPECT_EQ(pool.find(TreeKey(1, 1)), nullptr);
  EXPECT_EQ(pool.size(TileType::Grid), 1u);
  EXPECT_EQ(pool.size(TileType::Tree), 2u);
}

TGFX_TEST(TilePoolTest, EvictsLeastRecentlyUsedOfSameType) {
  TilePool pool = {};
  for (int i = 0; i < 3; ++i) {
    pool.insert(GridKey(i), std::make_shared<Tile>());
    pool.insert(TreeKey(i), std::make_shared<Tile>());
  }
  pool.touch(GridKey(0));
  auto evicted = pool.evict(TileType::Grid, 1);
  EXPECT_EQ(evicted.size(), 1u);
  EXPECT_EQ(pool.find(GridKey(1)), nullptr);
  EXPECT_NE(pool.find(GridKey(0)), nullptr);
  EXPECT_NE(pool.find(GridKey(2)), nullptr);
  EXPECT_EQ(pool.size(TileType::Tree), 3u);
}

TGFX_TEST(TilePoolTest, EvictSkipsTilesInUse) {
  TilePool pool = {};
  auto inUse = std::make_shared<Tile>();
  pool.insert(TreeKey(0), inUse);
  pool.insert(TreeKey(1), std::make_shared<Tile>());
  auto evicted = pool.evict(TileType::Tree, 2);
  EXPECT_EQ(evicted.size(), 1u);
  EXPECT_EQ(pool.find(TreeKey(0)), inUse);
  EXPECT_EQ(pool.find(TreeKey(1)), nullptr);
}

TGFX_TEST(TilePoolTest, EraseAndClearReturnTiles) {
  TilePool pool = {};
  auto tile = std::make_shared<Tile>();
  pool.insert(GridKey(0), tile);
  pool.insert(GridKey(1), std::make_shared<Tile>());
  pool.insert(TreeKey(0), std::make_shared<Tile>());
  EXPECT_EQ(pool.erase(GridKey(0)), tile);
  EXPECT_EQ(pool.erase(GridKey(0)), nullptr);
  EXPECT_EQ(pool.clear(TileType::Grid).size(), 1u);
  EXPECT_EQ(pool.size(TileType::Grid), 0u);
  EXPECT_EQ(pool.size(TileType::Tree), 1u);
}

static std::shared_ptr<SolidLayer> MakeSolid(float width, float height, Color color) {
  auto layer = SolidLayer::Make();
  layer->setWidth(width);
  layer->setHeight(height);
  layer->setColor(color);
  return layer;
}

static uint32_t ReadPixel(const std::shared_ptr<Surface>& surface, int x, int y) {
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t pixel = 0;
  EXPECT_TRUE(surface->readPixels(info, &pixel, x, y));
  return pixel;
}

static constexpr uint32_t OPAQUE_RED = 0xFF0000FFu;
static constexpr uint32_t OPAQUE_BLUE = 0xFFFF0000u;
static constexpr uint32_t OPAQUE_WHITE = 0xFFFFFFFFu;

TGFX_TEST_PRIVATE(TilePoolTest, ZoomWithoutBudgetFillsViewportFromTreeLevels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());

  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 10, 10), OPAQUE_RED);
  EXPECT_EQ(ReadPixel(surface, 500, 500), OPAQUE_RED);
  // A zero budget never rasterizes the exact tiles while the viewport is still, so nothing pending
  // can progress and the loop has to stop.
  EXPECT_FALSE(displayList->hasContentChanged());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 0u);
                      auto treeCount = displayList->tilePool->size(TileType::Tree);
                      EXPECT_GT(treeCount, 0u);
                      // The second zoom reuses the absolute level 3 tiles rather than adding more.
                      displayList->setZoomScale(0.75f); displayList->render(surface.get());
                      EXPECT_EQ(displayList->tilePool->size(TileType::Tree), treeCount));
}

TGFX_TEST(TilePoolTest, LeavingTiledModeDropsPendingFallbackWork) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(1);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  ASSERT_TRUE(displayList->hasContentChanged());

  // The other modes never refine tiles, so the work the tiled frames left pending must not keep
  // asking for frames once they take over.
  displayList->setRenderMode(RenderMode::Direct);
  displayList->render(surface.get());
  EXPECT_FALSE(displayList->hasContentChanged());
}

TGFX_TEST_PRIVATE(TilePoolTest, FastModeLeavesUncoveredCellsToTheBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Fast);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->setBackgroundColor(Color::White());
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());

  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  // The zero budget leaves every cell without a tile, and Fast mode does not rasterize a fallback
  // for them within the frame, so they show the background instead. The Smooth mode counterpart is
  // covered by ZoomWithoutBudgetFillsViewportFromTreeLevels.
  EXPECT_EQ(ReadPixel(surface, 10, 10), OPAQUE_WHITE);
  EXPECT_EQ(ReadPixel(surface, 500, 500), OPAQUE_WHITE);
  // Fast mode leaves those cells to the background and the zero budget leaves the grid empty, so no
  // frame would ever change them and the loop has to stop.
  EXPECT_FALSE(displayList->hasContentChanged());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 0u);
                      EXPECT_EQ(displayList->tilePool->size(TileType::Tree), 0u));
}

TGFX_TEST_PRIVATE(TilePoolTest, BudgetRefinesFallbackIntoGridTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(1);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());

  displayList->setZoomScale(0.7f);
  // The grid fills first, then the fallback levels are topped up, so the frame count is open.
  for (int i = 0; i < 100 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
    EXPECT_EQ(ReadPixel(surface, 300, 300), OPAQUE_RED);
  }
  EXPECT_FALSE(displayList->hasContentChanged());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 4u));
}

TGFX_TEST_PRIVATE(TilePoolTest, FallbackLevelsFillAfterTheExactTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(2);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());

  displayList->setZoomScale(0.7f);
  size_t treeTiles = 0;
  for (int i = 0; i < 100 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
    EXPECT_EQ(ReadPixel(surface, 300, 300), OPAQUE_RED);
    // Once the grid is complete the remaining frames only top up the fallback levels, two tiles at
    // a time.
    TGFX_PRIVATE_ACCESS(auto count = displayList->tilePool->size(TileType::Tree);
                        EXPECT_GE(count, treeTiles);
                        if (displayList->tilePool->size(TileType::Grid) == 4u) {
                          EXPECT_LE(count - treeTiles, 2u);
                        } treeTiles = count);
  }
  EXPECT_FALSE(displayList->hasContentChanged());
  auto level = GetTileLevel(0.7f);
  // L+2 and L+1 are what the next zoom-in uses as its own L+3 and L+2, and they are filled whether
  // or not the idle fill goes all the way down to L.
  TGFX_PRIVATE_ACCESS(
      EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 4u);
      EXPECT_NE(displayList->tilePool->find(TileKey::MakeTree(0, 0, level + 1)), nullptr);
      EXPECT_NE(displayList->tilePool->find(TileKey::MakeTree(0, 0, level + 2)), nullptr));
}

// The first frame fills the grid in one go. A platform that only renders while the content changes
// must still get the frames that prepare the fallback levels for the next zoom.
TGFX_TEST_PRIVATE(TilePoolTest, FirstFramePreparesFallbackLevels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  EXPECT_TRUE(displayList->hasContentChanged());
  for (int i = 0; i < 100 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
  }
  EXPECT_FALSE(displayList->hasContentChanged());
  auto level = GetTileLevel(1.0f);
  TGFX_PRIVATE_ACCESS(
      EXPECT_NE(displayList->tilePool->find(TileKey::MakeTree(0, 0, level + 1)), nullptr);
      EXPECT_NE(displayList->tilePool->find(TileKey::MakeTree(0, 0, level + 2)), nullptr));
}

TGFX_TEST_PRIVATE(TilePoolTest, FallbackLevelsKeepTheLoopAliveOnDirtyFrames) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(2);
  auto solid = MakeSolid(4096, 4096, Color::Red());
  displayList->root()->addChild(solid);
  displayList->render(surface.get());

  displayList->setZoomScale(0.7f);
  // Stop as soon as the grid is complete, with the fallback levels half filled.
  for (int i = 0; i < 20 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
    TGFX_PRIVATE_ACCESS(if (displayList->tilePool->size(TileType::Grid) == 4u) { break; });
  }
  EXPECT_TRUE(displayList->hasContentChanged());

  // A frame that carries dirty work only reports the remaining chain; it must not stop the loop.
  solid->setColor(Color::Blue());
  displayList->render(surface.get());
  EXPECT_TRUE(displayList->hasContentChanged());

  for (int i = 0; i < 200 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
  }
  EXPECT_FALSE(displayList->hasContentChanged());
}

TGFX_TEST_PRIVATE(TilePoolTest, ZoomChangeRecyclesGridTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  TGFX_PRIVATE_ACCESS(auto atlasCount = displayList->surfaceCaches.size();
                      EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 4u));
  for (auto zoomScale : {0.9f, 0.8f, 1.1f, 1.2f}) {
    displayList->setZoomScale(zoomScale);
    displayList->render(surface.get());
    EXPECT_EQ(ReadPixel(surface, 10, 10), OPAQUE_RED);
  }
  // Slots from the previous zoom scale are reused, so zooming never allocates another atlas.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->surfaceCaches.size(), atlasCount));
}

TGFX_TEST_PRIVATE(TilePoolTest, DirtyRegionInvalidatesTreeTiles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(0);
  auto layer = MakeSolid(4096, 4096, Color::Red());
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 300, 300), OPAQUE_RED);

  layer->setColor(Color::Blue());
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 300, 300), OPAQUE_BLUE);
}

TGFX_TEST_PRIVATE(TilePoolTest, DirtyRegionPatchesFallbackTileInPlace) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  // At 0.7 the viewport level is 0, so the only fallback tile drawn is level 3 at (0, 0).
  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  auto key = TileKey::MakeTree(0, 0, 3);
  std::shared_ptr<Tile> tile = nullptr;
  TGFX_PRIVATE_ACCESS(tile = displayList->tilePool->find(key));
  ASSERT_NE(tile, nullptr);

  auto blue = MakeSolid(100, 100, Color::Blue());
  blue->setMatrix(Matrix::MakeTrans(200, 200));
  displayList->root()->addChild(blue);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 175, 175), OPAQUE_BLUE);
  EXPECT_EQ(ReadPixel(surface, 100, 100), OPAQUE_RED);
  EXPECT_EQ(ReadPixel(surface, 300, 300), OPAQUE_RED);
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->find(key), tile));
}

TGFX_TEST_PRIVATE(TilePoolTest, TimeBudgetDefersPatchesToLaterFrames) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 1024, 1024);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  auto first = MakeSolid(50, 50, Color::Red());
  first->setMatrix(Matrix::MakeTrans(20, 20));
  displayList->root()->addChild(first);
  auto second = MakeSolid(50, 50, Color::Red());
  // Tiles (0, 0) and (3, 3) fall in different 2x2 blocks, so they are drawn in separate groups.
  second->setMatrix(Matrix::MakeTrans(800, 800));
  displayList->root()->addChild(second);
  displayList->render(surface.get());
  EXPECT_FALSE(displayList->hasContentChanged());

  // The smallest budget runs out after the first group, so each frame patches a single block.
  displayList->setTileTimeBudget(1e-9f);
  first->setColor(Color::Blue());
  second->setColor(Color::Blue());
  displayList->render(surface.get());
  auto firstPixel = ReadPixel(surface, 40, 40);
  auto secondPixel = ReadPixel(surface, 820, 820);
  EXPECT_TRUE((firstPixel == OPAQUE_BLUE) != (secondPixel == OPAQUE_BLUE));
  EXPECT_TRUE(firstPixel == OPAQUE_RED || secondPixel == OPAQUE_RED);
  TGFX_PRIVATE_ACCESS(EXPECT_FALSE(displayList->deferredPatches.empty()));
  EXPECT_TRUE(displayList->hasContentChanged());

  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 40, 40), OPAQUE_BLUE);
  EXPECT_EQ(ReadPixel(surface, 820, 820), OPAQUE_BLUE);
  TGFX_PRIVATE_ACCESS(EXPECT_TRUE(displayList->deferredPatches.empty()));
}

TGFX_TEST_PRIVATE(TilePoolTest, TimeBudgetDefersNewTilesToFallbacks) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(8);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  // Leave part of the viewport without its exact tiles, so later frames have both grid tiles and
  // fallback levels to rasterize.
  displayList->setContentOffset(-256, 0);
  displayList->render(surface.get());
  // The smallest budget runs out after the first group, so each frame adds at most one 2x2 block.
  displayList->setTileTimeBudget(1e-9f);
  auto tileCount = [&]() {
    size_t count = 0;
    TGFX_PRIVATE_ACCESS(count = displayList->tilePool->size(TileType::Grid) +
                                displayList->tilePool->size(TileType::Tree));
    return count;
  };
  auto lastCount = tileCount();
  int frames = 0;
  while (displayList->hasContentChanged() && frames < 64) {
    displayList->render(surface.get());
    ++frames;
    // Cells whose tiles wait for a later frame show a fallback rather than the background.
    EXPECT_EQ(ReadPixel(surface, 10, 10), OPAQUE_RED);
    EXPECT_EQ(ReadPixel(surface, 500, 500), OPAQUE_RED);
    auto count = tileCount();
    EXPECT_LE(count, lastCount + 4);
    lastCount = count;
  }
  EXPECT_GT(frames, 1);
  EXPECT_LT(frames, 64);
  TGFX_PRIVATE_ACCESS(EXPECT_TRUE(displayList->deferredPatches.empty()));
}

TGFX_TEST_PRIVATE(TilePoolTest, DirtyRegionDropsTilesOutsideFallbackLevels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));
  displayList->render(surface.get());
  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());
  // At 4.0 the fallback levels are -2 to 1, so the level 3 tile is cached but out of range.
  displayList->setZoomScale(4.0f);
  displayList->render(surface.get());
  std::shared_ptr<Tile> coarseTile = nullptr;
  TGFX_PRIVATE_ACCESS(EXPECT_NE(displayList->tilePool->find(TileKey::MakeTree(0, 0, 3)), nullptr);
                      coarseTile = displayList->tilePool->find(TileKey::MakeTree(0, 0, 1)));
  ASSERT_NE(coarseTile, nullptr);

  auto blue = MakeSolid(20, 20, Color::Blue());
  blue->setMatrix(Matrix::MakeTrans(10, 10));
  displayList->root()->addChild(blue);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 80, 80), OPAQUE_BLUE);
  TGFX_PRIVATE_ACCESS(
      EXPECT_EQ(displayList->tilePool->find(TileKey::MakeTree(0, 0, 3)), nullptr);
      EXPECT_EQ(displayList->tilePool->find(TileKey::MakeTree(0, 0, 1)), coarseTile));
}

TGFX_TEST_PRIVATE(TilePoolTest, CapacityFollowsViewport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->root()->addChild(MakeSolid(4096, 4096, Color::Red()));

  auto surface = Surface::Make(context, 1600, 900);
  ASSERT_NE(surface, nullptr);
  displayList->render(surface.get());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->quota(TileType::Tree), 376u);
                      EXPECT_EQ(displayList->tilePool->quota(TileType::Grid), 136u));

  auto largeSurface = Surface::Make(context, 2560, 1440);
  ASSERT_NE(largeSurface, nullptr);
  displayList->render(largeSurface.get());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->quota(TileType::Tree), 586u);
                      EXPECT_EQ(displayList->tilePool->quota(TileType::Grid), 182u));
  EXPECT_EQ(ReadPixel(largeSurface, 2000, 1000), OPAQUE_RED);

  auto smallSurface = Surface::Make(context, 1600, 900);
  ASSERT_NE(smallSurface, nullptr);
  displayList->render(smallSurface.get());
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->quota(TileType::Tree), 376u);
                      EXPECT_EQ(displayList->tilePool->quota(TileType::Grid), 136u);
                      EXPECT_LE(displayList->tilePool->size(TileType::Grid),
                                displayList->tilePool->quota(TileType::Grid)));
}

TGFX_TEST(TilePoolTest, BlankTileSetGroupsAndRanges) {
  BlankTileSet blanks = {};
  blanks.insert(TileKey::MakeGrid(1, 1, 1000));
  blanks.insert(TileKey::MakeGrid(-2, 3, 1000));
  blanks.insert(TileKey::MakeTree(-1, -1, 2));
  blanks.insert(TileKey::MakeTree(5, 5, 2));
  EXPECT_EQ(blanks.size(), 4u);
  EXPECT_TRUE(blanks.contains(TileKey::MakeGrid(-2, 3, 1000)));
  EXPECT_FALSE(blanks.contains(TileKey::MakeGrid(-2, 3, 1100)));
  EXPECT_FALSE(blanks.contains(TileKey::MakeTree(-1, -1, 3)));

  blanks.eraseUnderRect(TileKey::MakeTree(0, 0, 2), Rect::MakeXYWH(-10, -10, 20, 20), 256);
  EXPECT_FALSE(blanks.contains(TileKey::MakeTree(-1, -1, 2)));
  EXPECT_TRUE(blanks.contains(TileKey::MakeTree(5, 5, 2)));
  // A range larger than the group is resolved by scanning the group instead.
  blanks.eraseUnderRect(TileKey::MakeGrid(0, 0, 1000), Rect::MakeXYWH(-1e6f, -1e6f, 2e6f, 2e6f),
                        256);
  EXPECT_EQ(blanks.size(), 1u);

  blanks.insert(TileKey::MakeGrid(0, 0, 1000));
  blanks.insert(TileKey::MakeGrid(0, 0, 1100));
  EXPECT_FALSE(blanks.contains(TileKey::MakeGrid(0, 0, 1000)));
  EXPECT_EQ(blanks.size(), 2u);

  blanks.setMaxCount(2);
  blanks.insert(TileKey::MakeTree(9, 9, 4));
  EXPECT_EQ(blanks.gridScale(), 0);
  EXPECT_TRUE(blanks.contains(TileKey::MakeTree(5, 5, 2)));
  EXPECT_EQ(blanks.size(), 2u);
}

TGFX_TEST_PRIVATE(TilePoolTest, BlankGridTilesTakeNoSlot) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 1024, 256);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setBackgroundColor(Color::White());
  displayList->root()->addChild(MakeSolid(200, 256, Color::Red()));
  auto right = MakeSolid(200, 256, Color::Red());
  right->setMatrix(Matrix::MakeTrans(824, 0));
  displayList->root()->addChild(right);
  displayList->render(surface.get());

  // The first frame rasterizes one continuous block, so the cells are judged after a zoom change.
  displayList->setZoomScale(0.999f);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 100, 100), OPAQUE_RED);
  EXPECT_EQ(ReadPixel(surface, 400, 100), OPAQUE_WHITE);
  TGFX_PRIVATE_ACCESS(
      EXPECT_EQ(displayList->tilePool->size(TileType::Grid), 2u);
      EXPECT_EQ(displayList->blankTiles->size(), 2u);

      displayList->render(surface.get()); EXPECT_EQ(ReadPixel(surface, 400, 100), OPAQUE_WHITE);

      // New content in a blank cell must show up, so the edit has to clear the mark.
      auto blue = MakeSolid(100, 100, Color::Blue());
      blue->setMatrix(Matrix::MakeTrans(300, 0)); displayList->root()->addChild(blue);
      displayList->render(surface.get()); EXPECT_EQ(ReadPixel(surface, 350, 50), OPAQUE_BLUE);
      EXPECT_EQ(ReadPixel(surface, 600, 100), OPAQUE_WHITE);
      TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->blankTiles->size(), 1u));)
}

TGFX_TEST_PRIVATE(TilePoolTest, BlankTreeTilesCoverViewport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->setBackgroundColor(Color::White());
  displayList->root()->addChild(MakeSolid(100, 4096, Color::Red()));
  auto far = MakeSolid(100, 4096, Color::Red());
  far->setMatrix(Matrix::MakeTrans(8000, 0));
  displayList->root()->addChild(far);
  displayList->render(surface.get());

  // At 0.7 the coarsest level 3 rasterizes at 1/8, so its tile x = 2 covers 4096..6144 in layer
  // space, which holds nothing.
  displayList->setZoomScale(0.7f);
  displayList->setContentOffset(-4200 * 0.7f, 0);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 256, 256), OPAQUE_WHITE);
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->tilePool->size(TileType::Tree), 0u);

                      displayList->render(surface.get());
                      EXPECT_EQ(ReadPixel(surface, 256, 256), OPAQUE_WHITE);)
}

TGFX_TEST_PRIVATE(TilePoolTest, MovingViewportRefinesCoarseLevelsFirst) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 1024, 1024);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(4);
  displayList->root()->addChild(MakeSolid(20000, 20000, Color::Red()));
  displayList->render(surface.get());

  // The zoom frame rasterizes the single level 3 tile over the viewport, then spends the budget on
  // the 4 level 2 tiles below it while grid tiles wait for the viewport to stop.
  displayList->setZoomScale(0.7f);
  displayList->render(surface.get());

  // The next moving frame refines one more level without skipping any.
  displayList->setContentOffset(-1, 0);
  displayList->render(surface.get());
  EXPECT_EQ(ReadPixel(surface, 500, 500), OPAQUE_RED);

  // Once the viewport stops, the budget goes to grid tiles at the exact zoom scale. Whether one
  // tile is kept for the coarse fallback levels is a policy switch, so only the total is fixed.
  displayList->render(surface.get());
  for (int i = 0; i < 100 && displayList->hasContentChanged(); ++i) {
    displayList->render(surface.get());
  }
  EXPECT_FALSE(displayList->hasContentChanged());
  EXPECT_EQ(ReadPixel(surface, 10, 1000), OPAQUE_RED);
}
namespace {
// Sets the negligible blur switch for its lifetime and restores the previous value afterwards, so
// an early return cannot leak the setting into later tests.
class SkipNegligibleBlurScope {
 public:
  explicit SkipNegligibleBlurScope(bool enabled)
      : previous(BackgroundHandler::IsSkippingNegligibleBlur()) {
    BackgroundHandler::SetSkippingNegligibleBlur(enabled);
  }

  ~SkipNegligibleBlurScope() {
    BackgroundHandler::SetSkippingNegligibleBlur(previous);
  }

 private:
  bool previous = true;
};

struct SkipRun {
  std::vector<uint8_t> pixels = {};
};

// Renders `count` small panels with a background blur over a striped backdrop at the given zoom.
// The first render of a fresh display list is measured, after an identical throwaway render has
// filled the texture pools.
SkipRun RenderPanels(Context* context, float zoom, float sigma, int count, bool skip,
                     float stripePeriod = 16.0f, float origin = 150.0f) {
  SkipNegligibleBlurScope skipScope(skip);
  auto size = 512;
  auto surface = Surface::Make(context, size, size);
  SkipRun run = {};
  for (int pass = 0; pass < 2; ++pass) {
    auto displayList = std::make_unique<DisplayList>();
    displayList->setRenderMode(RenderMode::Tiled);
    displayList->setTileUpdateMode(TileUpdateMode::Smooth);
    displayList->setBackgroundColor(Color::Black());
    auto root = displayList->root();
    root->addChild(MakeSolid(8192, 8192, Color::Black()));
    for (int i = 0; i < static_cast<int>(8192.0f / stripePeriod); ++i) {
      auto stripe = MakeSolid(stripePeriod / 2, 8192, Color::White());
      stripe->setMatrix(Matrix::MakeTrans(static_cast<float>(i) * stripePeriod, 0));
      root->addChild(stripe);
    }
    auto makePanel = [&](float x, float y, float w, float h, float blur) {
      auto shape = ShapeLayer::Make();
      Path path = {};
      path.addRect(Rect::MakeWH(w, h));
      shape->setPath(path);
      shape->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 0, 2)));
      shape->setMatrix(Matrix::MakeTrans(x, y));
      shape->setLayerStyles({BackgroundBlurStyle::Make(blur, blur)});
      auto group = Layer::Make();
      group->addChild(shape);
      return group;
    };
    for (int i = 0; i < count; ++i) {
      root->addChild(makePanel(origin + static_cast<float>((i % 10) * 160),
                               origin + static_cast<float>((i / 10) * 160), 100, 100, sigma));
    }
    displayList->setZoomScale(zoom);
    displayList->render(surface.get());
    context->flushAndSubmit();
  }
  auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
  run.pixels.resize(static_cast<size_t>(size * size * 4));
  EXPECT_TRUE(surface->readPixels(info, run.pixels.data(), 0, 0));
  return run;
}

int MaxDifference(const SkipRun& a, const SkipRun& b) {
  int maxDiff = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i) {
    maxDiff =
        std::max(maxDiff, std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i])));
  }
  return maxDiff;
}
}  // namespace

TGFX_TEST(TilePoolTest, SkippingNegligibleBackgroundBlurKeepsThePicture) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  // sigma 4 at zoom 0.05 is 0.2 pixels of the tile, below the 0.4 threshold.
  auto on = RenderPanels(context, 0.05f, 4.0f, 40, true);
  auto off = RenderPanels(context, 0.05f, 4.0f, 40, false);
  // Every style in the frame is below the threshold, so the capture is elided; the picture changes by
  // at most a few levels of 255. The fine stripes average out at this zoom, so even a lost blur
  // stays within that bound; the pixel boundary test below is the strict check.
  EXPECT_LE(MaxDifference(on, off), 4);
}

// With hard edges in the backdrop and panels sitting on whole pixels, nothing but the blur differs.
// A panel on a half pixel also changes by the sub-pixel shift the snapshot picked up, which the
// shortcut removes together with the blur.
TGFX_TEST(TilePoolTest, SkippingNegligibleBackgroundBlurIsExactOnPixelBoundaries) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  // Panels at multiples of 160 world units are whole pixels at zoom 0.05.
  auto on = RenderPanels(context, 0.05f, 4.0f, 40, true, 400.0f, 160.0f);
  auto off = RenderPanels(context, 0.05f, 4.0f, 40, false, 400.0f, 160.0f);
  EXPECT_EQ(MaxDifference(on, off), 0);
}

TGFX_TEST(TilePoolTest, BackgroundBlurAboveTheThresholdIsNotSkipped) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  // sigma 16 at zoom 0.05 is 0.8 pixels of the tile: it must be drawn whether or not skipping is on.
  // The hard-edged backdrop makes the blur visible, so a blur lost on both paths is caught too.
  auto on = RenderPanels(context, 0.05f, 16.0f, 40, true, 400.0f, 160.0f);
  auto off = RenderPanels(context, 0.05f, 16.0f, 40, false, 400.0f, 160.0f);
  auto none = RenderPanels(context, 0.05f, 0.0f, 40, false, 400.0f, 160.0f);
  EXPECT_EQ(MaxDifference(on, off), 0);
  EXPECT_GE(MaxDifference(off, none), 32);
}

TGFX_TEST(TilePoolTest, BackgroundBlurAtTheThresholdBoundary) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto style = BackgroundBlurStyle::Make(6.0f, 2.0f);
  // The larger of the two blurs decides: 6 * 0.0666 = 0.3996 is still negligible, a hair more
  // is not.
  EXPECT_TRUE(BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), 0.0666f));
  EXPECT_FALSE(BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), 0.0667f));
  EXPECT_FALSE(BackgroundHandler::IsNegligibleBackgroundStyle(nullptr, 0.0666f));
  // A style without any blur keeps the path it always had.
  auto none = BackgroundBlurStyle::Make(0.0f, 0.0f);
  EXPECT_FALSE(BackgroundHandler::IsNegligibleBackgroundStyle(none.get(), 0.05f));
  SkipNegligibleBlurScope disabled(false);
  EXPECT_FALSE(BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), 0.0666f));
}

// The root is drawn without its own alpha, visibility or matrix, so setting them must not make
// the capture skip the background styles below it.
TGFX_TEST(TilePoolTest, RootPropertiesDoNotSkipBackgroundCapture) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto render = [&](float rootAlpha, bool rootVisible, float rootScale) {
    auto surface = Surface::Make(context, 256, 256);
    auto displayList = std::make_unique<DisplayList>();
    displayList->setRenderMode(RenderMode::Tiled);
    displayList->setBackgroundColor(Color::Black());
    auto root = displayList->root();
    for (int i = 0; i < 13; ++i) {
      auto stripe = MakeSolid(10, 256, Color::White());
      stripe->setMatrix(Matrix::MakeTrans(static_cast<float>(i * 20), 0));
      root->addChild(stripe);
    }
    auto panel = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(48, 48, 160, 160));
    panel->setPath(path);
    panel->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 0, 40)));
    panel->setLayerStyles({BackgroundBlurStyle::Make(8, 8)});
    root->addChild(panel);
    root->setAlpha(rootAlpha);
    root->setVisible(rootVisible);
    root->setMatrix(Matrix::MakeScale(rootScale));
    displayList->render(surface.get());
    SkipRun run = {};
    auto info = ImageInfo::Make(256, 256, ColorType::RGBA_8888, AlphaType::Premultiplied);
    run.pixels.resize(static_cast<size_t>(256 * 256 * 4));
    EXPECT_TRUE(surface->readPixels(info, run.pixels.data(), 0, 0));
    return run;
  };
  auto normal = render(1.0f, true, 1.0f);
  EXPECT_EQ(MaxDifference(normal, render(0.0f, true, 1.0f)), 0);
  EXPECT_EQ(MaxDifference(normal, render(1.0f, false, 1.0f)), 0);
  EXPECT_EQ(MaxDifference(normal, render(1.0f, true, 0.01f)), 0);
}

// A layer with group opacity draws into an offscreen seeded from the tile atlas. The seed has to
// line up with the content drawn on top of it, or a blend that reads the destination shifts the
// edge of a tile whose boundary falls between pixels.
TGFX_TEST(TilePoolTest, PassThroughSeedMatchesDirectRendering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto oldGroupOpacity = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);
  auto render = [&](RenderMode mode) {
    auto surface = Surface::Make(context, 300, 300);
    auto displayList = std::make_unique<DisplayList>();
    auto background = MakeSolid(300, 300, Color::FromRGBA(0, 100, 200, 255));
    displayList->root()->addChild(background);
    auto container = Layer::Make();
    container->setAlpha(0.9f);
    auto child = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(75, 75, 150, 150));
    child->setPath(path);
    child->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 200)));
    child->setBlendMode(BlendMode::Exclusion);
    container->addChild(child);
    displayList->root()->addChild(container);
    displayList->setRenderMode(mode);
    if (mode == RenderMode::Tiled) {
      displayList->render(surface.get());
    }
    // At zoom 1.5 the tile boundaries fall between pixels.
    displayList->setZoomScale(1.5f);
    displayList->setContentOffset(-100, -100);
    displayList->render(surface.get());
    SkipRun run = {};
    auto info = ImageInfo::Make(300, 300, ColorType::RGBA_8888, AlphaType::Premultiplied);
    run.pixels.resize(300u * 300u * 4u);
    EXPECT_TRUE(surface->readPixels(info, run.pixels.data(), 0, 0));
    return run;
  };
  auto tiled = render(RenderMode::Tiled);
  auto direct = render(RenderMode::Direct);
  Layer::SetDefaultAllowsGroupOpacity(oldGroupOpacity);
  EXPECT_EQ(MaxDifference(tiled, direct), 0);
}

}  // namespace tgfx