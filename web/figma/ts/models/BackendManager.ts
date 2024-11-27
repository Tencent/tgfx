// js/models/BackendManager.ts

// 添加全局声明以扩展 Window 接口
declare global {
    interface Window {
        cefQuery?: any;
    }
}

export default class BackendManager {
    figmaRenderer: any;
    private cefFrameTimeCons: number = 0.0;
    /**
     * 创建后端通信管理器
     */
    constructor(figmaRenderer: any) {
        this.figmaRenderer = figmaRenderer;
        this.initialize();
    }

    /**
     * 初始化后端通信（假设使用CEF的cefQuery）
     */
    initialize(): void {
        const backendLabel = document.getElementById('backendLabel');
        if (backendLabel) {
            backendLabel.textContent = window.cefQuery ? 'CEF 渲染' : 'WASM 渲染';
        }
    }

    frameTimeCons(): number {
        if (window.cefQuery) {
            this.postGetCefFrameTimeCons();
            return this.cefFrameTimeCons;
        } else {
            return this.figmaRenderer.frameTimeCons();
        }
    }

    private postGetCefFrameTimeCons() {
        (window as any).cefQuery({
            request: 'frameTimeCons',
            persistent: false,
            onSuccess: (response: string) => {
                console.log('发送成功:', response);
                this.cefFrameTimeCons = parseFloat(response)
            }
        });
    }

    /**
     * 发送启用后端渲染的消息到后端
     */
    sendEnableBackendMessage(elementDataList: any[], canvasRect: any, viewBox: any): void {
        const messageObj = {
            action: 'enableBackend',
            canvasRect: canvasRect,
            viewBox: viewBox,
            elements: elementDataList
        };
        this.send(messageObj);
    }

    /**
     * 发送禁用后端渲染的消息到后端
     */
    sendDisableBackendMessage(): void {
        const messageObj = {
            action: 'disableBackend',
        };
        this.send(messageObj);
    }


    sendMoveMessage(elements: any[]): void {
        const messageObj = {
            action: 'move',
            elements: elements
        };
        this.send(messageObj);
    }

    /**
     * 发送更新元素的消息到后端
     * @param elements - 元素数据
     */
    sendUpdateMessage(elements: any[], canvasRect: any, viewBox: any): void {
        const messageObj = {
            action: 'update',
            canvasRect: canvasRect,
            viewBox: viewBox,
            elements: elements
        };
        this.send(messageObj);
    }

    /**
     * 发送画布平移的消息到后端
     */
    sendCanvasPanMessage(canvasRect: any, viewBox: any): void {
        const messageObj = {
            action: 'canvasPan',
            canvasRect: canvasRect,
            viewBox: viewBox,
        };
        this.send(messageObj);
    }

    /**
     * 发送消息到后端
     * @param messageObj - 完整的消息对象
     */
    send(messageObj: any): void {
        if (window.cefQuery) {
            this.sendToCef(messageObj);
        } else {
            this.sendToWASM(messageObj);
        }
    }

    private sendToWASM(messageObj: object) {
        this.figmaRenderer.handMessage(JSON.stringify(messageObj));
    }

    private sendToCef(messageObj: object) {
        const message = JSON.stringify(messageObj);
        (window as any).cefQuery({
            request: message,
            persistent: false
        });
    }

}