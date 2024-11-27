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
// js/views/UIManager.ts

import BaseElement from '../models/BaseElement.js';

export default class UIManager {
    propertiesPanel: HTMLElement;
    layersList: HTMLElement;
    onPropertyChange: (element: BaseElement, attr: string, value: any) => void;

    /**
     * 创建UI管理器
     * @param propertiesPanel - 属性面板元素
     * @param layersList - 图层列表元素
     * @param onPropertyChange - 属性变化的回调
     */
    constructor(propertiesPanel: HTMLElement, layersList: HTMLElement, onPropertyChange: (element: BaseElement, attr: string, value: any) => void) {
        this.propertiesPanel = propertiesPanel;
        this.layersList = layersList;
        this.onPropertyChange = onPropertyChange;
    }

    /**
     * 显示选中元素的属性
     * @param element - 选中的元素
     */
    showProperties(element: BaseElement | null): void {
        // console.log('显示属性面板:', element);
        if (!element) {
            this.propertiesPanel.innerHTML = '<p>选择一个元素以编辑其属性。</p>';
            return;
        }

        this.propertiesPanel.innerHTML = '';
        const attrs = element.getAttributes();

        // 可编辑的属性列表
        const editableAttrs = ['x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'fill', 'font-size', 'font-family', 'textContent'];

        editableAttrs.forEach(attr => {
            if (attr in attrs) {
                this.createPropertyField(attr, attrs[attr], element);
            }
        });

        // 如果是文本元素，添加文本内容编辑
        if (element.type === 'text' && element.getText) {
            this.createPropertyField('textContent', element.getText()!, element, true);
        }
    }

    /**
     * 创建单个属性编辑字段
     * @param name - 属性名称
     * @param value - 属性值
     * @param element - 所属元素
     * @param isText - 是否为文本内容
     */
    createPropertyField(name: string, value: any, element: BaseElement, isText: boolean = false): void {
        const label = document.createElement('label');
        label.textContent = this.getDisplayName(name);

        let input;
        if (isText && name === 'textContent') {
            // 对于文本内容，使用 textarea
            input = document.createElement('textarea');
            input.rows = 3;
        } else {
            input = document.createElement('input');
            input.type = this.getInputType(name);
        }

        input.value = isText ? value || '' : value;
        input.addEventListener('input', (e) => {
            let updatedValue: string | number = (e.target as HTMLInputElement).value;
            if (['x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'font-size'].includes(name)) {
                updatedValue = Number(updatedValue);
                if (isNaN(updatedValue)) {
                    // 处理NaN的情况，例如提示用户或忽略更新
                    console.error(`无效的数值输入: ${updatedValue}`);
                    return;
                }
            }
            if (isText && name === 'textContent') {
                if (element.setText) {
                    element.setText(updatedValue as string);
                }
            } else {
                this.onPropertyChange(element, name, updatedValue);
            }
        });

        this.propertiesPanel.appendChild(label);
        this.propertiesPanel.appendChild(input);
    }

    /**
     * 获取属性的显示名称
     * @param name - 属性名称
     * @returns 显示名称
     */
    getDisplayName(name: string): string {
        const mapping: { [key: string]: string } = {
            'x': 'X',
            'y': 'Y',
            'width': '宽度',
            'height': '高度',
            'cx': '中心X',
            'cy': '中心Y',
            'r': '半径',
            'fill': '填充色',
            'font-size': '字体大小',
            'font-family': '字体',
            'textContent': '文本内容'
        };
        return mapping[name] || name;
    }

    /**
     * 获取输入类型
     * @param name - 属性名称
     * @returns 输入类型
     */
    getInputType(name: string): string {
        const numberAttrs = ['x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'font-size'];
        return numberAttrs.includes(name) ? 'number' : 'text';
    }

    /**
     * 更新图层管理列表
     * @param elements - 元素列表
     * @param selectedElement - 选中的元素
     */
    updateLayersList(elements: BaseElement[], selectedElement: BaseElement | null): void {
        this.layersList.innerHTML = '';
        // 从后往前渲染，以最新的在上
        [...elements].reverse().forEach(el => {
            const layerItem = document.createElement('div');
            layerItem.classList.add('layer-item');
            layerItem.textContent = `${el.type.toUpperCase()} (${el.id})`;
            if (el === selectedElement) {
                layerItem.classList.add('selected-layer');
            }
            layerItem.addEventListener('click', () => {
                if (selectedElement !== el) {
                    const event = new CustomEvent('layerSelect', { detail: el });
                    window.dispatchEvent(event);
                }
            });
            this.layersList.appendChild(layerItem);
        });
    }

    /**
     * 显示提示信息
     * @param text - 提示文本
     * @param x - X坐标
     * @param y - Y坐标
     */
    showTooltip(text: string, x: number, y: number): void {
        const tooltip = document.getElementById('tooltip');
        if (!tooltip) return;
        tooltip.textContent = text;
        tooltip.style.left = `${x}px`;
        tooltip.style.top = `${y}px`;
        tooltip.style.opacity = '1';
        setTimeout(() => {
            tooltip.style.opacity = '0';
        }, 2000);
    }
}