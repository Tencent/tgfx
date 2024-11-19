// js/views/UIManager.js

export default class UIManager {
    /**
     * 创建UI管理器
     * @param {HTMLElement} propertiesPanel 属性面板元素
     * @param {HTMLElement} layersList 图层列表元素
     * @param {Function} onPropertyChange 属性变化的回调
     */
    constructor(propertiesPanel, layersList, onPropertyChange) {
        this.propertiesPanel = propertiesPanel;
        this.layersList = layersList;
        this.onPropertyChange = onPropertyChange;
    }

    /**
     * 显示选中元素的属性
     * @param {BaseElement} element 选中的元素
     */
    showProperties(element) {
        console.log('显示属性面板:', element);
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
        if (element.type === 'text') {
            this.createPropertyField('textContent', element.getText(), element, true);
        }
    }

    /**
     * 创建单个属性编辑字段
     * @param {string} name 属性名称
     * @param {any} value 属性值
     * @param {BaseElement} element 所属元素
     * @param {boolean} isText 是否为文本内容
     */
    createPropertyField(name, value, element, isText = false) {
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

        input.value = isText ? value : value;
        input.addEventListener('input', (e) => {
            let updatedValue = e.target.value;
            if (['x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'font-size'].includes(name)) {
                updatedValue = Number(updatedValue);
                if (isNaN(updatedValue)) {
                    console.warn(`无效的数值输入: ${updatedValue}`);
                    return;
                }
            }
            if (isText && name === 'textContent') {
                this.onPropertyChange(element, name, e.target.value);
            } else {
                this.onPropertyChange(element, name, updatedValue);
            }
        });

        this.propertiesPanel.appendChild(label);
        this.propertiesPanel.appendChild(input);
    }

    /**
     * 获取属性的显示名称
     * @param {string} name 属性名称
     * @returns {string} 显示名称
     */
    getDisplayName(name) {
        const mapping = {
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
     * @param {string} name 属性名称
     * @returns {string} 输入类型
     */
    getInputType(name) {
        const numberAttrs = ['x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'font-size'];
        return numberAttrs.includes(name) ? 'number' : 'text';
    }

    /**
     * 更新图层管理列表
     * @param {Array<BaseElement>} elements 元素列表
     * @param {BaseElement} selectedElement 选中的元素
     */
    updateLayersList(elements, selectedElement) {
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
     * @param {string} text 提示文本
     * @param {number} x X坐标
     * @param {number} y Y坐标
     */
    showTooltip(text, x, y) {
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