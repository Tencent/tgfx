// js/models/ElementManager.js

import Rectangle from './Rectangle.js';
import Circle from './Circle.js';
import TextElement from './TextElement.js';

export default class ElementManager {
    constructor(svgCanvas) {
        this.svgCanvas = svgCanvas;
        this.elements = [];
        this.selectedElement = null;
    }

    /**
     * 创建并添加元素
     * @param {string} type 元素类型
     * @returns {BaseElement|null} 创建的元素或null
     */
    createElement(type) {
        let element;
        switch (type) {
            case 'rect':
                element = new Rectangle();
                break;
            case 'circle':
                element = new Circle();
                break;
            case 'text':
                element = new TextElement();
                break;
            default:
                console.error('未知的元素类型:', type);
                return null;
        }
        element.render(this.svgCanvas);
        this.elements.push(element);
        return element;
    }

    /**
     * 选择元素
     * @param {BaseElement} element 要选择的元素
     */
    selectElement(element) {
        if (this.selectedElement) {
            this.selectedElement.setAttribute('stroke', null);
            this.selectedElement.setAttribute('stroke-width', null);
        }
        this.selectedElement = element;
        if (element) {
            element.setAttribute('stroke', '#007bff');
            element.setAttribute('stroke-width', 2);
        }
    }

    /**
     * 获取元素列表
     * @returns {Array<BaseElement>} 元素列表
     */
    getElements() {
        return this.elements;
    }

    /**
     * 根据ID获取元素
     * @param {string} id 元素ID
     * @returns {BaseElement|null} 元素或null
     */
    getElementById(id) {
        return this.elements.find(el => el.id === id) || null;
    }

    /**
     * 设置所有元素的可见性
     * @param {boolean} isVisible 是否可见
     */
    setAllElementsVisibility(isVisible) {
        this.elements.forEach(el => {
            el.element.style.opacity = isVisible ? '1' : '0';
        });
    }

    /**
     * 渲染所有元素到前端
     */
    renderAllElements() {
        this.svgCanvas.innerHTML = '';
        this.elements.forEach(el => {
            el.render(this.svgCanvas);
            el.element.style.opacity = '1';
        });
    }

    /**
     * 从数据加载元素
     * @param {Array<object>} dataList 元素数据列表
     */
    loadFromData(dataList) {
        this.elements = [];
        dataList.forEach(data => {
            let element;
            switch (data.tagName) {
                case 'rect':
                    element = new Rectangle();
                    break;
                case 'circle':
                    element = new Circle();
                    break;
                case 'text':
                    element = new TextElement();
                    break;
                default:
                    console.error('未知的元素类型:', data.tagName);
                    return;
            }
            element.fromData(data);
            element.render(this.svgCanvas);
            this.elements.push(element);
        });
    }
}