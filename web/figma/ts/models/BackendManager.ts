// js/models/BackendManager.ts

// 添加全局声明以扩展 Window 接口
declare global {
    interface Window {
        cefQuery?: any;
    }
}

export default class BackendManager {
    figmaRenderer: any; // 修改类型为 any 或者适当的类型

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
        if (window.cefQuery) {
            // 假设后端会主动发送消息，可以在此设置监听
            // 具体实现取决于后端如何与前端通信
            // 例如，通过WebSockets或其他方式
        } else {
            console.warn('cefQuery 未定义,后端通信不可用');
        }
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
        if (!this.isBackend()) {
            return;
        }
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
        if (!this.isBackend()) {
            return;
        }
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
        if (!this.isBackend()) {
            return;
        }
        const messageObj = {
            action: 'canvasPan',
            canvasRect: canvasRect,
            viewBox: viewBox,
        };
        this.send(messageObj);
    }

    /**
     * 判断是否启用后端渲染
     * @returns 是否启用
     */
    isBackend(): boolean {
        const backendToggle = document.getElementById('backendToggle') as HTMLInputElement;
        return backendToggle.checked;
    }

    /**
     * 发送消息到后端
     * @param messageObj - 完整的消息对象
     */
    send(messageObj: object): void {
        this.sendToWASM(messageObj);
    }

    private sendToWASM(messageObj: object) {
        this.figmaRenderer.handMessage(JSON.stringify(messageObj));
    }

    private sendToCef(messageObj: object) {
        if (!(window as any).cefQuery) {
            console.error('cefQuery 未定义，无法发送消息到后端');
            return;
        }

        const message = JSON.stringify(messageObj);
        (window as any).cefQuery({
            request: message,
            persistent: false,
            onSuccess: (response: string) => {
                console.log('发送成功:', response);
            },
            onFailure: (error_code: number, error_message: string) => {
                console.error(`发送失败 (${error_code}): ${error_message}`);
            }
        });
    }
}