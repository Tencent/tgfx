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

import {customOnINP, resetINP} from './CustomOnINP.js';
import {Metric} from "web-vitals";

/**
 * WebVitalsManager 类用于管理 web-vitals 的指标回调，特别是 INP 指标。
 */
export default class WebVitalsManager {
    private readonly inpCallback: (metric: Metric) => void;

    /**
     * 构造函数
     * @param callback 处理 INP 指标的回调函数
     */
    constructor(callback: (metric: Metric) => void) {
        this.inpCallback = callback;
        this.init();
    }

    private init() {
        customOnINP((metric) => {
            console.log(`INP: ${metric.value}, Delta: ${metric.delta}, Entries count: ${metric.entries.length}`);
            this.inpCallback(metric);
        }, {reportAllChanges: true, durationThreshold: 0});
    }

    /**
     * 重置 INP 指标的处理
     * 通过停止当前的回调处理，并允许重新启动
     */
    public reset() {
        console.log('Resetting INP metrics');
        resetINP();
    }
}