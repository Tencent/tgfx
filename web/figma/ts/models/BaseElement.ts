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
// js/models/BaseElement.ts

export default class BaseElement {
    type: string;
    id: string;
    element: SVGElement;

    /**
     * 创建基础图形元素
     * @param type - 元素类型（rect, circle, text）
     */
    constructor(type: string) {
        this.type = type;
        this.id = `el_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
        this.element = document.createElementNS("http://www.w3.org/2000/svg", type);
        this.element.setAttribute('id', this.id);
        this.element.classList.add('element');
        this.element.setAttribute('filter', 'blur(5px)');
    }

    /**
     * 获取元素的属性对象
     * @returns 属性键值对
     */
    getAttributes(): { [key: string]: string } {
        const attrs: { [key: string]: string } = {};
        const attributes = Array.from(this.element.attributes);
        for (let attr of attributes) {
            attrs[attr.name] = attr.value;
        }
        return attrs;
    }

    /**
     * 设置元素的属性
     * @param name - 属性名称
     * @param value - 属性值
     */
    setAttribute(name: string, value: string): void {
        this.element.setAttribute(name, value);
    }

    /**
     * 获取元素的某个属性
     * @param name - 属性名称
     * @returns 属性值
     */
    getAttribute(name: string): string | null {
        return this.element.getAttribute(name);
    }

    /**
     * 设置文本内容
     * @param text - 文本内容
     */
    setText(text: string): void {
        // 默认实现为空
    }

    /**
     * 获取文本内容
     * @returns 文本内容
     */
    getText(): string | null {
        return null;
    }

    /**
     * 渲染到指定的SVG画布
     * @param svg - 画布元素
     */
    render(svg: SVGElement): void {
        svg.appendChild(this.element);
    }

}