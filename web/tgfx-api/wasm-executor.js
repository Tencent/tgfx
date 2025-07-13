/**
 * WASM执行器 - 运行编译后的WebAssembly代码并与TGFX渲染系统集成
 * 支持真实WASM执行
 */

class WasmExecutor {
    constructor(tgfxAPI) {
        this.tgfxAPI = tgfxAPI; // 现有的TGFX API绑定
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;

    }


    /**
     * 加载并实例化WebAssembly模块
     */
    async loadWasm(wasmBinary, imports = {}) {
        try {
            console.log('📦 正在加载WebAssembly模块...');
            console.log('📦 传入的导入对象:', Object.keys(imports.env || {}));

            // 直接使用传入的导入对象
            const wasmImports = imports;

            console.log('📦 最终使用的导入对象:', Object.keys(wasmImports.env || {}));

            // 实例化WebAssembly模块
            const wasmModule = await WebAssembly.instantiate(wasmBinary, wasmImports);

            this.wasmModule = wasmModule;
            this.wasmExports = wasmModule.instance.exports;
            this.wasmMemory = this.wasmExports.memory || wasmImports.env.memory;
            this.isReady = true;

            console.log('✅ WebAssembly模块加载成功');
            console.log('📋 可用导出:', Object.keys(this.wasmExports));

            return true;

        } catch (error) {
            console.error('❌ WebAssembly加载失败:', error);
            console.error('❌ 错误详情:', error.message);
            console.error('❌ 错误堆栈:', error.stack);
            throw new Error(`WASM加载失败: ${error.message}`);
        }
    }



    /**
     * 执行用户主函数
     */
    async execute(functionName = 'user_main') {
        if (!this.isReady) {
            throw new Error('WASM模块未准备就绪');
        }

        try {
            // 使用真实WASM执行器
            if (!this.wasmExports[functionName]) {
                throw new Error(`函数 ${functionName} 未找到`);
            }

            console.log(`🎯 执行WASM函数: ${functionName}`);

            // 执行用户代码
            const result = this.wasmExports[functionName]();

            console.log(`✅ WASM执行完成，返回值: ${result}`);
            return result;

        } catch (error) {
            console.error('❌ WASM执行失败:', error);
            throw new Error(`WASM执行失败: ${error.message}`);
        }
    }

    /**
     * 清理资源
     */
    cleanup() {
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;
        console.log('🧹 WASM执行器已清理');
    }

    /**
     * 获取执行器状态
     */
    getStatus() {
        return {
            ready: this.isReady,
            hasModule: !!this.wasmModule,
            hasExports: !!this.wasmExports,
            memorySize: this.wasmMemory ? this.wasmMemory.buffer.byteLength : 0
        };
    }
}

export { WasmExecutor };
