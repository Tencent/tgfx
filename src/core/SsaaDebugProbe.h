// [SSAA-DBG] Shared probe for measuring Canvas ops issued inside DisplayList's SSAA drawRootLayer
// window. Owned by DisplayList.cpp; read from Canvas.cpp. Not part of any public API and only
// intended for local performance investigations.

#pragma once

#include <cstdint>
#include <string>

namespace tgfx {
namespace ssaa_debug {

struct Probe {
  bool active = false;

  int saveLayerCount = 0;
  int drawImageCount = 0;
  int drawImageFilterCount = 0;

  // dst pixels are pre-mapped through the current canvas matrix so they express the actual
  // super-sampled fragment coverage (SSAA tile is 2x scale, so a 100x100 world rect covers
  // roughly 4x that in SSAA pixels).
  int64_t drawImageSrcPixels = 0;
  int64_t drawImageDstPixels = 0;

  // Subtree cache instrumentation (populated from Layer::drawWithSubtreeCache).
  // - subtreeCacheEligible: canUseSubtreeCache returned true (layer is a caching candidate)
  // - subtreeCacheHit:     getValidSubtreeCache returned non-null AND hasCache was true
  // - subtreeCacheBuilt:   getValidSubtreeCache built a fresh cache image this call
  // - subtreeCacheOversized: rejected because longEdge > subtreeCacheMaxSize
  // - subtreeCacheDisabled: rejected due to RenderFlags::DisableCache path
  // - subtreeCacheMissOther: any other reason getValidSubtreeCache returned null
  int subtreeCacheEligible = 0;
  int subtreeCacheHit = 0;
  int subtreeCacheBuilt = 0;
  int subtreeCacheOversized = 0;
  int subtreeCacheDisabled = 0;
  int subtreeCacheMissOther = 0;

  // GPU-synced timings for the subtree cache path. Populated only when active is true so the
  // extra flushAndSubmit(true) barriers used to measure real GPU cost don't perturb release
  // builds. Segment mapping (see .codebuddy/knowledge/ssaa-subtree-cache-refactor.md):
  //   segment 1a = subtreeCacheBuildUs  (createSubtreeCacheImage: build 2x cache image)
  //   segment 1b = subtreeCacheDrawHitUs (SubtreeCache::draw: sample cached image into tile)
  //   segment 3  = ssaaTileDrawUs - subtreeCacheBuildUs - subtreeCacheDrawHitUs
  //                (direct-draw of non-cached layers at 2x, i.e. 4x fragment shader cost)
  int64_t subtreeCacheBuildUs = 0;
  int64_t subtreeCacheDrawHitUs = 0;
  int subtreeCacheBuildCount = 0;
  int subtreeCacheDrawHitCount = 0;

  // Oversized bucket distribution (Round 2, sizing analysis). Buckets are keyed on longEdge
  // in physical pixels (already includes SSAA 2x). They answer "how many layers would come
  // back into cache if we bumped subtreeCacheMaxSize to 3072 / 4096 / higher".
  //   le2048   : layers whose longEdge <= 2048 but were still rejected (should be 0; sanity)
  //   2k_3k    : (2048, 3072]  — would be saved by a 1.5x bump
  //   3k_4k    : (3072, 4096]  — would be saved by a 2x bump (full A1)
  //   4k_6k    : (4096, 6144]  — needs 3x
  //   6k_plus  : > 6144         — unrecoverable, always fallback
  int oversizedBucket_le2048 = 0;
  int oversizedBucket_2k_3k = 0;
  int oversizedBucket_3k_4k = 0;
  int oversizedBucket_4k_6k = 0;
  int oversizedBucket_6k_plus = 0;
  int oversizedMinLongEdge = 0;
  int oversizedMaxLongEdge = 0;

  // Fallback direct-draw timing (Round 2). When drawWithSubtreeCache returns false, the layer
  // still needs to be drawn — via drawOffscreen or drawDirectly at the outer draw() call site.
  // We split these into two buckets keyed on the reason getValidSubtreeCache returned nullptr:
  //   oversizedFallback : the layer was a cache candidate but longEdge > effectiveMaxSize
  //   otherFallback     : Disabled by renderFlags, or MissOther (zero-size / build failure)
  // Ineligible layers (canUseSubtreeCache=false) are NOT counted here — they never touched the
  // subtree cache path in the first place, so their time is in seg3_direct baseline.
  enum class LastMissReason : unsigned char {
    None,
    Oversized,
    Disabled,
    MissOther,
  };
  LastMissReason lastMissReason = LastMissReason::None;
  int64_t oversizedFallbackDrawUs = 0;
  int oversizedFallbackDrawCount = 0;
  int64_t otherFallbackDrawUs = 0;
  int otherFallbackDrawCount = 0;

  // [SSAA-DBG] Split of otherFallback by call-site context. otherFallback is Layer::draw's
  // fallback direct-draw chrono; it can be invoked from two distinct call sites during an SSAA
  // tile:
  //   inRec  : nested inside createSubtreeCacheImage's PictureRecorder pass. Time here is CPU
  //            op-recording only (no real GPU work); wall-clock overlaps with seg1a_build, so
  //            adding otherFallback and seg1a together would double-count.
  //   direct : tile's main draw chain, i.e. a parent layer that itself was not cacheable
  //            recurses to children who then hit stc_dis. Time here is independent of seg1a
  //            and is what actually contributes to the tile GPU cost.
  // Enter/exit is tracked via cacheImageBuildDepth incremented around every
  // createSubtreeCacheImage chrono window. Depth is used (not bool) because a cache-build's
  // recording can in principle itself recurse (parent A recording child B who also builds).
  int cacheImageBuildDepth = 0;
  int64_t otherFallbackInRecUs = 0;
  int otherFallbackInRecCount = 0;
  int64_t otherFallbackDirectUs = 0;
  int otherFallbackDirectCount = 0;

  // [SSAA-DBG] Round 6: SubtreeCache key/entry sanity probes. Answers "is the 5640 vs 138
  // builds gap driven by key instability (mipmap band jitter, scale change) or by GPU proxy
  // eviction (LRU purge under memory pressure)?"
  //   hasCacheKeyMiss   : hasCache() -> cacheEntries.find() == end. Either first-time build
  //                       OR the (longEdge, divisor) key changed since last frame (key jitter).
  //   hasCacheProxyMiss : hasCache() -> entry exists, but proxyProvider lost the TextureProxy
  //                       (GPU LRU purge). Cache metadata alive, texture gone.
  //   builtSecondEntry  : addCache() when the SubtreeCache instance already has >=1 entry.
  //                       Direct evidence that the same layer produced a different key this
  //                       time — the smoking gun for "key jitter causing rebuilds".
  //   builtEntriesSizeSum : each addCache accumulates the resulting cacheEntries.size() so
  //                       (sum / built_count) is the average number of distinct keys per
  //                       cached layer. Value >>1 = key jitter dominates.
  int hasCacheKeyMiss = 0;
  int hasCacheProxyMiss = 0;
  int builtSecondEntry = 0;
  int64_t builtEntriesSizeSum = 0;

  // [SSAA-DBG] Round 7: quantify subtree cache invalidation churn. Answers "is the hit/built
  // ratio stuck at 1.2 because parent/root caches are getting invalidated before they can be
  // reused?"
  //   subtreeInvalidated : (DEPRECATED — moved to gInvalidateCounter, kept for compat with
  //                        the LOGI format; will always be 0 here since Layer sets the
  //                        global counter instead of this field.)
  //   builtFromEmpty     : addCache() where cacheEntries.size() == 1 AFTER insertion, meaning
  //                        this is the very first entry in a freshly-constructed SubtreeCache.
  //                        Either the layer was never cached before (natural first build) OR
  //                        it was invalidated and is being rebuilt from scratch. When
  //                        (builtFromEmpty >> "expected new layers per replay"), invalidation
  //                        churn dominates.
  int subtreeInvalidated = 0;
  int builtFromEmpty = 0;

  // [SSAA-DBG] Round 4: physical edge histogram of successfully built cache images. Buckets
  // are physical longEdge (already 2x SSAA). Tells us where currently-cached layers cluster,
  // useful when reasoning about size-based cache admission strategies.
  int builtEdge_le64 = 0;      // (0,64]
  int builtEdge_64_128 = 0;    // (64,128]
  int builtEdge_128_256 = 0;   // (128,256]
  int builtEdge_256_512 = 0;   // (256,512]
  int builtEdge_512_1024 = 0;  // (512,1024]
  int builtEdge_1024_2048 = 0; // (1024,2048]
  int builtEdge_2048_plus = 0; // (2048, ...]
  int builtEdgeMin = 0;        // smallest physical edge of a successfully built cache
  int builtEdgeMax = 0;        // largest physical edge of a successfully built cache

  // [SSAA-DBG] Op-type distribution inside the SSAA drawRootLayer window. Split the "draw"
  // budget into rasterization primitives so we can see whether a slow frame is drown by many
  // paths, many rects, many text glyph runs, etc. Counters increment once per Canvas draw*
  // API call (not per fragment) so absolute numbers are op-count, not pixel cost.
  int opRect = 0;      // Canvas::drawRect (excluding degenerate stroke path fallback)
  int opRRect = 0;     // Canvas::drawRRect + drawOval + drawCircle + drawRoundRect
  int opPath = 0;      // Canvas::drawPath (non-empty)
  int opShape = 0;     // Canvas::drawShape (pre-built shape objects, e.g. StyledShape)
  int opText = 0;      // drawGlyphs / drawSimpleText / drawTextBlob
  int opMesh = 0;      // drawMesh (vertex fill, e.g. mesh warp)
  int opAtlas = 0;     // drawAtlas
  int opFill = 0;      // drawColor / drawPaint (fullscreen or clipped fills)
  int opLayer = 0;     // drawLayer (3D composite / picture playback with imageFilter)
  int opImage = 0;     // drawImage / drawImageRect (direct image fills; own DrawImageOp path,
                       // NOT going through drawRect). drawImageCount above counts the same
                       // events but is kept for source-pixel / dst-pixel bookkeeping.

  // [SSAA-DBG] Round 8: per-category GPU time. Only populated when gGpuSyncEnabled=true.
  // Category-boundary flush strategy: within one category the natural batching is preserved;
  // when a new op switches category we flushAndSubmit(true), attribute elapsed to the OLD
  // category, then start a fresh chrono for the new one. When op batches are clustered by
  // type this measures each cluster's true GPU cost; when types interleave heavily the
  // barrier overhead does inflate the numbers but relative ordering is still meaningful.
  // Categories 0..9 map to opRect/opRRect/opPath/opShape/opText/opMesh/opAtlas/opFill/opLayer/opImage.
  // -1 means "no active category" (either just entered active window or just flushed).
  enum OpCategory : signed char {
    OpCatNone = -1,
    OpCatRect = 0, OpCatRRect = 1, OpCatPath = 2, OpCatShape = 3,
    OpCatText = 4, OpCatMesh = 5, OpCatAtlas = 6, OpCatFill = 7, OpCatLayer = 8,
    OpCatImage = 9,
    OpCatCount = 10,
  };
  signed char currentOpCategory = OpCatNone;
  int64_t opCategoryT0Ns = 0;        // chrono time_point of current category's start
  int64_t opGpuUs[OpCatCount] = {};  // accumulated per-category GPU us
  int opGpuFlushCount = 0;           // number of category-switch barriers taken this window

  // [SSAA-DBG] Shader-type distribution of the brush actually feeding each draw op that goes
  // through Canvas::drawFill / drawPath / drawRect / drawRRect / drawShape. Counted at the
  // brush consumption point (RenderContext / OpsCompositor entry) so each op contributes at
  // most one shader classification. NULL shader (solid color from paint.color) increments
  // shaderNone.
  int shaderNone = 0;      // Brush has no shader; fill from paint.color only
  int shaderColor = 0;     // ColorShader (single color via shader wrapper)
  int shaderImage = 0;     // ImageShader (image fill / ImagePattern)
  int shaderGradient = 0;  // LinearGradient / RadialGradient / ConicGradient / DiamondGradient
  int shaderColorFilter = 0;   // ColorFilterShader
  int shaderMatrix = 0;    // MatrixShader (wrapping)
  int shaderBlend = 0;     // BlendShader
  int shaderPerlinNoise = 0;   // PerlinNoiseShader
  int shaderOther = 0;     // fallthrough
};

extern Probe gProbe;

// [SSAA-DBG] Round 7: invalidation churn counter — LIVES OUTSIDE Probe because
// Layer::invalidateSubtree() is called during property mutation (between renders) and Probe
// gets reset on every tile boundary via `gProbe = {}`. Keeping the counter global lets it
// accumulate across all invalidation events since the last DisplayList::render() flush.
// DisplayList aggregates and clears it once per SSAA-DBG log line.
extern int gInvalidateCounter;

// Bench replay tag written from outside (window::setBenchmarkTag). Printed by DisplayList's
// SSAA-DBG line so replay events can be aligned across SSAA on/off runs.
extern std::string gDebugTag;

// [SSAA-DBG] Master switch for the GPU-synced timing barriers wrapped around segment 1a/1b/3/4.
// When false, all flushAndSubmit(true) barriers inside those measurement points collapse to
// no-ops so the release pipeline runs unmodified. Set from outside via Window::setMeasureGpu
// so this switch tracks the existing SetMeasureMode benchmark control (single source of truth).
//
// - false (default): probe counters (hit/built/oversized/buckets/...) still update; timing
//   fields (seg1a_build/seg1b_drawHit/oversizedFB/otherFB) become approximate CPU-record
//   time only, but ratios across pure-CPU segments remain comparable.
// - true: barriers fire, per-segment wall-clock reflects real GPU cost, pipeline serialized.
extern bool gGpuSyncEnabled;

}  // namespace ssaa_debug
}  // namespace tgfx

