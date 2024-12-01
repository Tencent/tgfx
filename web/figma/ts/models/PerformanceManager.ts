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
import ElementManager from './ElementManager.js';
import BackendManager from './BackendManager.js';
import {settings} from "../config";

export default class PerformanceManager {
    private readonly fpsCounter: HTMLElement | null;
    private readonly renderCounter: HTMLElement | null;
    private readonly shapeCounter: HTMLElement | null;
    private lastFrameTime: number;
    private frameCount: number;
    private elementManager: ElementManager;
    private backendManager: BackendManager;

    constructor(elementManager: ElementManager, backendManager: BackendManager) {
        this.fpsCounter = document.getElementById('fpsCounter');
        this.renderCounter = document.getElementById('renderCounter');
        this.shapeCounter = document.getElementById('shapeCounter') as HTMLElement;

        this.lastFrameTime = performance.now();
        this.frameCount = 0;
        this.elementManager = elementManager;
        this.backendManager = backendManager;
        this.updatePerformanceInfo = this.updatePerformanceInfo.bind(this);
        requestAnimationFrame(this.updatePerformanceInfo);
    }


    private updatePerformanceInfo(): void {
        const { fps, frameTime } = this.calculateFPS();
        if (fps > 0) {
            this.updateUI(fps, frameTime);
        }

        requestAnimationFrame(this.updatePerformanceInfo);
    }

    private calculateFPS(): { fps: number; frameTime: number } {
        const now = performance.now();
        this.frameCount++;
        const delta = now - this.lastFrameTime;
        if (delta >= 1000) {
            const fps = (this.frameCount / delta) * 1000;
            const frameTime = delta / this.frameCount;
            this.frameCount = 0;
            this.lastFrameTime = now;
            return { fps: Math.round(fps), frameTime };
        }
        return { fps: 0, frameTime: 0 };
    }

    private updateUI(fps: number, frameTime: number): void {
        if (this.fpsCounter) {
            const backendFps = this.backendManager.fps();
            if (backendFps > 0) {
                fps = backendFps;
            }
            this.fpsCounter.textContent = `FPS: ${fps}`;
        }

        if (this.shapeCounter) {
            const count = this.elementManager.getElements().length;
            this.shapeCounter.textContent = `图形数量: ${count}`;
        }

        if (this.renderCounter) {
            if (settings.isBackend) {
                // 使用后端渲染时，显示后端渲染耗时
                frameTime = this.backendManager.frameTimeCons();
            }
            this.renderCounter.textContent = `单帧耗时: ${frameTime.toFixed(2)} ms`;
        }
    }

}