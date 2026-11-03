# TGFX Web Test

在浏览器 (WebGL + Emscripten) 环境下运行 TGFX 的 GTest 测试用例。

## 前置要求

- [emsdk](https://github.com/emscripten-core/emsdk) 4.0.15（`-sUSE_WEBGPU` 在 4.0.18 中被移除，4.0.15 是最后一个兼容版本。安装后无需每次手动 source，构建脚本会自动检测 `~/emsdk`、`/opt/emsdk`、`~/.emsdk`）
- Node.js >= 18
- Chrome（autotest 模式需要，Puppeteer 会自动下载）

> 如果 emsdk 不在上述默认路径，可设置环境变量 `EMSDK` 指向安装目录，或手动 `source <path>/emsdk_env.sh`。

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

原生通过 `accept_baseline.sh` 脚本完成 baseline 更新，内部依赖两个 target：

1. **`TGFXFullTest`**（无特殊宏）：运行所有测试，输出 `test/out/version.json` 记录当前各 key 的版本
2. **`UpdateBaseline`**（带 `UPDATE_BASELINE` + `GENERATE_BASELINE_IMAGES` 宏）：运行所有测试，无条件记录每个 key 的 MD5 到 cache，生成 baseline 图片，TearDown 时写入 `test/baseline/.cache/{backend}/md5.json`

Web 端 accept baseline 后，`accept_baseline.sh` 会自动同步 `version.json` 到 `test/baseline/.cache/webgl/` 和 `.cache/webgpu/`（如目录存在）。

### Web 端流程

Web 普通测试默认开启 `GENERATE_BASELINE_IMAGES`，只控制诊断图片输出，**不等于** `UPDATE_BASELINE`。普通构建显式关闭基准生成模式，只读参考缓存；浏览器还会校验 WASM 中的模式、后端和提交信息。

1. **构建时**：CMake 将以下文件 preload 进 WASM 虚拟 FS：
   - `test/baseline/version.json`（仓库基准版本）
   - `test/baseline/.cache/{backend}/md5.json` + `version.json`（本地 cache，如果存在）
   - 当前 git commit hash 写入 `HEAD` 文件

2. **运行时**：`Baseline::Compare` 对每个截图 key：
   - 计算当前截图的 MD5
   - 若 `test/out/` 中已有 `_base` 图且 cache 的 version、MD5 均匹配 → 直接复用，不重新生成
   - 否则生成新的 `_base` 图到 `test/baseline-out/`
   - 比较 `version.json`（仓库）与 `.cache/{backend}/version.json`（cache）的版本号：
     - **两边都有且版本号不同** → 跳过比较，视为通过（已接受变更）
     - **版本号相同或 key 不存在** → 做实际 MD5 比较，不匹配则测试失败

3. **运行后**：普通验证仅下载诊断产物：

   | zip | 内容 | 解压目标 |
   |-----|------|---------|
   | `baseline-out.zip` | 诊断图片、失败 MD5 和输出版本（非参考缓存） | `test/baseline-out/` |
   | `test-out.zip` | 其他测试输出（非空时生成） | `test/out/` |

   空目录不生成 ZIP，属于正常情况。未捕获的 WebGPU 错误、页面异常、下载丢失均返回失败，失败 MD5 不会被合并进参考缓存。

4. **重新构建**：独立准备的 `test/baseline/.cache/{backend}/md5.json` 和 `version.json` 被 preload 到 WASM。缺少参考时比较失败，不会静默通过。

### 更新 Baseline Cache

使用顶层 `update_baseline.sh` 从 main 分支生成 baseline cache：

```bash
# 从项目根目录执行
./update_baseline.sh USE_WEBGL
./update_baseline.sh USE_WEBGPU
```

该命令固定本地 `origin/main` 引用对应的提交（不会自动 fetch），然后：
1. 检查缓存的 MD5、版本、来源记录及内容摘要；缺任何一项都重新准备。
2. 将固定提交解包到 `web/test/build-reference-{backend}/run.*/source`，还原并校验 LFS 资源；不切分支、不 stash。依赖定义必须匹配已安装依赖，JS 绑定从该快照重新构建。
3. 用当前测试驱动构建该主干的库、测试及资源；参考构建和普通构建目录独立，普通运行产物不被覆盖。
4. 全量测试成功且没有 GPU 错误、跳过或下载失败后，校验专用 `reference-cache.zip`，将完整缓存事务发布到 `.cache/{backend}/`。`reference.json` 记录来源提交、执行数量及摘要；失败保留旧缓存，不合并部分结果。
5. 构建和诊断产物保留在该独立目录；`--skip-images` 仅跳过参考图片生成。

之后在当前分支运行 `npm run test:webgpu`（或 `test:webgl`）重新构建并比较，不能直接运行未重新打包参考输入的旧 WASM。

主干本身的编译、测试或 GPU 错误会阻止缓存发布，必须先修复主干，不能用当前分支输出代替。该流程仅重建主干参考，不改变仓库 `test/baseline/version.json`，不替代用户主动执行 `/accept-baseline` 接受截图变更。

如仅需刷新 cache 而不生成图像（CI 流水线使用此模式），追加 `--skip-images` 参数：

```bash
./update_baseline.sh USE_WEBGL --skip-images
```

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
