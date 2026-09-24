import esbuild from 'rollup-plugin-esbuild';
import resolve from '@rollup/plugin-node-resolve';
import commonJs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';
import path from "path";
import {readFileSync} from "node:fs";

const fileHeaderPath = path.resolve(__dirname, '../../.idea/fileTemplates/includes/TGFX File Header.h');
const banner = readFileSync(fileHeaderPath, 'utf-8');

const arch = process.env.ARCH;
const backend = process.env.BACKEND;
var filePath = (arch === 'wasm-mt' ? 'wasm-mt' : 'wasm');
// The page source to bundle, and the name the bundle is written as. The two differ for the WebGPU
// worker backend, which reuses the WebGL worker page source: the page tells the two apart through
// its own URL, which the HTML page sets, so only the output name has to carry the backend.
var inputName = '';
var outputName = '';
if(backend === 'webgpu'){
    inputName = outputName = (arch === 'wasm-mt' ? 'index-webgpu':'index-webgpu-st');
}else if(backend === 'webgl'){
    inputName = outputName = (arch === 'wasm-mt' ? 'index':'index-st');
}else if(backend === 'worker'){
    inputName = outputName = 'index-worker';
}else if(backend === 'worker-webgpu'){
    inputName = 'index-worker';
    outputName = 'index-worker-webgpu';
}

const plugins = [
    esbuild({tsconfig: "tsconfig.json", minify: false}),
    json(),
    resolve(),
    commonJs(),
    {
        name: 'preserve-import-meta-url',
        resolveImportMeta(property, options) {
            // Preserve the original behavior of `import.meta.url`.
            if (property === 'url') {
                return 'import.meta.url';
            }
            return null;
        },
    },
];

// The worker backends also need their own bundle: the worker script runs in a separate realm and
// loads the wasm module there, so it cannot be part of the page bundle.
const extraConfigs = (backend === 'worker' || backend === 'worker-webgpu') ? [
    {
        input: 'demo/worker-render.ts',
        output: {
            banner,
            file: `demo/${filePath}/worker-render.js`,
            format: 'esm',
            sourcemap: true
        },
        plugins: plugins,
    }
] : [];

export default [
    {
        input: `demo/${inputName}.ts`,
        output: {
            banner,
            file: `demo/${filePath}/${outputName}.js`,
            format: 'esm',
            sourcemap: true
        },
        plugins: plugins,
    },
    ...extraConfigs
];
