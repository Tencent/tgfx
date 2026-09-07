---
marp: true
theme: default
paginate: true
size: 16:9
style: |
  :root {
    --navy: #123c6d;
    --blue: #2874a6;
    --green: #239b56;
    --red: #c0392b;
    --ink: #17202a;
    --muted: #667985;
    --line: #d9e2ec;
    --paper: #ffffff;
    --bg: #f6f8fa;
  }
  section {
    font-family: -apple-system, "PingFang SC", "Microsoft YaHei", sans-serif;
    background: var(--bg);
    color: var(--ink);
    padding: 52px 64px 44px;
  }
  section.lead {
    background: linear-gradient(135deg, #102a43 0%, #1b4f72 100%);
    color: white;
    justify-content: center;
  }
  section.lead h1 {
    color: white;
    font-size: 46px;
    text-align: left;
  }
  section.lead p {
    color: #d9e7f2;
    font-size: 24px;
  }
  h1 {
    color: var(--navy);
    font-size: 34px;
    line-height: 1.25;
    margin: 0 0 24px;
    font-weight: 800;
  }
  h1 strong {
    color: var(--red);
  }
  h2, h3 {
    color: var(--navy);
  }
  .rule {
    height: 5px;
    width: 86px;
    background: var(--blue);
    border-radius: 3px;
    margin: -12px 0 26px;
  }
  .grid2 {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 28px;
    align-items: stretch;
  }
  .panel {
    background: var(--paper);
    border-top: 5px solid var(--blue);
    border-radius: 10px;
    padding: 22px 26px;
    min-height: 320px;
  }
  .panel.green { border-top-color: var(--green); }
  .panel.red { border-top-color: var(--red); }
  .panel.green strong, .panel.green h2, .panel.green h3 { color: var(--green); }
  .panel.red strong, .panel.red h2, .panel.red h3 { color: var(--red); }
  .panel.neutral { border-top-color: var(--blue); }
  .flow {
    margin-top: 34px;
    font-size: 29px;
    line-height: 1.8;
    font-weight: 800;
    text-align: center;
    color: var(--navy);
  }
  .flow .small {
    display: block;
    margin-top: 18px;
    font-size: 19px;
    line-height: 1.45;
    font-weight: 500;
    color: var(--muted);
  }
  .arrow {
    color: var(--blue);
    padding: 0 10px;
  }
  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 19px;
    background: var(--paper);
    border-radius: 8px;
    overflow: hidden;
  }
  th {
    background: var(--navy);
    color: white;
    font-weight: 700;
  }
  th, td {
    padding: 12px 14px;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  tr:last-child td {
    border-bottom: 0;
  }
  .hit {
    color: var(--green);
    font-weight: 800;
  }
  .miss {
    color: var(--red);
    font-weight: 800;
  }
  .caption {
    margin-top: 14px;
    color: var(--muted);
    font-size: 17px;
    line-height: 1.45;
  }
  .conclusion {
    position: absolute;
    left: 64px;
    right: 64px;
    bottom: 40px;
    background: #eaf2f8;
    border-left: 6px solid var(--blue);
    border-radius: 8px;
    padding: 14px 18px;
    color: var(--navy);
    font-size: 21px;
    font-weight: 800;
  }
  code, pre {
    font-family: Menlo, Consolas, monospace;
    font-size: 16px;
  }
---

<!-- _class: lead -->

# 运行时为什么需要提前准备 Shader

TGFX Shader 预编译：问题、约束与方案

---

# 每次新的绘制结构都会生成一份完整 VS/FS

<div class="rule"></div>

<div class="flow">
图片纹理 <span class="arrow">↓</span><br>
纹理采样 <span class="arrow">↓</span><br>
颜色矩阵 <span class="arrow">↓</span><br>
透明度阈值 <span class="arrow">↓</span><br>
完整 FS 源码
<span class="small">几何信息同时生成顶点 Shader；普通 SrcOver 由固定功能混合完成。</span>
</div>

```cpp
emitGeometry();
emitColorSteps();
emitTransfer();
```

<div class="conclusion">
结构决定源码；颜色矩阵和阈值数值只作为运行时参数。
</div>

---

# 参数变化不会生成 Shader，接口变化会

<div class="rule"></div>

<div class="grid2">

<div class="panel green">

## 仅参数变化

```text
Matrix M1 → M2
Threshold 0.25 → 0.50
```

**Shader 不变**

运行时只更新 Matrix 和 Threshold。

</div>

<div class="panel red">

## 增加设备遮罩

```glsl
uniform sampler2D TextureSampler;
uniform sampler2D MaskTextureSampler;
```

**FS 资源接口变化**

需要另一份 Shader。

</div>

</div>

<div class="conclusion">
首次成本来自结构变化，不来自颜色参数。
</div>

---

# 生成源码后，还要完成后端编译和 Pipeline 创建

<div class="rule"></div>

<div class="flow">
VS/FS <span class="arrow">→</span> Backend 编译 <span class="arrow">→</span> Shader Module <span class="arrow">→</span> Pipeline <span class="arrow">→</span> Draw
<span class="small">Pipeline = Shader + 附件格式 + 混合状态 + 采样数 + 深度模板 + 顶点布局</span>
</div>

| 后端 | Shader 工作 | Pipeline 工作 |
|---|---|---|
| OpenGL | 驱动编译 VS/FS | 链接 GL Program |
| Vulkan | 创建 Shader Module | 创建图形 Pipeline |
| Metal | 取得 VS/FS Function | 创建 Render Pipeline |
| WebGPU | 创建 Shader Module | 创建 Render Pipeline |

<div class="conclusion">
AOT 前移 Shader，不冻结运行时 Pipeline 状态。
</div>

---

# 缓存只能解决重复创建，不能解决首次创建

<div class="rule"></div>

| 输入变化 | 缓存键 | 结果 |
|---|---:|---|
| Matrix / Threshold 数值变化 | 不变 | <span class="hit">命中</span> |
| Sample Count 1 → 4 | 改变 | <span class="miss">未命中</span> |
| 增加 DeviceMask | 改变 | <span class="miss">未命中</span> |

<div class="caption">
缓存键同时包含静态结构、资源接口和关键绘制状态；矩阵与阈值作为运行时参数，通常不进入缓存键。
</div>

<div class="conclusion">
新结构、新接口和新状态仍产生首次成本。
</div>

---

# 运行时组合开放，构建期资产必须有限

<div class="rule"></div>

<div class="flow">
公开绘制组合 <span class="arrow">↓</span><br>
缓存无法提前覆盖 <span class="arrow">↓</span><br>
构建期必须准备有限 Shader <span class="arrow">↓</span><br>
<strong>谁定义候选范围？</strong>
</div>

| 候选方式 | 对 TGFX 是否适用 |
|---|---|
| 引擎固定能力 | 不适用：收窄公开输入 |
| 调用方申报组合 | 不适用：下游不提供清单 |
| 项目资产 Cook | 不适用：构建时看不到项目资产 |
| TGFX 库内规则 | 必须由库内建立 |

<div class="conclusion">
AOT 的关键不是提前编译，而是定义有限且可覆盖的 Shader 集合。
</div>
