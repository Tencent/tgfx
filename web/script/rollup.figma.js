import esbuild from 'rollup-plugin-esbuild';
import resolve from '@rollup/plugin-node-resolve';
import commonJs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';
import glob from 'glob';

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

const arch = process.env.ARCH;

const plugins = [
    esbuild({tsconfig: "tsconfig.json", minify: false}),
    json(),
    resolve(),
    commonJs(),
];

const inputFiles = glob.sync('figma/ts/*.ts');

export default [
    {
        input: inputFiles,
        output: {
            banner,
            // file: `figma/js/figma/ts/*.js`,
            dir: `figma/js/figma/ts`,
            format: 'esm',
            sourcemap: true
        },
        plugins: plugins,
    }
];
