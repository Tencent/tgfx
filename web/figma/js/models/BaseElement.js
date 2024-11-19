// js/models/BaseElement.js

export default class BaseElement {
    /**
     * 创建基础图形元素
     * @param {string} type 元素类型（rect, circle, text）
     */
    constructor(type) {
        this.type = type;
        this.id = `el_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
        this.element = document.createElementNS("http://www.w3.org/2000/svg", type);
        this.element.setAttribute('id', this.id);
        this.element.classList.add('element');
        this.element.setAttribute('filter', 'blur(5px)');
    }

    /**
     * 获取元素的属性对象
     * @returns {object} 属性键值对
     */
    getAttributes() {
        const attrs = {};
        for (let attr of this.element.attributes) {
            attrs[attr.name] = attr.value;
        }
        return attrs;
    }

    /**
     * 设置元素的属性
     * @param {string} name 属性名称
     * @param {any} value 属性值
     */
    setAttribute(name, value) {
        this.element.setAttribute(name, value);
    }

    /**
     * 获取元素的某个属性
     * @param {string} name 属性名称
     * @returns {any} 属性值
     */
    getAttribute(name) {
        return this.element.getAttribute(name);
    }

    /**
     * 渲染到指定的SVG画布
     * @param {SVGElement} svg 画布元素
     */
    render(svg) {
        svg.appendChild(this.element);
    }

    /**
     * 从数据创建元素
     * @param {object} data 元素数据
     */
    fromData(data) {
        for (let attr in data.attributes) {
            this.setAttribute(attr, data.attributes[attr]);
        }
        if (this.type === 'text' && data.textContent) {
            this.element.textContent = data.textContent;
        }
    }
}