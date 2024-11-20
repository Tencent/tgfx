// js/main.ts

import ElementManager from './models/ElementManager.js';
import BackendManager from './models/BackendManager.js';
import UIManager from './views/UIManager.js';
import EventManager from './controllers/EventManager.js';
import Figma from '../wasm/figma.js'; // 导入 FigmaRenderer

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
        this.backendManager = new BackendManager((data) => this.handleBackendMessage(data));
        this.eventManager = new EventManager(this.elementManager, this.uiManager, this.backendManager, this.svgCanvas);

        // 实例化 FigmaRenderer
        
        this.figmaRenderer = new figma.FigmaRenderer();
        // 定义 canvas 的 id，对应页面中的真实 canvas 元素
        const canvasId = '#realCanvas'; // 更新为实际的 canvas id
        this.figmaRenderer.initialize(canvasId); // 调用 initialize 方法

        // 初始化
        this.init();
        this.initViewBox(); /* 初始化 viewBox */

        // 初始化自定义事件监听
        this.initEventListeners();
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
            this.figmaRenderer.updateShape(); // 调用 updateShape 方法
        });

        // 如有需要，可以监听其他自定义事件，例如图形更新
        // document.addEventListener('shapeUpdated', () => {
        //     this.updateShapeCount();
        // });
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

    /**
     * 处理来自后端的消息
     * @param data - 后端发送的数据
     */
    handleBackendMessage(data: any): void {
        switch (data.action) {
            case 'add':
                this.handleBackendAdd(data.data);
                break;
            case 'update':
                this.handleBackendUpdate(data.data);
                break;
            case 'canvasPan':
                this.handleBackendCanvasPan(data.data);
                break;
            default:
                console.warn('未知的后端动作:', data.action);
        }

        this.updateShapeCount(); // 更新图形数量
    }

    /**
     * 处理后端添加元素
     * @param elementData - 元素数据
     */
    handleBackendAdd(elementData: any): void {
        let element;
        switch (elementData.tagName) {
            case 'rect':
                element = this.elementManager.createElement('rect');
                break;
            case 'circle':
                element = this.elementManager.createElement('circle');
                break;
            case 'text':
                element = this.elementManager.createElement('text');
                break;
            default:
                console.error('未知的元素类型:', elementData.tagName);
                return;
        }
        if (element) {
            element.fromData(elementData);
            this.uiManager.updateLayersList(this.elementManager.getElements(), this.elementManager.selectedElement);
            this.uiManager.showTooltip('后端添加了元素', 50, 60);
        }
        this.updateShapeCount();
    }

    /**
     * 处理后端更新元素
     * @param updateData - 更新数据
     */
    handleBackendUpdate(updateData: any): void {
        const element = this.elementManager.getElementById(updateData.id);
        if (element) {
            element.setAttribute(updateData.attribute, updateData.value);
            this.uiManager.showTooltip('后端更新了元素', 50, 60);
        }
        this.updateShapeCount();
    }

    /**
     * 处理后端画布平移
     * @param panData - 平移数据
     */
    handleBackendCanvasPan(panData: any): void {
        const { x, y } = panData;
        const viewBoxAttr = `${x} ${y} ${this.eventManager.viewBox.width} ${this.eventManager.viewBox.height}`;
        this.svgCanvas.setAttribute('viewBox', viewBoxAttr);
        this.eventManager.viewBox.x = x;
        this.eventManager.viewBox.y = y;
        this.uiManager.showTooltip('后端平移了画布', 50, 60);
    }
}

// 启动应用
document.addEventListener('DOMContentLoaded', async () => {
    // 加载 WebAssembly 模块
    const figma = await Figma();
    
    // 实例化应用并传入 FigmaModule
    new App(figma);
});


// 确保文件被视为 ES 模块
export { };