// js/main.js

import ElementManager from './models/ElementManager.js';
import BackendManager from './models/BackendManager.js';
import UIManager from './views/UIManager.js';
import EventManager from './controllers/EventManager.js';

/**
 * 应用程序主类
 */
class App {
    constructor() {
        // 获取DOM元素
        this.svgCanvas = document.getElementById('canvas');
        this.propertiesPanel = document.getElementById('properties');
        this.layersList = document.getElementById('layersList');
        this.shapeCounter = document.getElementById('shapeCounter'); // 获取图形数量显示元素

        // 实例化管理器
        this.elementManager = new ElementManager(this.svgCanvas);
        this.uiManager = new UIManager(this.propertiesPanel, this.layersList, (element, attr, value) => {
            this.eventManager.handlePropertyChange(element, attr, value);
        });
        this.backendManager = new BackendManager((data) => this.handleBackendMessage(data));
        this.eventManager = new EventManager(this.elementManager, this.uiManager, this.backendManager, this.svgCanvas);

        // 初始化
        this.init();
        this.initViewBox(); /* 初始化 viewBox */

        // 初始化自定义事件监听
        this.initEventListeners();
    }

    /**
     * 初始化应用
     */
    init() {
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
                fpsCounter.textContent = `FPS: ${Math.round(fps)}`;
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
    initEventListeners() {
        // 监听图形添加事件
        document.addEventListener('shapeAdded', () => {
            this.updateShapeCount();
        });

        // 如有需要，可以监听其他自定义事件，例如图形更新
        // document.addEventListener('shapeUpdated', () => {
        //     this.updateShapeCount();
        // });
    }

    /**
     * 更新图形数量显示
     */
    updateShapeCount() {
        const count = this.elementManager.getElements().length;
        this.shapeCounter.textContent = `图形数量: ${count}`;
    }

    /**
     * 初始化 SVG 的 viewBox 属性
     */
    initViewBox() {
        this.svgCanvas.setAttribute('viewBox', `0 0 ${this.eventManager.viewBox.width} ${this.eventManager.viewBox.height}`);
    }

    /**
     * 处理来自后端的消息
     * @param {object} data 后端发送的数据
     */
    handleBackendMessage(data) {
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
     * @param {object} elementData 元素数据
     */
    handleBackendAdd(elementData) {
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
     * @param {object} updateData 更新数据
     */
    handleBackendUpdate(updateData) {
        const element = this.elementManager.getElementById(updateData.id);
        if (element) {
            element.setAttribute(updateData.attribute, updateData.value);
            this.uiManager.showTooltip('后端更新了元素', 50, 60);
        }
        this.updateShapeCount();
    }

    /**
     * 处理后端画布平移
     * @param {object} panData 平移数据
     */
    handleBackendCanvasPan(panData) {
        const { x, y } = panData;
        const viewBoxAttr = `${x} ${y} ${this.eventManager.viewBox.width} ${this.eventManager.viewBox.height}`;
        this.svgCanvas.setAttribute('viewBox', viewBoxAttr);
        this.eventManager.viewBox.x = x;
        this.eventManager.viewBox.y = y;
        this.uiManager.showTooltip('后端平移了画布', 50, 60);
    }
}

// 启动应用
document.addEventListener('DOMContentLoaded', () => {
    new App();
});