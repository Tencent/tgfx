---
description: 项目编译与测试相关配置
alwaysApply: true
---

## 编译验证

修改代码后，使用以下命令验证编译。必须传递 `-DTGFX_BUILD_TESTS=ON` 以启用所有模块（layers、svg、pdf 等）。

```bash
cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest
```

## 测试框架

- 测试用例位于 `test/src/`，基于 Google Test 框架
- 测试代码可通过编译参数访问所有 private 成员，无需 friend class
- 运行测试：按上述编译验证步骤构建并执行 `TGFXFullTest`
- 测试命令返回非零退出码表示测试失败，这是正常行为，不要重复执行同一命令
## 截图测试

- 使用 `Baseline::Compare(pixels, key)` 比较截图，key 格式为 `{folder}/{name}`，例如 `CanvasTest/Clip`
- 截图输出到 `test/out/{folder}/{name}.webp`，基准图为同目录下 `{name}_base.webp`
- 比较机制：对比 `test/baseline/version.json`（仓库）与 `test/baseline/.cache/version.json`（本地）中同一 key 的版本号，一致时才进行基准图对比，不一致则跳过返回成功，以此接受截图变更

**!! IMPORTANT - 截图基准变更限制**：
- **NEVER** 自动接受截图基准变更，包括禁止自动运行 `UpdateBaseline` target、禁止修改或覆盖 `version.json` 文件
- 必须经过用户确认后运行 `accept_baseline.sh` 脚本来接受变更

### 截图构造规范

截图内容必须居中显示，四边边距约 50 像素（误差 1 像素内可接受）。所有字号、坐标、矩阵等数值尽可能使用整数，避免小数点，以确保清晰度。

**获取精确边界的方法**（必须通过打印获取，不可凭感觉估算）：
- 图层：`layer->getBounds(nullptr, true)`
- Shape：`shape->getPath().getBounds()`（Shape 必须通过 Path 获取边界）
- Path：`path.getBounds()`
- TextBlob：`textBlob->getTightBounds()`
- 其他情况：先输出截图，用工具读取图片计算实际像素边界

根据获取的边界调整位置和 Surface 尺寸，使内容居中。验证正确后移除临时打印语句。
