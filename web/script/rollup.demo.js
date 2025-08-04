import esbuild from 'rollup-plugin-esbuild';
import resolve from '@rollup/plugin-node-resolve';
import commonJs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';
import path from "path";
import {readFileSync} from "node:fs";

const fileHeaderPath = path.resolve(__dirname, '../../.idea/fileTemplates/includes/TGFX File Header.h');
const banner = readFileSync(fileHeaderPath, 'utf-8');

const arch = process.env.ARCH;
var fileName = (arch === 'wasm-mt'? 'index': 'index-st');
var filePath = (arch === 'wasm-mt'? 'wasm-mt': 'wasm');

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
    }
];
