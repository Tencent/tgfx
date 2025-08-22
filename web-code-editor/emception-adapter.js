/**
 * EmceptionAdapter - 真实 Emception 适配器
 * 目标：在浏览器端将 用户C++ 源码 与 预编译的 tgfx.a 静态库 链接为单一 WASM
 * 去除精简 API 与句柄式导入，允许直接使用原生 TGFX C++ 头文件与类型
 */
const __tgfxSingletons = { emceptionModulePromise: null, emceptionModuleInstance: null, headersPreparedPromise: null };

export default class EmceptionAdapter {
    constructor() {
        this.emceptionModule = null;
        this.isInitialized = false;
        this.compilationCache = new Map();

        // 默认编译选项
        this.defaultCompileOptions = {
            optimization: '-O2',
            target: 'wasm32',
            outputFormat: 'wasm',
            enableExceptions: true
        };
        this.compileOptions = { ...this.defaultCompileOptions };
    }

    /**
     * 初始化Emception编译器
     */
    async initialize(options = {}) {
        if (this.isInitialized) {
            console.log('⚠️ Emception adapter already initialized');
            return true;
        }

        try {
            console.log('🔄 Initializing EmceptionAdapter...');

            // 1. 加载并初始化 Emception（完整版本，包含标准库与sysroot）
            await this.loadEmceptionCore();

            // 2. 准备TGFX头文件环境
            await this.prepareTGFXEnvironment();

            // 3. 配置编译器选项
            this.configureCompiler(options);

            this.isInitialized = true;
            console.log('✅ EmceptionAdapter initialized successfully');
            return true;

        } catch (error) {
            console.error('❌ EmceptionAdapter initialization failed:', error);
            throw new Error(`EmceptionAdapter initialization failed: ${error.message}`);
        }
    }

    /**
     * 加载EmceptionCore模块并初始化
     */
    async loadEmceptionCore() {
        try {
            console.log('🔄 Loading Emception...');
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
            console.log('✅ Emception loaded and initialized successfully');
        } catch (error) {
            console.error('❌ Failed to load Emception:', error);
            throw new Error(`Failed to load Emception: ${error.message}`);
        }
    }

    /**
     * 准备TGFX环境：加载完整头文件与静态库
     * - 头文件写入到 /usr/local/include
     * - 静态库写入到 /usr/local/lib/tgfx.a
     */
    async prepareTGFXEnvironment() {
        console.log('📚 Preparing TGFX environment (headers + static lib)...');

        try {
            if (__tgfxSingletons.headersPreparedPromise) {
                await __tgfxSingletons.headersPreparedPromise;
                return;
            }
            __tgfxSingletons.headersPreparedPromise = (async () => {
                // 确保基本目录存在
                this.ensureDir('/usr');
                this.ensureDir('/usr/local');
                this.ensureDir('/usr/local/include');
                this.ensureDir('/usr/local/lib');

                // 加载头文件清单
                const manifestUrl = new URL('./wasm-assets/header-manifest.json', window.location.href).toString();
                const manifestResp = await fetch(manifestUrl);
                if (!manifestResp.ok) {
                    throw new Error(`加载头文件清单失败: ${manifestResp.status} ${manifestResp.statusText}`);
                }
                const manifest = await manifestResp.json();
                const files = Array.isArray(manifest.files) ? manifest.files : [];
                console.log(`📄 header files: ${files.length}`);

                // 批量加载与写入头文件（存在则跳过）
                const baseHeadersUrl = new URL('./wasm-assets/headers/', window.location.href).toString();
                for (const relPath of files) {
                    const fileUrl = baseHeadersUrl + relPath;
                    const targetPath = `/usr/local/include/${relPath}`;
                    const dirPath = targetPath.substring(0, targetPath.lastIndexOf('/'));
                    this.ensureDir(dirPath);

                    if (!this.fsExists(targetPath)) {
                        const text = await this.fetchText(fileUrl);
                        this.fsWriteFile(targetPath, text);
                    }
                }
                console.log('✅ All TGFX headers installed to /usr/local/include');

                // 加载静态库 tgfx.a（存在则跳过）
                const libPath = '/usr/local/lib/tgfx.a';
                if (!this.fsExists(libPath)) {
                    const libUrl = new URL('./wasm-assets/libs/tgfx.a', window.location.href).toString();
                    const libBin = await this.fetchBinary(libUrl);
                    this.fsWriteFile(libPath, new Uint8Array(libBin));
                    console.log('✅ Static lib installed to ' + libPath + ' (size: ' + libBin.byteLength + ')');
                } else {
                    console.log('ℹ️ Static lib already present, skip writing');
                }
            })();

            await __tgfxSingletons.headersPreparedPromise;

        } catch (error) {
            console.error('❌ Failed to prepare TGFX environment:', error);
            throw new Error(`Failed to prepare TGFX environment: ${error.message}`);
        }
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
     * 配置编译器选项
     */
    configureCompiler(options = {}) {
        console.log('⚙️ Configuring compiler options...');

        // 合并用户选项和默认选项
        this.compileOptions = {
            ...this.defaultCompileOptions,
            ...options.compileOptions
        };

        console.log('✅ Compiler configuration complete');
    }

    /**
     * 编译C++代码到WebAssembly
     */
    async compile(sourceCode, options = {}) {
        if (!this.isInitialized) {
            throw new Error('EmceptionAdapter not initialized');
        }

        try {
            console.log('🔨 Starting C++ compilation...');

            // 生成编译缓存ID
            const compileId = this.generateCompileId(sourceCode, options);

            // 检查缓存
            if (this.compilationCache.has(compileId)) {
                console.log('🎯 Using cached compilation result');
                return this.compilationCache.get(compileId);
            }

            // 1. 准备源文件
            const sourceFile = '/working/main.cpp';
            this.fsWriteFile(sourceFile, sourceCode);
            console.log('📝 Source file written to:', sourceFile);

            // 2. 执行编译
            const compileOptions = {
                ...this.compileOptions,
                ...options
            };

            // 根据源码判断导出入口（优先 user_main，否则 main）
            const wantsUserMain = /\buser_main\s*\(/.test(sourceCode);
            const compileOptions2 = { ...compileOptions, exportEntry: wantsUserMain ? 'user_main' : 'main' };

            const result = await this.performCompilation(sourceFile, compileOptions2);

            // 3. 缓存结果
            this.compilationCache.set(compileId, result);

            console.log('✅ Compilation completed successfully');
            return result;

        } catch (error) {
            console.error('❌ Compilation failed:', error);
            throw new Error(`Compilation failed: ${error.message}`);
        }
    }

    /**
    /**
     * 包装用户代码 - 支持标准C++语法的Paint类
     */
    wrapUserCode(userCode) {
        // 检查用户代码是否已经包含函数定义（包含 extern "C" 的 user_main/main）
        const hasFunctionDef = /(extern\s+\"C\"\s+)?int\s+user_main\s*\(|(extern\s+\"C\"\s+)?int\s+main\s*\(|void\s+\w+\s*\(/.test(userCode);

        let processedCode = userCode;

        if (hasFunctionDef) {
            // 如果包含 draw()，则提升为 user_main()
            if (processedCode.includes('void draw(')) {
                processedCode = processedCode.replace(/void\s+draw\s*\(\s*\)\s*\{/, 'extern "C" int user_main() {');
                processedCode = processedCode.replace(/\}(\s*)$/, '    return 0;\n}$1');
            } else if (processedCode.includes('int main(')) {
                processedCode = processedCode.replace(/int\s+main\s*\([^)]*\)\s*\{/, 'extern "C" int user_main() {');
            }
            return processedCode;
        }

        // 代码片段：包装到 user_main()
        const finalCode = `extern "C" {\n    int user_main() {\n        ${processedCode}\n        return 0;\n    }\n}`;
        return finalCode;
    }

    /**
     * 执行编译操作
     */
    async performCompilation(sourceFile, options) {
        const objectFile = '/working/main.o';
        const stubFile = '/working/export_stubs.cpp';
        const stubObject = '/working/export_stubs.o';
        const outputJS = '/working/output.mjs';

        console.log('🔧 Using two-step compilation process...');

        console.log("-------"+sourceFile);

        // 写入导出桩（弱符号，用户实现会覆盖）。提供可见的默认渲染，便于快速验证链路。
        const stubs = [
            '#include <cstdlib>',
            '#include <cstdint>',
            '#include <algorithm>',
            '#include <memory>',
            '#include <string>',
            '#include <emscripten.h>',
            '#include <emscripten/html5.h>',
            '#include <tgfx/gpu/opengl/webgl/WebGLWindow.h>',
            '#include <tgfx/gpu/opengl/webgl/WebGLDevice.h>',
            'namespace tgfx { __attribute__((weak)) bool TGFXBindInit() { return true; } }',
            'namespace tgfx {',
            '__attribute__((weak)) std::shared_ptr<WebGLDevice> WebGLDevice::MakeFrom(const std::string& canvasID) {',
            '  auto oldContext = emscripten_webgl_get_current_context();',
            '  EmscriptenWebGLContextAttributes attrs;',
            '  emscripten_webgl_init_context_attributes(&attrs);',
            '  attrs.depth = EM_FALSE;',
            '  attrs.stencil = EM_FALSE;',
            '  attrs.antialias = EM_FALSE;',
            '  attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;',
            '  attrs.enableExtensionsByDefault = EM_TRUE;',
            '  attrs.majorVersion = 2;',
            '  attrs.minorVersion = 0;',
            '  auto context = emscripten_webgl_create_context(canvasID.c_str(), &attrs);',
            '  if (context == 0) {',
            '    attrs.majorVersion = 1;',
            '    context = emscripten_webgl_create_context(canvasID.c_str(), &attrs);',
            '    if (context == 0) {',
            '      return nullptr;',
            '    }',
            '  }',
            '  auto result = emscripten_webgl_make_context_current(context);',
            '  if (result != EMSCRIPTEN_RESULT_SUCCESS) {',
            '    emscripten_webgl_destroy_context(context);',
            '    if (oldContext) {',
            '      emscripten_webgl_make_context_current(oldContext);',
            '    }',
            '    return nullptr;',
            '  }',
            '  emscripten_webgl_make_context_current(0);',
            '  if (oldContext) {',
            '    emscripten_webgl_make_context_current(oldContext);',
            '  }',
            '  return WebGLDevice::Wrap(context, false);',
            '}',
            '__attribute__((weak)) std::shared_ptr<WebGLWindow> WebGLWindow::MakeFrom(const std::string& canvasID) {',
            '  if (canvasID.empty()) {',
            '    return nullptr;',
            '  }',
            '  auto device = WebGLDevice::MakeFrom(canvasID);',
            '  if (device == nullptr) {',
            '    return nullptr;',
            '  }',
            '  auto window = std::shared_ptr<WebGLWindow>(new WebGLWindow(device));',
            '  window->canvasID = canvasID;',
            '  return window;',
            '}',
            '}',
            // 'static uint8_t* __tgfx_fb = nullptr; static int __tgfx_w = 0, __tgfx_h = 0;',
            // 'static inline void __tgfx_fill(uint8_t* fb,int w,int h,int x,int y,int rw,int rh,uint8_t r,uint8_t g,uint8_t b,uint8_t a){',
            // '  int x0 = std::max(0, x), y0 = std::max(0, y);',
            // '  int x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);',
            // '  for (int yy = y0; yy < y1; ++yy) { uint8_t* row = fb + (size_t)yy * w * 4; for (int xx = x0; xx < x1; ++xx) { uint8_t* px = row + xx * 4; px[0]=r; px[1]=g; px[2]=b; px[3]=a; } }',
            // '}',
            // 'extern "C" {',
            // '__attribute__((weak)) int tgfx_init(int w, int h) { if (w<=0||h<=0) return 0; std::free(__tgfx_fb); __tgfx_w=w; __tgfx_h=h; __tgfx_fb=(uint8_t*)std::malloc((size_t)w*h*4); if(!__tgfx_fb){__tgfx_w=__tgfx_h=0;return 0;} return 1; }',
            // '__attribute__((weak)) void tgfx_resize(int w, int h) { (void)tgfx_init(w,h); }',
            // '__attribute__((weak)) void tgfx_clear(float r,float g,float b,float a) { if(!__tgfx_fb||__tgfx_w<=0||__tgfx_h<=0) return; uint8_t R=(uint8_t)std::clamp(r*255.f,0.f,255.f); uint8_t G=(uint8_t)std::clamp(g*255.f,0.f,255.f); uint8_t B=(uint8_t)std::clamp(b*255.f,0.f,255.f); uint8_t A=(uint8_t)std::clamp(a*255.f,0.f,255.f); for(int yy=0; yy<__tgfx_h; ++yy){ uint8_t* row=__tgfx_fb + (size_t)yy*__tgfx_w*4; for(int xx=0; xx<__tgfx_w; ++xx){ uint8_t* px=row+xx*4; px[0]=R; px[1]=G; px[2]=B; px[3]=A; } } }',
            // '__attribute__((weak)) void tgfx_render() { if(!__tgfx_fb||__tgfx_w<=0||__tgfx_h<=0) return; __tgfx_fill(__tgfx_fb,__tgfx_w,__tgfx_h,40,40,160,100,255,0,0,255); for(int xx=0; xx<__tgfx_w; ++xx){ uint8_t r=(uint8_t)((xx/(float)__tgfx_w)*255); for(int yy=0; yy<20; ++yy){ uint8_t* px=__tgfx_fb + ((size_t)yy*__tgfx_w + xx)*4; px[0]=r; px[1]=128; px[2]=255-r; px[3]=255; } } }',
            // '__attribute__((weak)) void tgfx_present() { }',
            // '__attribute__((weak)) unsigned char* tgfx_get_framebuffer_ptr() { return __tgfx_fb; }',
            // '__attribute__((weak)) int tgfx_get_width() { return __tgfx_w; }',
            // '__attribute__((weak)) int tgfx_get_height() { return __tgfx_h; }',
            // '}'
        ].join('\n');
        this.fsWriteFile(stubFile, stubs);

        // 步骤1：编译到目标文件
        const compileCommand = [
            'em++',
            '-std=c++17',
            '-O2','-fno-exceptions','-fno-rtti','-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0','-DTGFX_USE_EMBIND=0','-D_LIBCPP_ABI_NAMESPACE=__2',
            '-Wno-error=version-check',
            '-c',
            '-I/usr/local/include',
            sourceFile,
            '-o',
            objectFile
        ].join(' ');

        console.log('🔧 Step 1 - Compile to object:', compileCommand);

        let compileResult;
        try {
            compileResult = await this.emceptionModule.run(compileCommand);
            console.log('✅ Compilation completed, returncode:', compileResult.returncode);
        } catch (error) {
            console.error('❌ Compilation failed:', error);
            throw new Error(`Compilation failed: ${error.message}`);
        }

        if (compileResult.returncode !== 0) {
            throw new Error(`Compilation failed with return code ${compileResult.returncode}: ${compileResult.stderr || 'Unknown error'}`);
        }

        // 步骤1b：编译导出桩
        const compileStubCmd = [
            'em++',
            '-std=c++17','-O2','-fno-exceptions','-fno-rtti','-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0','-DTGFX_USE_EMBIND=0','-D_LIBCPP_ABI_NAMESPACE=__2',
            '-I/usr/local/include',
            '-c', stubFile,
            '-o', stubObject
        ].join(' ');
        console.log('🔧 Step 1b - Compile export stubs:', compileStubCmd);
        const compileStubRes = await this.emceptionModule.run(compileStubCmd);
        if (compileStubRes.returncode !== 0) {
            throw new Error(`Stub compilation failed with return code ${compileStubRes.returncode}: ${compileStubRes.stderr || 'Unknown error'}`);
        }

        // 步骤2：使用 em++ 链接为单文件 ES6 模块（CPU 渲染，不启用 WebGL/HTML5 胶水）
        const linkCommand = [
            'em++',
            objectFile, stubObject,
            '/usr/local/lib/tgfx.a',
            '-O0',
            '-Wno-error=version-check',  // 忽略版本检查错误
            // 方案A：允许 em++ 后处理（Binaryen/wasm-emscripten-finalize）完整执行
            '-sMODULARIZE=1',
            '-sEXPORT_ES6=1',
            '-sENVIRONMENT=web,worker',
            '-sALLOW_MEMORY_GROWTH=1',
            '-sASSERTIONS=0',
            '-sDISABLE_EXCEPTION_CATCHING=1',
            "-sEXPORTED_FUNCTIONS=['_" + (options.exportEntry || 'main') + "']",
            '-sEXPORTED_RUNTIME_METHODS=["cwrap","ccall","HEAPU8"]',
            '-sERROR_ON_UNDEFINED_SYMBOLS=0',
            '-o', outputJS
        ].join(' ');

        console.log('🔧 Step 2 - Link with em++ (JS runtime):', linkCommand);

        let linkResult;
        try {
            linkResult = await this.emceptionModule.run(linkCommand);
            console.log('✅ Linking completed, returncode:', linkResult.returncode);
        } catch (error) {
            console.error('❌ Linking failed:', error);
            throw new Error(`Linking failed: ${error.message}`);
        }

        if (linkResult.returncode !== 0) {
            throw new Error(`Linking failed with return code ${linkResult.returncode}: ${linkResult.stderr || 'Unknown error'}`);
        }

        // 读取生成的 .mjs 文本和 .wasm 二进制
        let jsModuleCode;
        let linkedWasm;
        try {
            const jsBytes = this.fsReadFile(outputJS);
            jsModuleCode = new TextDecoder('utf-8').decode(jsBytes);
            console.log('✅ JS module generated, length:', jsModuleCode.length);
            // 尝试读取同名 wasm（SINGLE_FILE 已关闭）
            const wasmPath = '/working/output.wasm';
            if (this.fsExists(wasmPath)) {
                linkedWasm = this.fsReadFile(wasmPath);
                console.log('✅ WASM generated, size:', linkedWasm.length);
            }
        } catch (error) {
            console.error('❌ Failed to read JS module:', error);
            throw new Error(`Failed to read output JS module: ${error.message}`);
        }

        return {
            success: true,
            jsModuleCode,
            wasmBinary: linkedWasm,
            exports: ['user_main','tgfx_init','tgfx_resize','tgfx_render','tgfx_present','tgfx_clear','tgfx_get_framebuffer_ptr','tgfx_get_width','tgfx_get_height'],
            stderr: (compileResult.stderr || '') + (linkResult.stderr || ''),
            stdout: (compileResult.stdout || '') + (linkResult.stdout || '')
        };
    }

    // 兼容 emception-core 与 emception-fixed 的文件系统访问
    fsWriteFile(path, content) {
        if (this.emceptionModule.writeFile) return this.emceptionModule.writeFile(path, content);
        return this.emceptionModule.fileSystem.writeFile(path, content);
    }
    fsReadFile(path) {
        if (this.emceptionModule.readFile) return this.emceptionModule.readFile(path);
        return this.emceptionModule.fileSystem.readFile(path);
    }
    fsExists(path) {
        if (this.emceptionModule.exists) return this.emceptionModule.exists(path);
        return this.emceptionModule.fileSystem.exists(path);
    }

    /**
     * 生成编译缓存ID
     */
    generateCompileId(sourceCode, options) {
        const content = sourceCode + JSON.stringify(options);

        // 使用简单哈希避免编码问题
        let hash = 0;
        for (let i = 0; i < content.length; i++) {
            const char = content.charCodeAt(i);
            hash = ((hash << 5) - hash) + char;
            hash = hash & hash; // 转换为32位整数
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

    /**
     * 获取编译器信息
     */
    getCompilerInfo() {
        if (!this.isInitialized) {
            return null;
        }

        return {
            type: 'EmceptionCore',
            initialized: this.isInitialized,
            options: this.compileOptions,
            cacheSize: this.compilationCache.size
        };
    }
}
