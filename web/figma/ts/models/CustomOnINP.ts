/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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


// 这个类是从 web-vitals 包中复制的，进行了一些修改，主要是为了提供一个resetINP方法，用于重置INP指标的处理；
// 这样方便控制统计不同时间段内的INP指标，而不是全局的。
// 如果升级了 web-vitals 包，需要对比新版本onINP.ts的代码，有修改要同步更新过来。

import { onBFCacheRestore } from '../../../node_modules/web-vitals/dist/modules/lib/bfcache.js';
import { bindReporter } from '../../../node_modules/web-vitals/dist/modules/lib/bindReporter.js';
import { initMetric } from '../../../node_modules/web-vitals/dist/modules/lib/initMetric.js';

import {
  DEFAULT_DURATION_THRESHOLD,
  processInteractionEntry,
  estimateP98LongestInteraction,
  resetInteractions,
} from '../../../node_modules/web-vitals/dist/modules/lib/interactions.js';
import { observe } from '../../../node_modules/web-vitals/dist/modules/lib/observe.js';
import { onHidden } from '../../../node_modules/web-vitals/dist/modules/lib/onHidden.js';
import { initInteractionCountPolyfill } from '../../../node_modules/web-vitals/dist/modules/lib/polyfills/interactionCountPolyfill.js';
import { whenActivated } from '../../../node_modules/web-vitals/dist/modules/lib/whenActivated.js';
import { whenIdle } from '../../../node_modules/web-vitals/dist/modules/lib/whenIdle.js';

import {INPMetric, MetricRatingThresholds, ReportOpts} from '../../../node_modules/web-vitals/src/types.js';

/** Thresholds for INP. See https://web.dev/articles/inp#what_is_a_good_inp_score */
export const INPThresholds: MetricRatingThresholds = [200, 500];

let metric: INPMetric;
let report: ReturnType<typeof bindReporter>;
let currentOnReport: (metric: INPMetric) => void;
let currentOpts: ReportOpts | undefined;

/**
 * Calculates the [INP](https://web.dev/articles/inp) value for the current
 * page and calls the `callback` function once the value is ready, along with
 * the `event` performance entries reported for that interaction. The reported
 * value is a `DOMHighResTimeStamp`.
 *
 * A custom `durationThreshold` configuration option can optionally be passed to
 * control what `event-timing` entries are considered for INP reporting. The
 * default threshold is `40`, which means INP scores of less than 40 are
 * reported as 0. Note that this will not affect your 75th percentile INP value
 * unless that value is also less than 40 (well below the recommended
 * [good](https://web.dev/articles/inp#what_is_a_good_inp_score) threshold).
 *
 * If the `reportAllChanges` configuration option is set to `true`, the
 * `callback` function will be called as soon as the value is initially
 * determined as well as any time the value changes throughout the page
 * lifespan.
 *
 * _**Important:** INP should be continually monitored for changes throughout
 * the entire lifespan of a page—including if the user returns to the page after
 * it's been hidden/backgrounded. However, since browsers often [will not fire
 * additional callbacks once the user has backgrounded a
 * page](https://developer.chrome.com/blog/page-lifecycle-api/#advice-hidden),
 * `callback` is always called when the page's visibility state changes to
 * hidden. As a result, the `callback` function might be called multiple times
 * during the same page load._
 */
export const customOnINP = (
  onReport: (metric: INPMetric) => void,
  opts?: ReportOpts,
) => {
  // Return if the browser doesn't support all APIs needed to measure INP.
  if (
    !(
      'PerformanceEventTiming' in self &&
      'interactionId' in PerformanceEventTiming.prototype
    )
  ) {
    return;
  }

  // Set defaults
  opts = opts || {};
  
  currentOnReport = onReport;
  currentOpts = opts;

  whenActivated(() => {
    // TODO(philipwalton): remove once the polyfill is no longer needed.
    initInteractionCountPolyfill();

    metric = initMetric('INP');
    report = bindReporter(
      currentOnReport,
      metric,
      INPThresholds,
      currentOpts!.reportAllChanges,
    );

    const handleEntries = (entries: INPMetric['entries']) => {
      // Queue the `handleEntries()` callback in the next idle task.
      // This is needed to increase the chances that all event entries that
      // occurred between the user interaction and the next paint
      // have been dispatched. Note: there is currently an experiment
      // running in Chrome (EventTimingKeypressAndCompositionInteractionId)
      // 123+ that if rolled out fully may make this no longer necessary.
      whenIdle(() => {
        entries.forEach(processInteractionEntry);

        const inp = estimateP98LongestInteraction();

        if (inp && inp.latency !== metric.value) {
          metric.value = inp.latency;
          metric.entries = inp.entries;
          report();
        }
      });
    };

    const po = observe('event', handleEntries, {
      // Event Timing entries have their durations rounded to the nearest 8ms,
      // so a duration of 40ms would be any event that spans 2.5 or more frames
      // at 60Hz. This threshold is chosen to strike a balance between usefulness
      // and performance. Running this callback for any interaction that spans
      // just one or two frames is likely not worth the insight that could be
      // gained.
      durationThreshold: opts!.durationThreshold ?? DEFAULT_DURATION_THRESHOLD,
    });

    if (po) {
      // Also observe entries of type `first-input`. This is useful in cases
      // where the first interaction is less than the `durationThreshold`.
      po.observe({type: 'first-input', buffered: true});

      onHidden(() => {
        handleEntries(po.takeRecords() as INPMetric['entries']);
        report(true);
      });

      // Only report after a bfcache restore if the `PerformanceObserver`
      // successfully registered.
      onBFCacheRestore(() => {
        resetInteractions();
        metric = initMetric('INP');
        report = bindReporter(
          currentOnReport,
          metric,
          INPThresholds,
          currentOpts!.reportAllChanges,
        );
      });
    }
  });
};

export const resetINP = () => {
  resetInteractions();
  metric = initMetric('INP');
  report = bindReporter(
    currentOnReport,
    metric,
    INPThresholds,
    currentOpts!.reportAllChanges,
  );
};
