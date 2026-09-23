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
var fileName = '';
if(backend === 'webgpu'){
    fileName = (arch === 'wasm-mt' ? 'index-webgpu':'index-webgpu-st');
}else if(backend === 'webgl'){
    fileName = (arch === 'wasm-mt' ? 'index':'index-st');
}else if(backend === 'worker'){
    fileName = 'index-worker';
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

// The worker backend also needs its own bundle: the worker script runs in a separate realm and
// loads the wasm module there, so it cannot be part of the page bundle.
const extraConfigs = backend === 'worker' ? [
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
        input: `demo/${fileName}.ts`,
        output: {
            banner,
            file: `demo/${filePath}/${fileName}.js`,
            format: 'esm',
            sourcemap: true
        },
        plugins: plugins,
    },
    ...extraConfigs
];
