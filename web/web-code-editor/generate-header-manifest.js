#!/usr/bin/env node

/**
 * 生成头文件清单
 * 扫描wasm-assets/headers目录，生成所有头文件的列表
 */

const fs = require('fs');
const path = require('path');

function scanDirectory(dir, basePath = '') {
    const files = [];
    const items = fs.readdirSync(dir);
    
    for (const item of items) {
        const fullPath = path.join(dir, item);
        const relativePath = path.join(basePath, item);
        
        if (fs.statSync(fullPath).isDirectory()) {
            // 递归扫描子目录
            files.push(...scanDirectory(fullPath, relativePath));
        } else if (item.endsWith('.h')) {
            // 添加头文件
            files.push(relativePath.replace(/\\/g, '/'));
        }
    }
    
    return files;
}

// 扫描头文件目录
const headersDir = path.join(__dirname, 'wasm-assets/headers');
const headerFiles = scanDirectory(headersDir);

console.log(`找到 ${headerFiles.length} 个头文件`);

// 生成清单文件
const manifest = {
    generated: new Date().toISOString(),
    count: headerFiles.length,
    files: headerFiles.sort()
};

// 写入清单文件
const manifestPath = path.join(__dirname, 'wasm-assets/header-manifest.json');
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));

console.log(`头文件清单已生成: ${manifestPath}`);

// 同时生成JavaScript模块
const jsContent = `// 自动生成的头文件清单
// 生成时间: ${manifest.generated}

export const HEADER_FILES = ${JSON.stringify(headerFiles, null, 2)};

export const HEADER_COUNT = ${headerFiles.length};

export default HEADER_FILES;
`;

const jsPath = path.join(__dirname, 'wasm-assets/header-manifest.js');
fs.writeFileSync(jsPath, jsContent);

console.log(`JavaScript清单已生成: ${jsPath}`);
console.log('完成!');