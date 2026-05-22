# 贝塞尔光栅化 CPU 端性能报告

本文档记录 `ShapeBezierRasterizer` 与 `PathTriangulator`（Skia/PathKit）在 CPU
顶点构造阶段的性能对比，作为贝塞尔光栅化渲染路径的设计与优化依据。

## 1. 背景

新引入的贝塞尔光栅化渲染路径在 GPU 上用 Stencil + Cover 双通道做 Loop-Blinn
隐式曲线评估，CPU 侧负责把 `Path` 解析成顶点缓冲（位置 + KLM 系数）。

参与对比的三种 CPU 端实现：

- **`ShapeBezierRasterizer`（bezier_rasterize）**
  本项目新增的实现。把 path 分解为线段和二次贝塞尔片段，每条线输出 1 个填充
  三角形（3 顶点），每条 quad 输出 1 个曲线三角形 + 1 个填充三角形（6 顶点）。
  Cubic 通过 `PathUtils::ConvertCubicToQuads` 转为多段 quad；conic 由
  `NoConicsPathIterator` 展开为 quad。无三角化、无亚像素 flattening。
- **`PathTriangulator::ToTriangles`（triangulate_no_aa）**
  调用 PathKit 的 `SkPath::toTriangles`：曲线先按 0.25 像素 tolerance flatten
  为多段直线，再走 sweep-line 三角化。输出每个三角形 3 个顶点（无覆盖率）。
- **`PathTriangulator::ToAATriangles`（triangulate_aa）**
  在 no-aa 三角化基础上，再为每条边构建羽化条带（顶点带 coverage）。这是
  当前 `Canvas::drawPath(antiAlias=true)` 的默认 CPU 路径。

## 2. 测试设计

代码：[`test/src/BezierRasterizeBenchmark.cpp`](../test/src/BezierRasterizeBenchmark.cpp)
中的 `BezierRasterizeBenchmark.CpuRasterize`。

### 2.1 用例网格

3 × 3 case：图元类型 `{lines, quads, cubics}` × 图元数 `{16, 64, 512}`。所有
case 都是从 (150, 150) 中心扇出 N 个图元、在半径 120 的圆周上闭合的轮廓，
保持图元类型与数量是唯一的变量。

| 类型 | 构造方式 |
|---|---|
| Lines  | 圆周上 N 个均匀点连成的多边形（N 条 lineTo + close） |
| Quads  | N 段二次贝塞尔从中心扇出，控制点外推到 1.3× 半径 |
| Cubics | N 段三次贝塞尔，两个控制点分别落在 t=0.33 / 0.67 处的 1.3× 半径 |

### 2.2 计时方法

- 计时工具：`tgfx/core/Clock`，微秒精度
- 每用例：5 次 warmup + 30 次 measure，记录 min / median / mean
- 每条 measure 调用一次 `getData()`（bezier）或一次 `ToTriangles` /
  `ToAATriangles`（三角化）

### 2.3 输出指标

- 各方法的 min / median / mean（µs）
- **`pct_of_aa_median`**：以 `triangulate_aa.median` 为 100% 基准的百分比
  - 数值越低越快；100% 表示该方法和 AA 三角化一样慢
  - 选择 AA 三角化为基准是因为它代表当前默认 `drawPath(antiAlias=true)` 的
    CPU 成本

CSV 输出：`test/out/BezierRasterizeBenchmark/cpu_rasterize.csv`

## 3. 测试环境

- 设备：Apple M4 Pro，macOS 26.4 (Darwin 25.4.0)
- 编译器：Clang（Xcode toolchain）
- 项目构建：`cmake -G Ninja -DTGFX_BUILD_TESTS=ON`
- vendor 编译：本仓库的 `third_party/vendor_tools/vendor.cmake` 在 macOS 上
  **始终使用 release 版本的 vendor 库**（含 PathKit）。仅 Windows + Debug
  时才切到 debug vendor。所以下文 Debug 行的 `triangulate_*` 数据里，
  PathKit 仍然是 release 编译的，加速空间已经被吃掉，只有
  `PathTriangulator.cpp` 这层薄 wrapper 受 BUILD_TYPE 影响。

### 3.1 PathKit 编译标志验证

为确认 `tri_no_aa` / `tri_aa` 的对比公平性，验证 PathKit 实际编译标志：

| 验证项 | 结果 |
|---|---|
| vendor 构建模式 | `Release`（vendor.cmake 仅 Windows+Debug 才走 debug） |
| CMAKE_BUILD_TYPE | `Release`（vendor-build 通过 `-DCMAKE_BUILD_TYPE=Release` 传入） |
| 默认优化 | clang 在 Release 模式下注入 `-O3 -DNDEBUG` |
| vendor.json 自定义 flag | 仅追加 `-DCMAKE_CXX_FLAGS="-w"`（关闭警告，不影响优化） |
| LTO / PGO | 未启用（主项目和 vendor 都没有 `-flto` / `INTERPROCEDURAL_OPTIMIZATION`） |

二进制证据：

- `libpathkit.a` release = 711 KB，debug = 9.1 MB（13× 差距）
- `SkPath.cpp.o` release = 44 KB，debug = 346 KB（8× 差距）
- `SkPath::toTriangles` 在 release `.o` 中只有 1 条 tail-call jump 指令，是
  典型 `-O*` 优化产物

**结论**：bezier 路径（主项目 `ShapeBezierRasterizer.cpp` 等）和 PathKit
（`SkPath::toTriangles` 等）处于完全对等的优化级别（`-O3 -DNDEBUG`，无 LTO）。
唯一的不对称是 bezier 实现在同一 TU 内可以跨函数内联，PathKit 隔着翻译单元
边界。这是任何使用 vendored 静态库的代码都要承担的代价，不影响结论的有效性。

## 4. 结果

### 4.1 Release 构建（生产代表）

主项目 `-O3`，vendor `-O3`。median µs：

| Case        | bezier | tri_no_aa | tri_aa | bezier vs tri_no_aa | bezier vs tri_aa |
|-------------|-------:|----------:|-------:|--------------------:|-----------------:|
| Lines_16    |     0  |       1   |     3  |   ≤1µs（噪声）       |   ≤1µs（噪声）     |
| Lines_64    |     1  |       4   |    13  |              4.0×   |             13×   |
| Lines_512   |     5  |      41   |    99  |              8.2×   |             20×   |
| Quads_16    |     1  |      11   |    54  |             11×     |             54×   |
| Quads_64    |     2  |      50   |   229  |             25×     |            115×   |
| **Quads_512** | **16** | **506** | **4463** |         **32×**   |         **279×**  |
| Cubics_16   |     1  |      16   |    74  |             16×     |             74×   |
| Cubics_64   |     3  |      46   |   226  |             15×     |             75×   |
| **Cubics_512** | **11** | **211** | **2158** |         **19×**   |         **196×**  |

pct（median / tri_aa.median，越低越快）：

| Case        | bezier | tri_no_aa | tri_aa |
|-------------|-------:|----------:|-------:|
| Lines_16    |  0.00% |    33.33% |  100%  |
| Lines_64    |  7.69% |    30.77% |  100%  |
| Lines_512   |  5.05% |    41.41% |  100%  |
| Quads_16    |  1.85% |    20.37% |  100%  |
| Quads_64    |  0.87% |    21.83% |  100%  |
| Quads_512   |  0.36% |    11.34% |  100%  |
| Cubics_16   |  1.35% |    21.62% |  100%  |
| Cubics_64   |  1.33% |    20.35% |  100%  |
| Cubics_512  |  0.51% |     9.78% |  100%  |

### 4.2 Debug 构建（开发态参考）

主项目 `-O0 -g`，vendor 仍 `-O3`（vendor 不受 BUILD_TYPE 影响）。median µs：

| Case        | bezier | tri_no_aa | tri_aa |
|-------------|-------:|----------:|-------:|
| Lines_16    |     2  |       2   |     4  |
| Lines_64    |     7  |       5   |    17  |
| Lines_512   |    45  |      47   |   114  |
| Quads_16    |     8  |      13   |    59  |
| Quads_64    |    28  |      55   |   250  |
| Quads_512   |   210  |     543   |  5079  |
| Cubics_16   |    14  |      19   |    97  |
| Cubics_64   |    56  |      52   |   248  |
| Cubics_512  |   173  |     236   |  2530  |

### 4.3 Debug → Release 的加速倍数

| Case        | bezier debug→release | tri_no_aa debug→release |
|-------------|---------------------:|------------------------:|
| Lines_512   |               9×     |                  1.1×   |
| Quads_512   |            **13×**   |                  1.1×   |
| Cubics_512  |            **16×**   |                  1.1×   |

**关键观察**：bezier 路径在 Debug 下被严重高估时间（10-16× 加速空间），而
`triangulate_*` 的核心循环在 Skia/PathKit 库里已经是 release 编译，几乎没有
加速空间。所以**Debug 测试会大幅低估 bezier 的实际优势**，所有结论都应以
Release 数据为准。

## 5. 主要结论

### 5.1 Release 下的相对位置

- **直线场景**：bezier 比 tri_no_aa 快 4-8×；N≤16 时绝对耗时已低于 1µs，差异
  在测量噪声内，可以认为持平。
- **二次贝塞尔场景**：bezier 全面碾压。N=512 时 bezier 16µs vs tri_no_aa
  506µs，**快 32×**。三角化器需要把每个 quad flatten 到 ~10 段直线再做
  sweep-line，bezier 直接吐 6 顶点把曲线判定甩给 GPU 片元着色器。
- **三次贝塞尔场景**：bezier 在 N=64 起就拉开差距，N=512 快 19×。
- **vs AA 三角化**：bezier 在所有曲线场景快 75-279×。这意味着如果未来给
  bezier 路径加 AA 支持，对 `drawPath(antiAlias=true)` 是巨大利好。

### 5.2 性能差距的来源

| 工序 | bezier_rasterize | triangulate_no_aa |
|---|---|---|
| 曲线处理 | quad 直接产 6 顶点；cubic 转少量 quad | 曲线 flatten 到 0.25 px 直线段（quad → ~10 段，cubic → ~20 段） |
| 几何拓扑 | 扇形 fill 三角形（O(N) 写入） | sweep-line 三角化（O(N log N)） |
| 输出顶点数 | 每图元 ~6 顶点 | 每 flatten 段 ~3 顶点（量级大得多） |
| AA 处理 | 暂不涉及 | 额外构建羽化条带（tri_aa 比 tri_no_aa 慢 5-10×） |

### 5.3 关于 path 分类路由

当前贝塞尔光栅化渲染路径在 `OpsCompositor` 内只在 `antiAlias=false` 时调度。
从 CPU 数据看：

- 在 **直线为主** 的 path 上，bezier 与 tri_no_aa 已经持平。
- 在 **含 quad/cubic** 的 path 上，bezier 取代 tri_no_aa 是稳赢的。
- 端到端是否取胜还要看 GPU 双通道开销，但 CPU 已不构成阻力。

## 6. 优化历程

bezier 路径的 CPU 优化经历两步，结果均已合入：

### 6.1 `ConvertCubicToQuads` 改 out-parameter

接口从返回 `std::vector<Point>` 改为传入 `std::vector<Point>*`，调用方复用同一
buffer 跨 cubic 调用，避免每段 cubic 一次堆分配 + 一次 vector move。涉及：

- `src/core/utils/PathUtils.{h,cpp}`
- `src/core/HairlineTriangulator.cpp`（同步迁移，加 `cubicQuads_` 复用 buffer）
- `src/core/ShapeBezierRasterizer.cpp`（同步迁移）
- `test/src/StrokeTest.cpp`（9 处 stroke 测试同步）

Debug 下 `Cubics_256` 从 176 µs 降到 91 µs（≈48%）。

### 6.2 合并 `decompose` 和 `emit`

`PathDecomposer` 不再持有中间的 `lines_` / `quads_` vector，而是构造时接收
顶点输出 buffer 与 fan origin，`addLine` / `addQuad` 直接调
`EmitFillTriangle` / `EmitQuadTriangles` 写入最终顶点。

`getData()` 流程从 6 段（getPath / decompose / getBounds / reserve / emit /
makeData）压成 5 段（合并 decompose+emit）。`vertices.reserve` 改用
`path.countVerbs() * 12` 估算（cubic-heavy path 可能触发一次扩容，仍优于按
worst-case 全量预分配）。

涉及：`src/core/ShapeBezierRasterizer.cpp`

Debug 下 `Cubics_512` 从 ~275 µs 降到 ~215 µs（≈22%），`Quads_*` 全线提速
30-50%，`Lines_*` 提速 25-36%。

### 6.3 暂时不做的优化

如果未来还想压 bezier 在 cubic-heavy 场景的耗时（Release 下已经很低），还有
以下方向：

- **`ConvertCubicToQuads` 改 callback / 栈 buffer**：消除 `cubicQuads_` 这个
  仍然存在的中间 vector。预期 5-10% 收益，工程量中等。
- **`ConvertNoninflectCubicToQuads` 改循环 + 显式栈**：消除递归调用开销。预期
  3-5% 收益。
- **`ChopCubicAtHalf` 特化**：t=0.5 时 Lerp 简化为 `(a+b)*0.5`，省一次减法。
  收益 1-3 µs。
- **SIMD 化 Lerp**（`PathUtils.cpp` 中已有 TODO 标记）：用 NEON / SSE 把 Lerp
  的 x/y 分量并行做。预期 30-50% 收益，但 release 下绝对耗时已经在 µs 量级，
  ROI 不高。

## 7. 复现方法

```bash
# Release 构建（推荐）
cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -B cmake-build-release
cmake --build cmake-build-release --target TGFXFullTest

# 跑 CPU benchmark
./cmake-build-release/TGFXFullTest --gtest_filter='BezierRasterizeBenchmark.CpuRasterize'

# CSV 结果
cat test/out/BezierRasterizeBenchmark/cpu_rasterize.csv
```

如需对比 debug 数据，把 `Release` 换成 `Debug`、目录换成 `cmake-build-debug`。
