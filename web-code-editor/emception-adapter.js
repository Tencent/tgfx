/**
 * EmceptionAdapter - 浏览器端C++编译器适配器
 */
const __tgfxSingletons = {
    emceptionModulePromise: null,
    emceptionModuleInstance: null,
    headersPreparedPromise: null
};

export default class EmceptionAdapter {
    constructor() {
        this.emceptionModule = null;
        this.isInitialized = false;
        this.compilationCache = new Map();
        this.compileOptions = { optimization: '-O1', target: 'wasm32', outputFormat: 'wasm', enableExceptions: false };
    }

    /**
     * 初始化Emception编译器
     */
    async initialize(options = {}) {
        if (this.isInitialized) return true;

        try {
            console.log('Initializing EmceptionAdapter...');
            await this.loadEmceptionCore();
            await this.prepareTGFXEnvironment();
            Object.assign(this.compileOptions, options.compileOptions);
            this.isInitialized = true;
            console.log('EmceptionAdapter initialized successfully');
            return true;
        } catch (error) {
            console.error('EmceptionAdapter initialization failed:', error);
            throw new Error(`EmceptionAdapter initialization failed: ${error.message}`);
        }
    }

    /**
     * 加载Emception
     */
    async loadEmceptionCore() {
        if (__tgfxSingletons.emceptionModuleInstance) {
            this.emceptionModule = __tgfxSingletons.emceptionModuleInstance;
            return;
        }
        if (!__tgfxSingletons.emceptionModulePromise) {
            __tgfxSingletons.emceptionModulePromise = (async () => {
                const EmceptionModule = await import('./emception/emception.js');
                const inst = new EmceptionModule.default();
                await inst.init();
                return inst;
            })();
        }
        this.emceptionModule = await __tgfxSingletons.emceptionModulePromise;
        __tgfxSingletons.emceptionModuleInstance = this.emceptionModule;
        console.log('Emception loaded successfully');
    }

    /**
     * 准备TGFX环境 - 头文件和静态库
     */
    async prepareTGFXEnvironment() {
        console.log('Preparing TGFX environment...');

        if (__tgfxSingletons.headersPreparedPromise) {
            await __tgfxSingletons.headersPreparedPromise;
            return;
        }

        __tgfxSingletons.headersPreparedPromise = (async () => {
            // 创建目录
            ['/usr', '/usr/local', '/usr/local/include', '/usr/local/lib'].forEach(dir => this.ensureDir(dir));

            const baseUrl = (typeof window !== 'undefined' ? window.location.href : self.location.href);

            // 加载头文件
            const manifestUrl = new URL('./wasm-assets/header-manifest.json', baseUrl).toString();
            const manifest = await (await fetch(manifestUrl)).json();
            const files = Array.isArray(manifest.files) ? manifest.files : [];

            const baseHeadersUrl = new URL('./wasm-assets/headers/', baseUrl).toString();
            await Promise.all(files.map(async (relPath) => {
                const targetPath = `/usr/local/include/${relPath}`;
                const dirPath = targetPath.substring(0, targetPath.lastIndexOf('/'));
                this.ensureDir(dirPath);

                if (!this.fsExists(targetPath)) {
                    const text = await this.fetchText(baseHeadersUrl + relPath);
                    this.fsWriteFile(targetPath, text);
                }
            }));

            // 加载静态库
            const libPath = '/usr/local/lib/tgfx.a';
            if (!this.fsExists(libPath)) {
                const libUrl = new URL('./wasm-assets/libs/tgfx.a', baseUrl).toString();
                const libBin = await this.fetchBinary(libUrl);
                this.fsWriteFile(libPath, new Uint8Array(libBin));
                console.log('Static lib installed, size:', libBin.byteLength);
            }
        })();

        await __tgfxSingletons.headersPreparedPromise;
    }

    ensureDir(path) {
        if (!this.fsExists(path)) {
            this.emceptionModule.fileSystem.mkdirTree(path);
        }
    }

    async fetchText(url) {
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(`请求失败: ${url} ${resp.status}`);
        return await resp.text();
    }

    async fetchBinary(url) {
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(`请求失败: ${url} ${resp.status}`);
        return await resp.arrayBuffer();
    }

    /**
     * 编译C++代码到WebAssembly
     */
    async compile(sourceCode, options = {}) {
        if (!this.isInitialized) throw new Error('EmceptionAdapter not initialized');

        const compileId = this.generateCompileId(sourceCode, options);
        if (this.compilationCache.has(compileId)) {
            console.log('Using cached compilation result');
            return this.compilationCache.get(compileId);
        }

        try {
            console.log('Starting C++ compilation...');
            const sourceFile = '/working/main.cpp';
            this.fsWriteFile(sourceFile, sourceCode);

            const compileOptions = { ...this.compileOptions, ...options };
            const result = await this.performCompilation(sourceFile, compileOptions);

            this.compilationCache.set(compileId, result);
            console.log('Compilation completed successfully');
            return result;
        } catch (error) {
            console.error('❌ Compilation failed:', error);
            throw new Error(`Compilation failed: ${error.message}`);
        }
    }

    /**
     * 执行编译操作
     */
    async performCompilation(sourceFile, options) {
        const objectFile = '/working/main.o';
        const stubFile = '/working/export_stubs.cpp';
        const stubObject = '/working/export_stubs.o';
        const outputJS = '/working/output.mjs';

        // 生成导出桩代码
        const stubs = this.generateStubCode();
        this.fsWriteFile(stubFile, stubs);

        // 编译主文件
        const compileCmd = [
            'em++', '-std=c++17', options.optimization || '-O1', '-fno-exceptions', '-fno-rtti',
            '-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0', '-D_LIBCPP_ABI_NAMESPACE=__2',
            '-Wno-error=version-check', '-c', '-I/usr/local/include', sourceFile, '-o', objectFile
        ].join(' ');

        const compileResult = await this.emceptionModule.run(compileCmd);
        if (compileResult.returncode !== 0) {
            throw new Error(`Compilation failed: ${compileResult.stderr || 'Unknown error'}`);
        }

        // 编译桩文件
        if (!this.fsExists(stubObject)) {
            const stubCmd = [
                'em++', '-std=c++17', options.optimization || '-O1', '-fno-exceptions', '-fno-rtti',
                '-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0', '-D_LIBCPP_ABI_NAMESPACE=__2',
                '-I/usr/local/include', '-c', stubFile, '-o', stubObject
            ].join(' ');

            const stubResult = await this.emceptionModule.run(stubCmd);
            if (stubResult.returncode !== 0) {
                throw new Error(`Stub compilation failed: ${stubResult.stderr || 'Unknown error'}`);
            }
        }

        // 链接
        const linkCmd = [
            'em++', objectFile, stubObject, '/usr/local/lib/tgfx.a',
            options.optimization || '-O1', '-Wno-error=version-check',
            '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sSINGLE_FILE=1', '-sENVIRONMENT=web',
            '-sALLOW_MEMORY_GROWTH=1', '-sASSERTIONS=0', '-sDISABLE_EXCEPTION_CATCHING=1',
            "-sEXPORTED_FUNCTIONS=['_main']", '-sERROR_ON_UNDEFINED_SYMBOLS=0',
            '-sFORCE_FILESYSTEM=1', '-sEXPORTED_RUNTIME_METHODS=["FS"]', '-o', outputJS
        ].join(' ');

        const linkResult = await this.emceptionModule.run(linkCmd);
        if (linkResult.returncode !== 0) {
            throw new Error(`Linking failed: ${linkResult.stderr || 'Unknown error'}`);
        }

        // 读取结果
        const jsBytes = this.fsReadFile(outputJS);
        const jsModuleCode = new TextDecoder('utf-8').decode(jsBytes);

        let linkedWasm;
        const wasmPath = '/working/output.wasm';
        if (this.fsExists(wasmPath)) {
            linkedWasm = this.fsReadFile(wasmPath);
        }

        return {
            success: true,
            jsModuleCode,
            wasmBinary: linkedWasm,
            exports: ['main'],
            stderr: (compileResult.stderr || '') + (linkResult.stderr || ''),
            stdout: (compileResult.stdout || '') + (linkResult.stdout || '')
        };
    }

    /**
    /**
     * 生成最小导出桩代码
     */
    generateStubCode() {
        return 'namespace tgfx { __attribute__((weak)) bool TGFXBindInit() { return true; } }';
    }

    // 文件系统操作
    fsWriteFile(path, content) {
        return this.emceptionModule.writeFile ?
            this.emceptionModule.writeFile(path, content) :
            this.emceptionModule.fileSystem.writeFile(path, content);
    }

    fsReadFile(path) {
        return this.emceptionModule.readFile ?
            this.emceptionModule.readFile(path) :
            this.emceptionModule.fileSystem.readFile(path);
    }

    fsExists(path) {
        return this.emceptionModule.exists ?
            this.emceptionModule.exists(path) :
            this.emceptionModule.fileSystem.exists(path);
    }

    /**
     * 生成编译缓存ID
     */
    generateCompileId(sourceCode, options) {
        const content = sourceCode + JSON.stringify(options);
        let hash = 0;
        for (let i = 0; i < content.length; i++) {
            const char = content.charCodeAt(i);
            hash = ((hash << 5) - hash) + char;
            hash = hash & hash;
        }
        return Math.abs(hash).toString(16).substring(0, 16);
    }

    /**
     * 清除编译缓存
     */
    clearCache() {
        this.compilationCache.clear();
        console.log('🗑️ Compilation cache cleared');
    }
}
