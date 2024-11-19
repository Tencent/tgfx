// js/models/TextElement.ts

import BaseElement from './BaseElement.js';

export default class TextElement extends BaseElement {
    /**
     * 创建文本元素
     */
    constructor() {
        super('text');
        // 默认属性
        this.setAttribute('x', this.getRandomInt(0, 1000).toString());
        this.setAttribute('y', this.getRandomInt(0, 800).toString());
        this.setAttribute('text-anchor', "start");
        this.setAttribute('dominant-baseline', "text-before-edge");
        this.setAttribute('fill', this.getRandomColor());
        this.setAttribute('font-size', this.getRandomInt(12, 36).toString());
        this.setAttribute('font-family', this.getRandomFontFamily());
        this.element.textContent = this.getRandomText();
    }

    /**
     * 设置文本内容
     * @param text - 文本内容
     */
    setText(text: string): void {
        this.element.textContent = text;
    }

    /**
     * 获取文本内容
     * @returns 文本内容
     */
    getText(): string | null {
        return this.element.textContent;
    }

    /**
     * 获取随机整数
     * @param min - 最小值
     * @param max - 最大值
     * @returns 随机整数
     */
    getRandomInt(min: number, max: number): number {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    /**
     * 获取随机颜色
     * @returns 随机颜色
     */
    getRandomColor(): string {
        const letters = '0123456789ABCDEF';
        let color = '#';
        for (let i = 0; i < 6; i++) {
            color += letters[Math.floor(Math.random() * 16)];
        }
        return color;
    }

    /**
     * 获取随机字体
     * @returns 随机字体
     */
    getRandomFontFamily(): string {
        const fonts = ['Arial, sans-serif', 'Verdana, sans-serif', 'Times New Roman, serif', 'Courier New, monospace'];
        return fonts[Math.floor(Math.random() * fonts.length)];
    }

    /**
     * 获取随机文本
     * @returns 随机文本
     */
    getRandomText(): string {
        const texts = [
            '你好', '世界', '随机', '文本', '示例', '测试', '演示', '你好世界', '示例文本', '测试文本',
            // ...其他文本...
        ];
        return texts[Math.floor(Math.random() * texts.length)];
    }
}