// js/models/BackendManager.ts

// 添加全局声明以扩展 Window 接口
declare global {
    interface Window {
        cefQuery?: any;
    }
}

export default class BackendManager {
    /**
     * 创建后端通信管理器
     * @param onMessageReceived - 接收到消息时的回调
     */
    constructor() {
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
            console.warn('cefQuery 未定义���后端通信不可用');
        }
    }

    /**
     * 发送消息到后端
     * @param messageObj - 完整的消息对象
     */
    send(messageObj: object): void {
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