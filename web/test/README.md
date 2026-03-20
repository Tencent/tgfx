# TGFX Web Test

在浏览器 (WebGL + Emscripten) 环境下运行 TGFX 的 GTest 测试用例。

## 前置要求

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)（确保 `emcmake` 可用）
- Node.js >= 18
- Chrome（autotest 模式需要，Puppeteer 会自动下载）

## 快速开始

```bash
cd web/test
npm install
```

### 自动化测试（推荐）

一键编译 + 运行 + 收集结果：

```bash
npm run autotest
```

该命令会：
1. 编译 WASM
2. 启动本地 HTTP 服务
3. 用 Puppeteer (headless Chrome) 打开测试页面
4. 等待所有测试完成
5. 下载截图产物并解压到对应目录
6. 输出测试结果和退出码

### 手动测试

适合调试或查看浏览器控制台：

```bash
npm run build       # 编译
npm run serve       # 启动 http-server (端口 8082)
```

然后浏览器打开 `http://127.0.0.1:8082/index.html`，测试自动运行，完成后会触发 zip 下载。

### Debug 构建

```bash
npm run build:debug   # 带 source map 的 Debug 构建
```

## 截图 Baseline 流程

Web 端的 baseline 机制与原生主干一致，核心流程相同，但因 WASM 运行在浏览器虚拟 FS 中，多了一层文件搬运。

### 原生主干流程（对照）

原生通过两个 target 完成 baseline：

1. **`UpdateBaseline`**（带 `UPDATE_BASELINE` + `GENERATE_BASELINE_IMAGES` 宏）：运行所有测试，生成 baseline 图片到 `test/baseline-out/`，同时更新本地 cache（`test/baseline/.cache/md5.json`）
2. **`TGFXFullTest`**：用当前代码运行测试，对比 `test/baseline/version.json`（仓库）与 `test/baseline/.cache/version.json`（本地 cache）的版本号，决定是否做实际图片比较

`accept_baseline.sh` 的完整流程：
1. 构建并运行 `TGFXFullTest` → 生成 `test/out/version.json`
2. 拷贝 `test/out/version.json` 到 `test/baseline/version.json`
3. 构建并运行 `UpdateBaseline` → 生成 baseline 图片 + 更新 cache

### Web 端流程

Web 端构建时始终开启 `GENERATE_BASELINE_IMAGES`，等效于原生的 `UpdateBaseline` 行为：

1. **构建时**：CMake 将以下文件 preload 进 WASM 虚拟 FS：
   - `test/baseline/version.json`（仓库基准版本）
   - `test/baseline/.cache-web/md5.json` + `version.json`（本地 cache，如果存在）
   - 当前 git commit hash 写入 `HEAD` 文件

2. **运行时**：`Baseline::Compare` 对每个截图 key：
   - 计算当前截图的 MD5
   - 若 `test/out/` 中已有 `_base` 图且 cache 的 version、MD5 均匹配 → 直接复用，不重新生成
   - 否则生成新的 `_base` 图到 `test/baseline-out/`
   - 比较 `version.json`（仓库）与 `.cache-web/version.json`（cache）的版本号：
     - **两边都有且版本号不同** → 跳过比较，视为通过（已接受变更）
     - **版本号相同或 key 不存在** → 做实际 MD5 比较，不匹配则测试失败

3. **运行后**：JS 从虚拟 FS 打包下载三个 zip：

   | zip | 内容 | 解压目标 |
   |-----|------|---------|
   | `baseline-out.zip` | baseline 截图 (`_base.webp`) + `md5.json` + `version.json` | `test/baseline-out/` |
   | `test-out.zip` | 失败截图 + `version.json` | `test/out/` |
   | `cache-web.zip` | `md5.json` + `version.json` | `test/baseline/.cache-web/` |

4. **下次构建**：`test/baseline/.cache-web/` 中的 cache 被 preload 回 WASM，实现跨次运行的 baseline 比对。

### 更新 Baseline Cache

使用 `update_baseline.sh` 从 main 分支生成 baseline cache：

```bash
cd web/test
bash update_baseline.sh
```

该脚本会：
1. 暂存当前分支的未提交改动（如有）
2. 切到 main 分支，以 `UPDATE_BASELINE` 模式构建并运行 web 测试
3. 生成 baseline cache 到 `test/baseline/.cache-web/`
4. 切回当前分支，恢复暂存改动

之后在当前分支运行 `npm run autotest` 即可对比 main baseline 验证。

## 命令一览

| 命令 | 说明 |
|------|------|
| `npm run build` | Release 编译 |
| `npm run build:debug` | Debug 编译（带 source map） |
| `npm run serve` | 启动 HTTP 服务（端口 8082） |
| `npm run test` | 编译 + 启动服务（手动打开浏览器） |
| `npm run autotest` | 全自动：编译 + Puppeteer 运行 + 收集结果 |
| `npm run clean` | 清理构建产物 |

## 平台差异

以下测试在 Web 平台被排除：

- `ReadPixelsTest.NativeCodec` — 依赖平台原生 image codec
- `ReadPixelsTest.JpegCodec` — Emscripten 环境下无 JPEG codec
- `PDFExportTest` — PDF 模块未在 Web 平台启用
