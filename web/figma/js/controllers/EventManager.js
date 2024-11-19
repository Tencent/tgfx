// js/controllers/EventManager.js

export default class EventManager {
    /**
     * 创建事件管理器
     * @param {ElementManager} elementManager 元素管理器
     * @param {UIManager} uiManager UI管理器
     * @param {BackendManager} backendManager 后端管理器
     * @param {SVGElement} svgCanvas SVG画布元素
     */
    constructor(elementManager, uiManager, backendManager, svgCanvas) {
        this.elementManager = elementManager;
        this.uiManager = uiManager;
        this.backendManager = backendManager;
        this.svgCanvas = svgCanvas;
        this.isDragging = false;
        this.draggingElement = null;
        this.offsetX = 0;
        this.offsetY = 0;
        this.isCanvasDragging = false;
        this.canvasStartX = 0;
        this.canvasStartY = 0;
        this.viewBox = {x: 0, y: 0, width: 1000, height: 800};
        this.isPerformanceTestRunning = false;
        this.animationFrameId = null;
        this.initEvents();
        this.initResizeListener(); /* 添加窗口调整大小监听 */
    }

    /**
     * 初始化事件监听
     */
    initEvents() {
        // 工具栏按钮事件
        document.getElementById('addRect').addEventListener('click', () => {
            this.createAndAddElement('rect', '添加矩形');
        });

        document.getElementById('addCircle').addEventListener('click', () => {
            this.createAndAddElement('circle', '添加圆形');
        });

        document.getElementById('addText').addEventListener('click', () => {
            this.createAndAddElement('text', '添加文本');
        });

        document.getElementById('addRandom').addEventListener('click', () => {
            const shapeCounts = parseInt(document.getElementById('shapeCounts').value, 10);
            const elementList = [];
            const shapes = ['rect', 'circle', 'text'];
            // const shapes = ['rect', 'circle'];

            for (let i = 0; i < shapeCounts; i++) {
                const randomShape = shapes[Math.floor(Math.random() * shapes.length)];
                const element = this.elementManager.createElement(randomShape);
                if (element) {
                    this.elementManager.selectElement(element);
                    this.uiManager.showProperties(element);
                    elementList.push(this.getElementData(element));
                    if (this.isBackend()) {
                        element.element.style.opacity = 0;
                    }
                }
            }
            this.updateLayers();
            this.sendUpdateMessage(elementList);
            // 触发自定义事件以通知图形已添加
            const event = new CustomEvent('shapeAdded');
            document.dispatchEvent(event);
        });

        document.getElementById('performanceTest').addEventListener('click', () => {
            if (this.isPerformanceTestRunning) {
                cancelAnimationFrame(this.animationFrameId);
                this.isPerformanceTestRunning = false;
            } else {
                this.isPerformanceTestRunning = true;
                this.animateElements();
            }
        });

        // 画布事件
        this.svgCanvas.addEventListener('mousedown', (e) => this.onCanvasMouseDown(e));
        this.svgCanvas.addEventListener('mousemove', (e) => this.onCanvasMouseMove(e));
        this.svgCanvas.addEventListener('mouseup', (e) => this.onCanvasMouseUp(e));
        this.svgCanvas.addEventListener('mouseleave', (e) => this.onCanvasMouseUp(e));
        this.svgCanvas.addEventListener('click', () => this.elementManager.selectElement(null));

        // 属性编辑器事件
        window.addEventListener('layerSelect', (e) => {
            const selectedEl = e.detail;
            console.log('图层选择事件触发，选中元素:', selectedEl);
            this.elementManager.selectElement(selectedEl);
            this.uiManager.showProperties(selectedEl);
            this.uiManager.updateLayersList(this.elementManager.getElements(), selectedEl);
        });

        // 后端渲染切换
        document.getElementById('backendToggle').addEventListener('change', (e) => {
            const isBackend = e.target.checked;
            if (isBackend) {
                this.enableBackendRendering();
            } else {
                this.disableBackendRendering();
            }
        });
    }

    /**
     * 初始化窗口调整大小的事件监听
     */
    initResizeListener() {
        window.addEventListener('resize', () => {
            this.svgCanvas.setAttribute('viewBox', `${this.viewBox.x} ${this.viewBox.y} ${this.viewBox.width} ${this.viewBox.height}`);
        });
    }

    /**
     * 创建并添加元素
     * @param {string} type 元素类型
     * @param {string} tooltip 提示信息
     */
    createAndAddElement(type, tooltip) {
        const element = this.elementManager.createElement(type);
        if (element) {
            this.elementManager.selectElement(element);
            this.uiManager.showProperties(element);
            this.uiManager.showTooltip(tooltip, 50, 60);
            this.updateLayers();
            this.sendUpdateMessage([this.getElementData(element)]);
            if (this.isBackend()) {
                element.element.style.opacity = 0;
            }
            // 触发自定义事件以通知图形已添加
            const event = new CustomEvent('shapeAdded');
            document.dispatchEvent(event);
        }
    }

    /**
     * 启用后端渲染
     */
    enableBackendRendering() {
        this.elementManager.setAllElementsVisibility(false);
        this.uiManager.showTooltip('后端渲染已启用', 50, 60);
        this.setElementsOpacity(0);
        this.sendEnableBackendMessage();
    }

    /**
     * 禁用后端渲染
     */
    disableBackendRendering() {
        this.elementManager.setAllElementsVisibility(true);
        this.elementManager.renderAllElements();
        this.uiManager.showTooltip('后端渲染已关闭', 50, 60);
        this.setElementsOpacity(1);
        this.sendDisableBackendMessage();
    }

    sendMoveMessage(elements) {
        if (!this.isBackend()) {
            return;
        }
        const messageObj = {
            action: 'move',
            elements: elements
        };
        this.backendManager.send(messageObj);
    }

    /**
     * 发送更新元素的消息到后端
     * @param {object[]} elements 元素数据
     */
    sendUpdateMessage(elements) {
        if (!this.isBackend()) {
            return;
        }
        const messageObj = {
            action: 'update',
            canvasRect: this.getCanvasRect(),
            viewBox: this.viewBox,
            elements: elements
        };
        this.backendManager.send(messageObj);
    }

    /**
     * 发送画布平移的消息到后端
     */
    sendCanvasPanMessage() {
        if (!this.isBackend()) {
            return;
        }
        const messageObj = {
            action: 'canvasPan',
            canvasRect: this.getCanvasRect(),
            viewBox: this.viewBox
        };
        this.backendManager.send(messageObj);
    }

    /**
     * 发送启用后端渲染的消息到后端
     */
    sendEnableBackendMessage() {
        const elements = this.elementManager.getElements();
        const elementDataList = elements.map(element => this.getElementData(element))
        const messageObj = {
            action: 'enableBackend',
            canvasRect: this.getCanvasRect(),
            viewBox: this.viewBox,
            elements: elementDataList
        };
        this.backendManager.send(messageObj);
    }

    /**
     * 发送禁用后端渲染的消息到后端
     */
    sendDisableBackendMessage() {
        const messageObj = {
            action: 'disableBackend',
        };
        this.backendManager.send(messageObj);
    }

    /**
     * 获取画布的位置信息
     * @returns {object} 画布位置信息
     */
    getCanvasRect() {
        const canvasContainer = this.svgCanvas.parentElement;
        const canvasRect = canvasContainer.getBoundingClientRect();
        return {
            x: canvasRect.x,
            y: canvasRect.y,
            width: canvasRect.width,
            height: canvasRect.height
        };
    }

    /**
     * 处理画布按下事件
     * @param {MouseEvent} e 鼠标事件
     */
    onCanvasMouseDown(e) {
        const target = e.target;
        if (target.classList.contains('element')) {
            const selectedEl = this.elementManager.getElementById(target.id);
            console.log('选中元素:', selectedEl);
            this.elementManager.selectElement(selectedEl);
            this.uiManager.showProperties(selectedEl);
            this.uiManager.updateLayersList(this.elementManager.getElements(), selectedEl);
            this.isDragging = true;
            this.draggingElement = selectedEl;
            const mousePos = this.getMousePosition(e);
            this.setDragOffsets(selectedEl, mousePos);
        } else {
            this.isCanvasDragging = true;
            this.canvasStartX = e.clientX;
            this.canvasStartY = e.clientY;
        }
    }

    /**
     * 设置拖拽偏移量
     * @param {BaseElement} element 元素
     * @param {object} mousePos 鼠标位置
     */
    setDragOffsets(element, mousePos) {
        const tagName = element.type;
        if (tagName === 'rect' || tagName === 'text') {
            const x = parseFloat(element.getAttribute('x'));
            const y = parseFloat(element.getAttribute('y'));
            this.offsetX = mousePos.x - x;
            this.offsetY = mousePos.y - y;
        } else if (tagName === 'circle') {
            const cx = parseFloat(element.getAttribute('cx'));
            const cy = parseFloat(element.getAttribute('cy'));
            this.offsetX = mousePos.x - cx;
            this.offsetY = mousePos.y - cy;
        }
    }

    /**
     * 处理画布移动事件
     * @param {MouseEvent} e 鼠标事件
     */
    onCanvasMouseMove(e) {
        if (this.isDragging && this.draggingElement) {
            const mousePos = this.getMousePosition(e);
            this.updateElementPosition(this.draggingElement, mousePos);
            // const id = this.draggingElement.getAttribute('id');
            // const offsetX = this.offsetX;
            // const offsetY = this.offsetY;
            // this.sendMoveMessage([{'id' : id, 'offsetX' : offsetX, 'offsetY' : offsetY}]);
            this.sendUpdateMessage([this.getElementData(this.draggingElement)]);
        } else if (this.isCanvasDragging) {
            this.updateViewBox(e);
            this.sendCanvasPanMessage();
        }
    }

    /**
     * 更新元素的位置
     * @param {BaseElement} element 元素
     * @param {object} mousePos 鼠标位置
     */
    updateElementPosition(element, mousePos) {
        const tagName = element.type;
        if (tagName === 'rect' || tagName === 'text') {
            const newX = mousePos.x - this.offsetX;
            const newY = mousePos.y - this.offsetY;
            element.setAttribute('x', newX);
            element.setAttribute('y', newY);
        } else if (tagName === 'circle') {
            const newCx = mousePos.x - this.offsetX;
            const newCy = mousePos.y - this.offsetY;
            element.setAttribute('cx', newCx);
            element.setAttribute('cy', newCy);
        }
    }

    /**
     * 更新视图框
     * @param {MouseEvent} e 鼠标事件
     */
    updateViewBox(e) {
        const dx = (this.canvasStartX - e.clientX) * this.viewBox.width / this.svgCanvas.clientWidth;
        const dy = (this.canvasStartY - e.clientY) * this.viewBox.height / this.svgCanvas.clientHeight;
        this.viewBox.x += dx;
        this.viewBox.y += dy;
        this.svgCanvas.setAttribute('viewBox', `${this.viewBox.x} ${this.viewBox.y} ${this.viewBox.width} ${this.viewBox.height}`);
        this.canvasStartX = e.clientX;
        this.canvasStartY = e.clientY;
    }

    /**
     * 处理画布松开事件
     * @param {MouseEvent} e 鼠标事件
     */
    onCanvasMouseUp(e) {
        this.isDragging = false;
        this.draggingElement = null;
        this.isCanvasDragging = false;
    }

    /**
     * 获取鼠标在SVG中的位置
     * @param {MouseEvent} evt 鼠标事件
     * @returns {object} 包含x和y坐标
     */
    getMousePosition(evt) {
        const CTM = this.svgCanvas.getScreenCTM();
        return {
            x: (evt.clientX - CTM.e) / CTM.a,
            y: (evt.clientY - CTM.f) / CTM.d
        };
    }

    /**
     * 处理属性变化
     * @param {BaseElement} element 发生变化的元素
     * @param {string} attribute 属性名称
     * @param {any} value 新值
     */
    handlePropertyChange(element, attribute, value) {
        if (!element || !attribute) return;

        try {
            if (attribute === 'textContent' && typeof element.setText === 'function') {
                element.setText(value);
            } else {
                element.setAttribute(attribute, value);
            }

            this.sendUpdateMessage([this.getElementData(element)]);
        } catch (error) {
            console.error(`设置属性失败: ${attribute} = ${value}`, error);
        }
    }

    /**
     * 更新图层管理视图
     */
    updateLayers() {
        this.uiManager.updateLayersList(this.elementManager.getElements(), this.elementManager.selectedElement);
    }

    /**
     * 获取元素数据
     * @param {BaseElement} element 元素
     * @returns {object} 元素数据
     */
    getElementData(element) {
        const attrs = element.getAttributes();
        const filteredAttrs = {
            id: attrs.id,
            // filter: attrs.filter,
            fill: attrs.fill,
            tagName: element.type
        };

        if (element.type === 'circle') {
            filteredAttrs.cx = parseInt(attrs.cx);
            filteredAttrs.cy = parseInt(attrs.cy);
            filteredAttrs.r = parseInt(attrs.r);
        } else if (element.type === 'text') {
            filteredAttrs.x = parseInt(attrs.x);
            filteredAttrs.y = parseInt(attrs.y);
            filteredAttrs['font-size'] = parseInt(attrs['font-size']);
            filteredAttrs.textContent = element.getText();
        } else if (element.type === 'rect') {
            filteredAttrs.x = parseInt(attrs.x);
            filteredAttrs.y = parseInt(attrs.y);
            filteredAttrs.width = parseInt(attrs.width);
            filteredAttrs.height = parseInt(attrs.height);
        }

        return filteredAttrs;
    }

    /**
     * 判断是否启用后端渲染
     * @returns {boolean} 是否启用
     */
    isBackend() {
        return document.getElementById('backendToggle').checked;
    }

    /**
     * 设置所有元素的透明度
     * @param {number} opacity 透明度值 (0-1)
     */
    setElementsOpacity(opacity) {
        const elements = this.elementManager.getElements();
        elements.forEach(element => {
            element.element.style.opacity = opacity;
        });
    }

    /**
     * 动画元素
     */
    animateElements() {
        const elements = this.elementManager.getElements();
        const currentTime = performance.now();

        if (!this.lastFrameTime) {
            this.lastFrameTime = currentTime;
        }
        const deltaTime = (currentTime - this.lastFrameTime) / 1000;
        this.lastFrameTime = currentTime;

        const elementInfoList = [];
        const containerWidth = this.viewBox.width;
        const containerHeight = this.viewBox.height;

        elements.forEach(element => {
            const tagName = element.element.tagName.toLowerCase();

            if (!element.element.dataset.vx || !element.element.dataset.vy) {
                const maxSpeed = 200;
                const minSpeed = 50;
                const speed = Math.random() * (maxSpeed - minSpeed) + minSpeed;
                const angle = Math.random() * 2 * Math.PI;

                const vx = speed * Math.cos(angle);
                const vy = speed * Math.sin(angle);

                element.element.dataset.vx = vx;
                element.element.dataset.vy = vy;
            }

            let vx = parseFloat(element.element.dataset.vx);
            let vy = parseFloat(element.element.dataset.vy);
            let x, y;

            if (tagName === 'rect' || tagName === 'text') {
                x = parseFloat(element.getAttribute('x'));
                y = parseFloat(element.getAttribute('y'));
            } else if (tagName === 'circle') {
                x = parseFloat(element.getAttribute('cx'));
                y = parseFloat(element.getAttribute('cy'));
            } else {
                // 如果是其他类型的元素，可以根据需要扩展
                return;
            }

            // 更新位置
            const offsetX = vx * deltaTime;
            const offsetY = vy * deltaTime;

            x += offsetX;
            y += offsetY;

            let bounced = false;

            // 处理边界碰撞
            if (tagName === 'circle') {
                const radius = parseFloat(element.getAttribute('r')) || 0;

                // 水平边界
                if (x - radius < 0) {
                    x = radius;
                    vx = -vx;
                    bounced = true;
                } else if (x + radius > containerWidth) {
                    x = containerWidth - radius;
                    vx = -vx;
                    bounced = true;
                }

                // 垂直边界
                if (y - radius < 0) {
                    y = radius;
                    vy = -vy;
                    bounced = true;
                } else if (y + radius > containerHeight) {
                    y = containerHeight - radius;
                    vy = -vy;
                    bounced = true;
                }
            } else {
                // 假设 rect 和 text 的宽高为0，如果有实际宽高，请调整以下逻辑
                // 水平边界
                if (x < 0) {
                    x = 0;
                    vx = -vx;
                    bounced = true;
                } else if (x > containerWidth) {
                    x = containerWidth;
                    vx = -vx;
                    bounced = true;
                }

                // 垂直边界
                if (y < 0) {
                    y = 0;
                    vy = -vy;
                    bounced = true;
                } else if (y > containerHeight) {
                    y = containerHeight;
                    vy = -vy;
                    bounced = true;
                }
            }

            // 如果碰撞，更新速度
            if (bounced) {
                element.element.dataset.vx = vx;
                element.element.dataset.vy = vy;
            }

            // 设置更新后的位置
            if (tagName === 'rect' || tagName === 'text') {
                element.setAttribute('x', x);
                element.setAttribute('y', y);
            } else if (tagName === 'circle') {
                element.setAttribute('cx', x);
                element.setAttribute('cy', y);
            }

            // const id = element.getAttribute('id');
            // elementInfoList.push({'id' : id, 'offsetX' : offsetX, 'offsetY' : offsetY});
            elementInfoList.push(this.getElementData(element));
        });

        // this.sendMoveMessage(elementInfoList);
        this.sendUpdateMessage(elementInfoList);
        // 使用 requestAnimationFrame 调度下一帧
        if (this.isPerformanceTestRunning) {
            this.animationFrameId = requestAnimationFrame(() => this.animateElements());
        }
    }

}