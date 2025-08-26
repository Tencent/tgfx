/**
 * Compilation Worker - 在后台线程执行编译任务
 */

import EmceptionAdapter from './emception-adapter.js';

let adapter = null;
let isInitialized = false;

self.addEventListener('message', async (event) => {
    const { id, type, data } = event.data;

    try {
        switch (type) {
            case 'init':
                await initializeAdapter(data);
                self.postMessage({ id, type: 'init', success: true });
                break;

            case 'compile':
                const result = await compileCode(data.sourceCode, data.options);
                self.postMessage({ id, type: 'compile', success: true, result });
                break;

            case 'clearCache':
                if (adapter) {
                    adapter.clearCache();
                }
                self.postMessage({ id, type: 'clearCache', success: true });
                break;

            default:
                throw new Error(`Unknown message type: ${type}`);
        }
    } catch (error) {
        self.postMessage({ 
            id, 
            type, 
            success: false, 
            error: error.message 
        });
    }
});

async function initializeAdapter(options = {}) {
    if (isInitialized) return;
    
    adapter = new EmceptionAdapter();
    await adapter.initialize(options);
    isInitialized = true;
}

async function compileCode(sourceCode, options = {}) {
    if (!isInitialized) {
        throw new Error('Adapter not initialized');
    }
    
    return await adapter.compile(sourceCode, options);
}