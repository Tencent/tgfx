import resolve from '@rollup/plugin-node-resolve';
import commonJs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';
import {terser} from 'rollup-plugin-terser';
import esbuild from 'rollup-plugin-esbuild';

const banner = `/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
`;

const umdConfig = {
    input: 'src/binding.ts',
    output: [
        {
            name: 'tgfx',
            banner,
            format: 'umd',
            exports: 'named',
            sourcemap: true,
            file: 'lib/tgfx.js',
        },
    ],
    plugins: [
        esbuild({
            tsconfig: 'tsconfig.json',
            minify: false,
        }),
        json(),
        resolve(),
        commonJs(),
    ],
};

const umdMinConfig = {
    input: 'src/binding.ts',
    output: [
        {
            name: 'tgfx',
            banner,
            format: 'umd',
            exports: 'named',
            sourcemap: true,
            file: 'lib/tgfx.min.js',
        },
    ],
    plugins: [
        esbuild({
            tsconfig: 'tsconfig.json',
            minify: false,
        }),
        json(),
        resolve(),
        commonJs(),
        terser(),
    ],
};

export default [
    umdConfig,
    umdMinConfig,
    {
        input: 'src/binding.ts',
        output: [
            {banner, file: 'lib/tgfx.esm.js', format: 'esm', sourcemap: true},
            {banner, file: 'lib/tgfx.cjs.js', format: 'cjs', exports: 'auto', sourcemap: true},
        ],
        plugins: [
            esbuild({
                tsconfig: 'tsconfig.json',
                minify: false
            }),
            json(),
            resolve(),
            commonJs()
        ],
    },
];
