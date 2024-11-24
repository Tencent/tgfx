// js/main.ts

import ElementManager from './models/ElementManager.js';
import BackendManager from './models/BackendManager.js';
import UIManager from './views/UIManager.js';
import EventManager from './controllers/EventManager.js';
import Figma from '../wasm/figma.js'; // 导入 FigmaRenderer
import {TGFXBind} from '../../lib/tgfx.js';
import * as types from '../../types/types.d.js';


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
    figmaRenderer: any; // 修改类型为 any 或者适当的类型

    constructor(figma: any) { // 接受 FigmaModule 作为参数
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

        this.elementManager = new ElementManager(this.svgCanvas);
        this.uiManager = new UIManager(this.propertiesPanel, this.layersList, (element, attr, value) => {
            this.eventManager.handlePropertyChange(element, attr, value);
        });
        // 实例化 FigmaRenderer
        this.figmaRenderer = new figma.FigmaRenderer();
        this.figmaRenderer.initialize('#realCanvas');


        this.backendManager = new BackendManager(this.figmaRenderer);
        this.eventManager = new EventManager(this.elementManager, this.uiManager, this.backendManager, this.svgCanvas);

        // 监听窗口大小变化
        window.onresize = () => {
            this.figmaRenderer.invalisize();
        };

        // 初始化
        this.init();
        this.initViewBox(); /* 初始化 viewBox */

        // 初始化自定义事件监听
        this.initEventListeners();

        this.initFont().then(r => this.figmaRenderer.updateShape());
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

        this.updateShapeCount(); // 初始化图形数量显示

        // FPS 计算和显示
        const fpsCounter = document.getElementById('fpsCounter');
        let lastFrameTime = performance.now();
        let frameCount = 0;

        function updateFPS() {
            const now = performance.now();
            frameCount++;
            const delta = now - lastFrameTime;
            if (delta >= 1000) {
                const fps = (frameCount / delta) * 1000;
                if (fpsCounter) {
                    fpsCounter.textContent = `FPS: ${Math.round(fps)}`;
                }
                frameCount = 0;
                lastFrameTime = now;
            }
            requestAnimationFrame(updateFPS);
        }

        requestAnimationFrame(updateFPS);
    }

    /**
     * 初始化自定义事件监听
     */
    initEventListeners(): void {
        // 监听图形添加事件
        document.addEventListener('shapeAdded', () => {
            this.updateShapeCount();
        });
    }

    /**
     * 更新图形数量显示
     */
    updateShapeCount(): void {
        const count = this.elementManager.getElements().length;
        this.shapeCounter.textContent = `图形数量: ${count}`;
    }

    /**
     * 初始化 SVG 的 viewBox 属性
     */
    initViewBox(): void {
        this.svgCanvas.setAttribute('viewBox', `0 0 ${this.eventManager.viewBox.width} ${this.eventManager.viewBox.height}`);
    }
}

// 启动应用
document.addEventListener('DOMContentLoaded', async () => {
    // 加载 WebAssembly 模块
    // const figma = await Figma();

    // 绑定 TGFX
    const figma = await Figma({
        locateFile: (file: string) => './wasm/' + file
    }).then((module: any) => {
        TGFXBind(module);
        return module;
    }).catch((error: any) => {
        console.error(error);
        throw new Error("TGFX init failed. Please check the .wasm file path!.");
    });

    // 实例化应用并传入 FigmaModule
    new App(figma);
});

// 确保文件被视为 ES 模块
export {};