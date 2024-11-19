// js/models/BackendManager.js

export default class BackendManager {
    /**
     * 创建后端通信管理器
     * @param {function} onMessageReceived 接收到消息时的回调
     */
    constructor(onMessageReceived) {
        this.onMessageReceived = onMessageReceived;
        this.initialize();
    }

    /**
     * 初始化后端通信（假设使用CEF的cefQuery）
     */
    initialize() {
        if (window.cefQuery) {
            // 假设后端会主动发送消息，可以在此设置监听
            // 具体实现取决于后端如何与前端通信
            // 例如，通过WebSockets或其他方式
        } else {
            console.warn('cefQuery 未定义，后端通信不可用');
        }
    }

    /**
     * 发送消息到后端
     * @param {object} messageObj 完整的消息对象
     */
    send(messageObj) {
        if (!window.cefQuery) {
            console.error('cefQuery 未定义，无法发送消息到后端');
            return;
        }

        const message = JSON.stringify(messageObj);
        window.cefQuery({
            request: message,
            persistent: false,
            onSuccess: (response) => {
                console.log('发送成功:', response);
            },
            onFailure: (error_code, error_message) => {
                console.error(`发送失败 (${error_code}): ${error_message}`);
            }
        });
    }

    /**
     * 接收来自后端的消息
     * @param {string} message 后端消息
     */
    receive(message) {
        if (this.onMessageReceived) {
            try {
                const data = JSON.parse(message);
                this.onMessageReceived(data);
            } catch (error) {
                console.error('解析后端消息失败:', error);
            }
        }
    }
}