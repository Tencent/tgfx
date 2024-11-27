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
// js/main.ts

import ElementManager from './models/ElementManager.js';
import BackendManager from './models/BackendManager.js';
import UIManager from './views/UIManager.js';
import EventManager from './controllers/EventManager.js';
import {Metric} from "web-vitals";
import WebVitalsManager from "./models/WebVitalsManager";
import PerformanceManager from './models/PerformanceManager';


/**
 * 应用程序主类
 */
class App {
    svgCanvas: SVGElement;
    propertiesPanel: HTMLElement;
    layersList: HTMLElement;
    shapeCounter: HTMLElement;
    elementManager: ElementManager;
    uiManager: UIManager;
    backendManager: BackendManager;
    eventManager: EventManager;
    performanceManager: PerformanceManager; // 添加性能管理器
    figmaRenderer: any; // 修改类型为 any 或者适当的类型

    constructor() { // 移除 figma 参数
        self.addEventListener('error', (e) => {
            console.error('Worker Error:', e.message, e.filename, e.lineno);
        });
        // 获取DOM元素
        const svgCanvasElement = document.getElementById('canvas');

        if (!svgCanvasElement || !(svgCanvasElement instanceof SVGElement)) {
            throw new Error('无法找到具有id "canvas" 的SVG元素。');
        }

        const svgCanvas = svgCanvasElement;

        const propertiesPanel = document.getElementById('properties') as HTMLElement;
        const layersList = document.getElementById('layersList') as HTMLElement;
        const shapeCounter = document.getElementById('shapeCounter') as HTMLElement;

        // 实例化管理器
        this.svgCanvas = svgCanvas;
        this.propertiesPanel = propertiesPanel;
        this.layersList = layersList;
        this.shapeCounter = shapeCounter;

        // 监听 wasm 加载完成事件
        document.addEventListener('wasmLoaded', (event: CustomEvent) => {
            const figma = event.detail;
            this.initializeWASM(figma);
        });

    }

    initializeWASM(figma: any): void {
        this.figmaRenderer = new figma.FigmaRenderer();
        this.figmaRenderer.initialize('#realCanvas');
        this.initFont().then(r => console.log("font loaded"));


        this.elementManager = new ElementManager(this.svgCanvas);
        this.uiManager = new UIManager(this.propertiesPanel, this.layersList, (element, attr, value) => {
            this.eventManager.handlePropertyChange(element, attr, value);
        });

        this.backendManager = new BackendManager(this.figmaRenderer);
        // 计算 Interaction to Next Paint (INP) 的功能
        const INPCounter = document.getElementById('INPCounter');
        const handleINP = (metric: Metric) => {
            if (INPCounter) {
                INPCounter.textContent = `INP: ${metric.value} ms`;
            }
        };
        const vitalsManager = new WebVitalsManager(handleINP);
        this.performanceManager = new PerformanceManager(this.elementManager, this.backendManager); // 初始化性能管理器
        this.eventManager = new EventManager(this.elementManager, this.uiManager, this.backendManager, this.svgCanvas, vitalsManager);

        // 监听窗口大小变化
        window.onresize = () => {
            this.figmaRenderer.invalisize();
        };

        // 初始化
        this.init();
        this.initViewBox(); /* 初始化 viewBox */
    }

    async initFont(): Promise<void> {
        var emoji_font_path = "./assets/font/NotoSansSC-Regular.otf";
        const emoji_font_buffer = await fetch(emoji_font_path).then((response) => response.arrayBuffer());
        const emoji_font_array = new Uint8Array(emoji_font_buffer);
        this.figmaRenderer.registerFonts(emoji_font_array);
    }

    /**
     * 初始化应用
     */
    init(): void {
        // 绑定图层管理更新
        this.uiManager.updateLayersList(this.elementManager.getElements(), this.elementManager.selectedElement);

        // 移除原有的性能统计代码
    }

    /**
     * 初始化 SVG 的 viewBox 属性
     */
    initViewBox(): void {
        this.svgCanvas.setAttribute('viewBox', `0 0 ${this.eventManager.viewBox.width} ${this.eventManager.viewBox.height}`);
    }
}

// 启动应用
document.addEventListener('DOMContentLoaded', () => {
    new App();
});

// 确保文件被视为 ES 模块
export {};