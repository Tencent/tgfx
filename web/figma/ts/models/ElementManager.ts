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