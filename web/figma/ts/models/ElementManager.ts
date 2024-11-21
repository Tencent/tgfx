
// js/models/ElementManager.ts

import Rectangle from './Rectangle.js';
import Circle from './Circle.js';
import TextElement from './TextElement.js';
import BaseElement from './BaseElement.js';

export default class ElementManager {
    svgCanvas: SVGElement;
    elements: BaseElement[];
    selectedElement: BaseElement | null;

    constructor(svgCanvas: SVGElement) {
        this.svgCanvas = svgCanvas;
        this.elements = [];
        this.selectedElement = null;
    }

    /**
     * 创建并添加元素
     * @param type - 元素类型
     * @returns 创建的元素或null
     */
    createElement(type: string): BaseElement | null {
        let element: BaseElement | null = null;
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
     * @param element - 要选择的元素
     */
    selectElement(element: BaseElement | null): void {
        if (this.selectedElement) {
            this.selectedElement.setAttribute('stroke', '');
            this.selectedElement.setAttribute('stroke-width', '');
        }
        this.selectedElement = element;
        if (element) {
            element.setAttribute('stroke', '#007bff');
            element.setAttribute('stroke-width', '2');
        }
    }

    /**
     * 获取元素列表
     * @returns 元素列表
     */
    getElements(): BaseElement[] {
        return this.elements;
    }

    /**
     * 根据ID获取元素
     * @param id - 元素ID
     * @returns 元素或null
     */
    getElementById(id: string): BaseElement | null {
        return this.elements.find(el => el.id === id) || null;
    }

    /**
     * 设置所有元素的可见性
     * @param isVisible - 是否可见
     */
    setAllElementsVisibility(isVisible: boolean): void {
        this.elements.forEach(el => {
            el.element.style.opacity = isVisible ? '1' : '0';
        });
    }

    /**
     * 渲染所有元素到前端
     */
    renderAllElements(): void {
        this.svgCanvas.innerHTML = '';
        this.elements.forEach(el => {
            el.render(this.svgCanvas);
            el.element.style.opacity = '1';
        });
    }
}