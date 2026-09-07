// Generates the 29-page TGFX AOT composite-draw architecture deck.
// Run: node generate-aot-architecture.js
const pptxgen = require("pptxgenjs");

const pptx = new pptxgen();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "TGFX Graphics Team";
pptx.company = "Tencent";
pptx.subject = "TGFX AOT composite-draw architecture";
pptx.title = "TGFX-AOT-复合绘制架构";
pptx.lang = "zh-CN";
pptx.theme = { headFontFace: "PingFang SC", bodyFontFace: "PingFang SC", lang: "zh-CN" };

const W = 13.333;
const M = 0.62;
const FONT = "PingFang SC";
const MONO = "Menlo";
const C = {
  navy: "123C6D",
  navyDark: "0C2A4E",
  blue: "2B6CB0",
  blueSoft: "EAF1F8",
  green: "1E8E4E",
  greenSoft: "E8F4EC",
  red: "C0392B",
  redSoft: "FAEAE7",
  orange: "D97B29",
  orangeSoft: "FCF0E3",
  ink: "1B2A3A",
  text: "33475C",
  muted: "647788",
  line: "D5DEE8",
  bg: "F7FAFC",
  white: "FFFFFF",
  codeBg: "10243C",
  codeText: "DCE9F5",
  codeDim: "8FB3D9",
};

let page = 0;

function addText(slide, value, x, y, w, h, o = {}) {
  slide.addText(value, {
    x, y, w, h,
    margin: o.margin === undefined ? 0 : o.margin,
    fontFace: o.mono ? MONO : FONT,
    fontSize: o.size || 13,
    color: o.color || C.text,
    bold: o.bold || false,
    italic: o.italic || false,
    align: o.align || "left",
    valign: o.valign || "top",
    fit: "shrink",
    lineSpacingMultiple: o.lineSpacing || 1.06,
    breakLine: false,
  });
}

function box(slide, x, y, w, h, fill = C.white, line = C.line, lw = 1) {
  slide.addShape("rect", { x, y, w, h, fill: { color: fill }, line: { color: line, width: lw } });
}

function vArrow(slide, cx, y, h, color = C.blue) {
  slide.addShape("line", {
    x: cx, y, w: 0, h,
    line: { color, width: 1.6, endArrowType: "triangle" },
  });
}

function hArrow(slide, x, cy, w, color = C.blue) {
  slide.addShape("line", {
    x, y: cy, w, h: 0,
    line: { color, width: 1.6, endArrowType: "triangle" },
  });
}

function chip(slide, value, x, y, w, h, o = {}) {
  box(slide, x, y, w, h, o.fill || C.blueSoft, o.fill || C.blueSoft, 0);
  addText(slide, value, x + 0.06, y, w - 0.12, h, {
    size: o.size || 11, color: o.color || C.navy, bold: o.bold === undefined ? true : o.bold,
    mono: o.mono || false, align: "center", valign: "mid",
  });
}

function flowNode(slide, value, x, y, w, h, o = {}) {
  box(slide, x, y, w, h, o.fill || C.white, o.line || C.navy, o.lw || 1.4);
  addText(slide, value, x + 0.08, y, w - 0.16, h, {
    size: o.size || 13, color: o.color || C.navy, bold: o.bold === undefined ? true : o.bold,
    align: "center", valign: "mid", mono: o.mono || false, lineSpacing: 1.04,
  });
}

function codeBlock(slide, code, x, y, w, h, o = {}) {
  box(slide, x, y, w, h, C.codeBg, C.codeBg, 0);
  addText(slide, code, x + 0.16, y + 0.06, w - 0.32, h - 0.12, {
    size: o.size || 11.5, color: C.codeText, mono: true, valign: "mid", lineSpacing: 1.22,
  });
}

function addTable(slide, rows, x, y, w, colW, o = {}) {
  slide.addTable(rows, {
    x, y, w, colW,
    border: { type: "solid", color: C.line, pt: 0.8 },
    fill: { color: C.white },
    fontFace: FONT,
    fontSize: o.size || 12,
    color: C.text,
    valign: "mid",
    margin: [3, 6, 3, 6],
    rowH: o.rowH,
    autoPage: false,
  });
}

function headRow(cells) {
  return cells.map((t) => ({
    text: t,
    options: { fill: { color: C.navy }, color: "FFFFFF", bold: true, fontSize: 12 },
  }));
}

function conclude(slide, text, o = {}) {
  box(slide, M, 6.66, 0.07, 0.48, o.color || C.navy, o.color || C.navy, 0);
  box(slide, M + 0.07, 6.66, W - 2 * M - 0.07, 0.48, C.blueSoft, C.blueSoft, 0);
  addText(slide, text, M + 0.26, 6.66, W - 2 * M - 0.42, 0.48, {
    size: 12.5, color: C.navy, bold: true, valign: "mid",
  });
}

function addBase(num, kicker, title) {
  const slide = pptx.addSlide();
  page++;
  slide.background = { color: C.bg };
  box(slide, 0, 0, W, 0.09, C.navy, C.navy, 0);
  addText(slide, kicker, M, 0.26, 8.5, 0.22, { size: 10, color: C.muted, bold: true, mono: true });
  chip(slide, num, W - M - 0.52, 0.24, 0.52, 0.26, { fill: C.navy, color: "FFFFFF", size: 11, mono: true });
  addText(slide, title, M, 0.5, W - 2 * M - 0.7, 0.62, {
    size: title.length > 30 ? 24 : 27, color: C.navy, bold: true,
  });
  box(slide, M, 1.24, 1.05, 0.05, C.blue, C.blue, 0);
  slide.addShape("line", { x: M, y: 7.28, w: W - 2 * M, h: 0, line: { color: C.line, width: 0.8 } });
  addText(slide, "TGFX AOT", M, 7.32, 2.2, 0.14, { size: 8.5, color: C.muted, mono: true });
  addText(slide, String(page).padStart(2, "0") + " / 29", W - M - 1.2, 7.32, 1.2, 0.14, {
    size: 8.5, color: C.muted, mono: true, align: "right",
  });
  return slide;
}

function notes(slide, text) {
  slide.addNotes(text);
}

// ---------------------------------------------------------------- 01
{
  const s = addBase("01", "成本来源 / COST ORIGIN", "每个新的绘制结构都会生成一份完整VS/FS");
  const fx = M, fw = 5.6;
  const steps = ["图片纹理", "颜色矩阵", "透明度阈值", "完整 FS 源码"];
  const fills = [C.white, C.white, C.white, C.navy];
  const colors = [C.navy, C.navy, C.navy, "FFFFFF"];
  steps.forEach((t, i) => {
    const y = 1.62 + i * 1.13;
    flowNode(s, t, fx + 0.7, y, fw - 1.4, 0.72, { fill: fills[i], color: colors[i], line: i === 3 ? C.navy : C.line, size: 14 });
    if (i < 3) vArrow(s, fx + fw / 2, y + 0.74, 0.36);
  });
  chip(s, "普通 SrcOver 由固定功能混合完成，不进入 FS", fx + 0.35, 6.06, fw - 0.7, 0.36, { fill: C.greenSoft, color: C.green, size: 11 });
  const rx = 6.7, rw = W - M - rx;
  addText(s, "源码按固定顺序拼接", rx, 1.52, rw, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "emitAndInstallGeoProc(&color, &coverage);\n" +
    "emitAndInstallFragProcessors(&color, &coverage);\n" +
    "emitAndInstallXferProc(color, coverage);",
    rx, 1.88, rw, 1.0);
  addText(s, "生成的 FS（节选）", rx, 3.08, rw, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "vec4 color = texture(TextureSampler_0, coord);\n" +
    "color = uColorMatrix * color + uColorBias;\n" +
    "if (color.a < uThreshold) { discard; }\n" +
    "fragColor = color;",
    rx, 3.44, rw, 1.28);
  addText(s, "矩阵与阈值是 uniform：数值变化时源码逐字节不变。", rx, 4.92, rw, 0.6, { size: 12, color: C.muted });
  conclude(s, "结构决定源码；颜色矩阵和阈值数值只作为运行时参数。");
  notes(s, "源码依据：src/gpu/ProgramBuilder.cpp:41-49 的真实拼接顺序；src/gpu/shaders/glsl/level1/texture_fill.frag 主流程。删减说明：FS 片段为三段流程的示意节选，真实文件还含 subset clamp、coverage 与 XP 阶段；正文省略 GeometryProcessor/FragmentProcessor/XferProcessor 类名。适用边界：普通 SrcOver 落入 attachment 固定功能混合，emitAndInstallXferProc 不等于所有混合都在 shader 内计算。");
}

// ---------------------------------------------------------------- 02
{
  const s = addBase("02", "成本来源 / COST ORIGIN", "参数变化不会生成Shader，接口变化会");
  const colW = (W - 2 * M - 0.4) / 2;
  const lx = M, rx = M + colW + 0.4;
  box(s, lx, 1.5, colW, 4.9, C.white, C.green, 1.6);
  box(s, lx, 1.5, colW, 0.1, C.green, C.green, 0);
  addText(s, "仅参数变化 → Shader 不变", lx + 0.24, 1.72, colW - 0.48, 0.34, { size: 15, color: C.green, bold: true });
  addText(s, "Matrix 数值\nThreshold 数值\nAlphaOnly 开关", lx + 0.24, 2.2, colW - 0.48, 1.05, { size: 13, mono: true, color: C.text, lineSpacing: 1.35 });
  codeBlock(s, "if (AlphaOnly != 0) {\n  color = vec4(color.r);\n}", lx + 0.24, 3.4, colW - 0.48, 0.94);
  addText(s, "纯 fragment 数学折叠为 uniform 分支：同一份 Shader 覆盖两种取值，运行时只更新 uniform。", lx + 0.24, 4.52, colW - 0.48, 1.7, { size: 12.5, color: C.muted, lineSpacing: 1.25 });
  box(s, rx, 1.5, colW, 4.9, C.white, C.red, 1.6);
  box(s, rx, 1.5, colW, 0.1, C.red, C.red, 0);
  addText(s, "增加设备遮罩 → 新的 FS", rx + 0.24, 1.72, colW - 0.48, 0.34, { size: 15, color: C.red, bold: true });
  addText(s, "资源接口多一个 sampler", rx + 0.24, 2.2, colW - 0.48, 0.3, { size: 13, color: C.text });
  codeBlock(s,
    "layout(set = 1, binding = 0)\n  uniform sampler2D TextureSampler_0;\nlayout(set = 1, binding = 1)\n  uniform sampler2D MaskTextureSampler;",
    rx + 0.24, 2.62, colW - 0.48, 1.34);
  addText(s, "sampler 数量与绑定布局属于编译期接口，无法用 uniform 表达：必须生成另一份 Shader。", rx + 0.24, 4.14, colW - 0.48, 1.9, { size: 12.5, color: C.muted, lineSpacing: 1.25 });
  conclude(s, "首次成本来自结构与资源接口变化，不来自颜色参数数值。");
  notes(s, "源码依据：src/gpu/shaders/glsl/level1/texture_fill.frag:35-46（TextureSampler_0 与 HAS_DEVICE_MASK 下的 MaskTextureSampler）、:61-65（AlphaOnly uniform 分支）；src/gpu/shaders/level1/TextureFillShader.h:41-42 说明 AlphaOnly/HasRgbaaa 折叠为运行时 uniform。删减说明：左右各取一个代表性差异，未列出全部维度。适用边界：只讨论 FS 侧；VS 同理按 varying/属性接口划分。");
}

// ---------------------------------------------------------------- 03
{
  const s = addBase("03", "成本来源 / COST ORIGIN", "生成源码后还要完成后端编译和Pipeline创建");
  const labels = ["VS/FS 源码", "Backend 编译", "Shader Module", "Pipeline", "Draw"];
  const bw = 2.06, gap = (W - 2 * M - 5 * bw) / 4;
  labels.forEach((t, i) => {
    const x = M + i * (bw + gap);
    const hot = i === 3;
    flowNode(s, t, x, 1.56, bw, 0.66, {
      fill: hot ? C.orangeSoft : C.white, line: hot ? C.orange : C.navy, color: hot ? C.orange : C.navy, size: 13.5,
    });
    if (i < 4) hArrow(s, x + bw + 0.03, 1.89, gap - 0.06);
  });
  addText(s, "Pipeline 状态组成", M, 2.52, 4, 0.3, { size: 13, color: C.orange, bold: true });
  const pills = ["Shader Module", "附件格式", "混合状态", "采样数", "深度模板", "顶点布局"];
  const pw = 1.82, pgap = (W - 2 * M - 6 * pw) / 5;
  pills.forEach((t, i) => chip(s, t, M + i * (pw + pgap), 2.88, pw, 0.44, { fill: C.orangeSoft, color: C.orange, size: 12 }));
  addTable(s, [
    headRow(["后端", "Shader 侧工作", "Pipeline 侧工作"]),
    [{ text: "OpenGL" }, { text: "驱动编译 VS/FS 源码" }, { text: "链接 GL Program（状态在绘制时绑定）" }],
    [{ text: "Vulkan" }, { text: "创建 VkShaderModule" }, { text: "创建 VkPipeline（状态全部冻结）" }],
    [{ text: "Metal" }, { text: "取得 MTLFunction" }, { text: "创建 MTLRenderPipelineState" }],
    [{ text: "WebGPU" }, { text: "创建 GPUShaderModule" }, { text: "创建 GPURenderPipeline" }],
  ], M, 3.62, W - 2 * M, [1.7, (W - 2 * M - 1.7) / 2, (W - 2 * M - 1.7) / 2], { size: 12 });
  conclude(s, "Shader 只是 Pipeline 的一个输入；Pipeline 仍是按绘制状态组装的运行时对象。", { color: C.orange });
  notes(s, "源码依据：src/gpu/ProgramInfo.cpp:151-175 的 key 组成（blendMode、format、sampleCount、depthStencil 等即 Pipeline 状态）；各后端 src/gpu/opengl、vulkan、metal、webgpu 的 Program/Pipeline 创建代码。删减说明：每后端只保留一句代表性工作。适用边界：OpenGL 无独立 Pipeline 对象，其等价成本是 driver 编译与链接。");
}

// ---------------------------------------------------------------- 04
{
  const s = addBase("04", "成本来源 / COST ORIGIN", "缓存只能解决重复创建，不能解决首次创建");
  addTable(s, [
    headRow(["输入变化", "缓存键", "结果", "首次成本"]),
    [{ text: "Matrix / Threshold 数值变化" }, { text: "不变" }, { text: "命中", options: { color: C.green, bold: true } }, { text: "无", options: { color: C.green } }],
    [{ text: "sampleCount 1 → 4" }, { text: "改变" }, { text: "未命中", options: { color: C.red, bold: true } }, { text: "编译 + 组装", options: { color: C.red } }],
    [{ text: "增加 DeviceMask" }, { text: "改变" }, { text: "未命中", options: { color: C.red, bold: true } }, { text: "生成源码 + 编译 + 组装", options: { color: C.red } }],
  ], M, 1.56, W - 2 * M, [4.3, 1.6, 1.6, W - 2 * M - 7.5], { size: 12.5 });
  addText(s, "键同时包含结构、资源接口与关键绘制状态；参数数值不进入键", M, 3.62, W - 2 * M, 0.3, { size: 12.5, color: C.muted });
  codeBlock(s,
    "key.write(static_cast<uint32_t>(renderTarget->sampleCount()));\n" +
    "auto program = globalCache->findProgram(programKey);",
    M, 4.0, W - 2 * M, 0.86);
  addText(s, "未命中时缓存无事可做：它记录的是“已经见过”，回答不了“第一次见到”。", M, 5.1, W - 2 * M, 0.4, { size: 13, color: C.text });
  conclude(s, "缓存消除重复成本；首次成本只能前移，不能靠缓存。", { color: C.red });
  notes(s, "源码依据：src/gpu/ProgramInfo.cpp:161-173（blendMode/format/sampleCount/depthStencil 写入键）、:181-182（buildProgramKey + findProgram）；src/gpu/GlobalCache.cpp:74。删减说明：键字段只展开 sampleCount 一行作代表。适用边界：uniform 值通常不形成新键，但改变资源接口或绘制状态必然换键。");
}

// ---------------------------------------------------------------- 05
{
  const s = addBase("05", "边界 / THE BOUNDARY", "运行时组合开放，构建期资产必须有限");
  const funnel = [
    { t: "公开 API 可组合出无界绘制结构", w: 10.4, fill: C.blueSoft, color: C.navy },
    { t: "运行缓存只能覆盖已经见过的组合", w: 8.2, fill: C.blueSoft, color: C.navy },
    { t: "构建期只能准备有限份 Shader", w: 6.0, fill: C.orangeSoft, color: C.orange },
    { t: "由谁定义这个有限范围", w: 3.9, fill: C.redSoft, color: C.red },
  ];
  funnel.forEach((f, i) => {
    const w = f.w, x = (W - w) / 2, y = 1.7 + i * 1.14;
    flowNode(s, f.t, x, y, w, 0.74, { fill: f.fill, line: f.color, color: f.color, size: 14.5 });
    if (i < 3) vArrow(s, W / 2, y + 0.76, 0.36, C.muted);
  });
  conclude(s, "AOT 的关键不是提前编译，而是定义有限且可覆盖的候选集合。", { color: C.red });
  notes(s, "大纲依据：docs/tgfx-aot-presentation-v2.md 第 5 节。删减说明：漏斗为逻辑收窄示意，不对应具体数字。适用边界：本页只提出归属问题，回答在第 06-10 页的行业对照与第 11 页起的 TGFX 方案中展开。");
}

// ---------------------------------------------------------------- 06
{
  const s = addBase("06", "行业对照 / INDUSTRY", "预编译范围必须由掌握完整信息的一方提供");
  addTable(s, [
    headRow(["定义候选范围需要的信息", "通常由谁掌握"]),
    [{ text: "全部支持的运算种类和实现" }, { text: "库 / 引擎的实现者" }],
    [{ text: "实际会用到哪些组合" }, { text: "调用方或项目" }],
    [{ text: "资产清单与目标平台集合" }, { text: "项目构建流程" }],
    [{ text: "统一的构建入口与产出分发" }, { text: "引擎或项目的构建系统" }],
  ], M, 1.56, W - 2 * M, [(W - 2 * M) / 2, (W - 2 * M) / 2], { size: 12.5 });
  addText(s, "四者缺一会发生什么", M, 4.1, 6, 0.3, { size: 13, color: C.navy, bold: true });
  chip(s, "缺使用信息 → 范围溢出：编译大量永远用不到的版本", M, 4.5, W - 2 * M, 0.46, { fill: C.redSoft, color: C.red, size: 12 });
  chip(s, "缺算法信息 → 范围缺失：运行时遇到清单外组合", M, 5.06, W - 2 * M, 0.46, { fill: C.redSoft, color: C.red, size: 12 });
  chip(s, "信息齐备 → 范围有限且可覆盖", M, 5.62, W - 2 * M, 0.46, { fill: C.greenSoft, color: C.green, size: 12 });
  conclude(s, "没有完整信息的一方，给出的清单必然溢出或缺失。");
  notes(s, "本页为第 07-10 页的总纲：Flutter（引擎掌握算法+使用）、Graphite（调用方掌握使用并申报）、Unreal（项目掌握资产与平台）、TGFX（只有库内信息）。删减说明：信息要素归纳为四项。适用边界：判断标准是“谁同时掌握算法与使用”，不评价各方案优劣。");
}

// ---------------------------------------------------------------- 07
{
  const s = addBase("07", "行业对照 / INDUSTRY", "Flutter由引擎集中维护有限的Shader能力");
  const chain = [
    { t: "引擎自绘全栈，绘制能力由引擎收敛", c: C.navy, f: C.white, l: C.line },
    { t: "算法与全部用法都在引擎内部可见", c: C.navy, f: C.white, l: C.line },
    { t: "构建期枚举并编译有限 Shader 集合", c: C.navy, f: C.white, l: C.line },
    { t: "运行时零着色器编译", c: C.green, f: C.greenSoft, l: C.green },
  ];
  chain.forEach((n, i) => {
    const y = 1.6 + i * 1.02;
    flowNode(s, n.t, M + 0.5, y, 6.1, 0.7, { fill: n.f, line: n.l, color: n.c, size: 13.5 });
    if (i < 3) vArrow(s, M + 3.55, y + 0.72, 0.28);
  });
  const rx = 7.6, rw = W - M - rx;
  box(s, rx, 1.66, rw, 4.44, C.white, C.orange, 1.6);
  box(s, rx, 1.66, rw, 0.1, C.orange, C.orange, 0);
  addText(s, "成立前提与代价", rx + 0.24, 1.9, rw - 0.48, 0.32, { size: 14, color: C.orange, bold: true });
  addText(s, "前提：引擎同时拥有算法实现与全部调用点。\n\n代价：以收窄公开绘制能力换取有限性，新增能力必须先进引擎。\n\nFlutter 选择删除运行时着色器编译路径，正是建立在这个前提之上。", rx + 0.24, 2.34, rw - 0.48, 3.5, { size: 12.5, color: C.text, lineSpacing: 1.3 });
  addText(s, "固定快照：16 个 .vert + 47 个 .frag + 5 个共享 .glsl = 63 个 Shader stage 源文件", M + 0.5, 5.74, 6.4, 0.3, { size: 10, color: C.navy, bold: true, mono: true });
  addText(s, "这是 Shader stage 源文件数，不是 Pipeline 数；Pipeline 仍需结合后端与绘制状态在运行时组装。", M + 0.5, 6.08, 6.4, 0.5, { size: 11, color: C.muted, lineSpacing: 1.2 });
  conclude(s, "集中维护有效，因为引擎有权收窄能力；库没有这个权力。");
  notes(s, "外部依据：Flutter Impeller 的设计方向（构建期离线编译全部着色器、删除运行时编译路径），属公开资料；63 个 stage 源文件为审查确认的固定快照计数，是 Shader 源文件数而非 Pipeline 数。删减说明：不展开 Impeller 的具体分层，只保留与 TGFX 可对照的因果链。适用边界：结论仅用于对照“谁掌握信息”，不代表 TGFX 可复制该前提。");
}

// ---------------------------------------------------------------- 08
{
  const s = addBase("08", "行业对照 / INDUSTRY", "Graphite由调用方申报需要预编译的候选组合");
  addText(s, "调用方用公开 API 申报自己的用法", M, 1.52, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "graphite::PaintOptions options;\n" +
    "options.setShaders({ gradient, imageShader });\n" +
    "options.setBlendModes({ BlendMode::kSrcOver });\n" +
    "graphite::Precompile(recorder, options, drawTypes);",
    M, 1.9, 6.5, 1.5);
  const steps = ["调用方枚举自己的组合", "引擎在预编译窗口构建", "运行时直接命中"];
  steps.forEach((t, i) => {
    const y = 3.7 + i * 0.88;
    flowNode(s, t, M, y, 6.5, 0.6, { size: 12.5, fill: i === 2 ? C.greenSoft : C.white, line: i === 2 ? C.green : C.line, color: i === 2 ? C.green : C.navy });
    if (i < 2) vArrow(s, M + 3.25, y + 0.62, 0.24);
  });
  const rx = 7.6, rw = W - M - rx;
  box(s, rx, 1.9, rw, 4.4, C.white, C.line, 1.2);
  addText(s, "职责划分", rx + 0.24, 2.12, rw - 0.48, 0.3, { size: 14, color: C.navy, bold: true });
  addText(s, "引擎：提供算法实现与预编译机制。\n\n调用方：掌握自己的用法，负责把候选组合喂给引擎。\n\n范围的完整性来自调用方，不来自引擎。", rx + 0.24, 2.54, rw - 0.48, 2.2, { size: 12.5, color: C.text, lineSpacing: 1.3 });
  chip(s, "前提：调用方能够且愿意枚举自己的用法", rx + 0.24, 5.1, rw - 0.48, 0.46, { fill: C.orangeSoft, color: C.orange, size: 12 });
  conclude(s, "申报制把“谁知道会用到什么”显式交给调用方回答。");
  notes(s, "外部依据：Skia Graphite 的 Precompile 公开 API（PaintOptions + Precompile 入口），代码为真实 API 的最小调用形态，参数列表有删减。适用边界：申报依赖调用方配合；下游不维护清单时该机制无输入。");
}

// ---------------------------------------------------------------- 09
{
  const s = addBase("09", "行业对照 / INDUSTRY", "Unreal依靠项目上下文分别准备Shader和Pipeline状态");
  const lw = (W - 2 * M - 0.4) / 2;
  box(s, M, 1.56, lw, 4.5, C.white, C.navy, 1.4);
  box(s, M, 1.56, lw, 0.1, C.navy, C.navy, 0);
  addText(s, "Shader 层：项目 Cook", M + 0.24, 1.8, lw - 0.48, 0.32, { size: 14, color: C.navy, bold: true });
  addText(s, "材质图 × 目标平台 × 渲染特性开关\n→ 在项目构建期生成 Shader Library", M + 0.24, 2.26, lw - 0.48, 0.9, { size: 12.5, color: C.text, lineSpacing: 1.3 });
  flowNode(s, "清单来源：项目资产与构建设置", M + 0.24, 3.4, lw - 0.48, 0.56, { fill: C.blueSoft, size: 12 });
  addText(s, "产出随项目打包分发", M + 0.24, 4.16, lw - 0.48, 0.3, { size: 12, color: C.green, bold: true });
  const rx = M + lw + 0.4;
  box(s, rx, 1.56, lw, 4.5, C.white, C.orange, 1.4);
  box(s, rx, 1.56, lw, 0.1, C.orange, C.orange, 0);
  addText(s, "Pipeline 层：状态缓存", rx + 0.24, 1.8, lw - 0.48, 0.32, { size: 14, color: C.orange, bold: true });
  addText(s, "运行/采集阶段记录 Pipeline 状态\n→ 预热创建，绑定具体渲染目标", rx + 0.24, 2.26, lw - 0.48, 0.9, { size: 12.5, color: C.text, lineSpacing: 1.3 });
  flowNode(s, "清单来源：同一项目的真实绘制记录", rx + 0.24, 3.4, lw - 0.48, 0.56, { fill: C.orangeSoft, color: C.orange, size: 12 });
  addText(s, "与 Shader Library 互补，不互相替代", rx + 0.24, 4.16, lw - 0.48, 0.62, { size: 12, color: C.muted });
  chip(s, "TGFX 借鉴的是“有限开关生成 Shader 版本 + Shader/Pipeline 分层”，不照搬 Material Cook 和 PSO 枚举", M + 0.8, 6.14, W - 2 * M - 1.6, 0.42, { fill: C.greenSoft, color: C.green, size: 12.5 });
  conclude(s, "“项目”这一资产边界是两层清单共同的来源；库看不到项目资产。");
  notes(s, "外部依据：Unreal 的 Shader Library（Cook 阶段产出）与 PSO Cache（采集+预热）机制，属公开资料。删减说明：两层各保留清单来源与产出，不展开材质系统细节。适用边界：方案成立依赖项目上下文完整可见，与库形态不兼容。");
}

// ---------------------------------------------------------------- 10
{
  const s = addBase("10", "行业对照 / INDUSTRY", "TGFX无法依赖外部清单，只能在库内定义预编译范围");
  addTable(s, [
    headRow(["外部清单来源", "成立前提", "TGFX 是否具备"]),
    [{ text: "引擎固定能力（Flutter）" }, { text: "引擎有权删减对外绘制能力" }, { text: "不具备：TGFX 不能收窄公开 API", options: { color: C.red, bold: true } }],
    [{ text: "调用方申报（Graphite）" }, { text: "调用方能枚举预计使用的组合" }, { text: "不具备：下游不提供组合清单", options: { color: C.red, bold: true } }],
    [{ text: "项目资产 Cook（Unreal）" }, { text: "构建时能看到项目资产" }, { text: "不具备：库构建时没有项目上下文", options: { color: C.red, bold: true } }],
    [{ text: "库内规则" }, { text: "TGFX 自己定义支持的运算和接口取值" }, { text: "唯一可行", options: { color: C.green, bold: true } }],
  ], M, 1.56, W - 2 * M, [3.3, 3.6, W - 2 * M - 6.9], { size: 12.5 });
  addText(s, "排除之后剩下的唯一来源", M, 4.34, 8, 0.3, { size: 13, color: C.navy, bold: true });
  flowNode(s, "库内规则：TGFX 预先定义支持哪些 Shader 算法、每个接口开关有哪些取值，构建期枚举生成候选版本", M, 4.72, W - 2 * M, 0.66, { fill: C.greenSoft, line: C.green, color: C.green, size: 13 });
  chip(s, "最小例子：HAS_XP 3 种目标读取 × HAS_DEVICE_MASK 0/1 → 最多 6 个候选 FS 接口", M + 1.6, 5.56, W - 2 * M - 3.2, 0.5, { fill: C.blueSoft, color: C.navy, size: 12.5 });
  conclude(s, "外部清单全部不可得，候选版本只能由库内规则枚举给出。", { color: C.green });
  notes(s, "大纲依据：docs/tgfx-aot-presentation-v2.md 第 5 节候选方式表；项目约束见 feedback：下游不改库且 API 参数无界，闭包必须在库内完成。删减说明：三种外部来源各压缩为一个不成立的前提。适用边界：规则必须同时满足有限与可覆盖，这是第 11-19 页的主题。");
}

// ---------------------------------------------------------------- 11
{
  const s = addBase("11", "TGFX 分层 / LAYERING", "TGFX将算法代码、资源接口和运行时参数分层处理");
  const cw = (W - 2 * M - 0.8) / 3;
  const cols = [
    { t: "固定算法代码", c: C.navy, soft: C.blueSoft, cap: "算法顺序在构建期固定（第 01 页）", items: ["构建期确定的源码文本", "运算种类与执行顺序", "例：采样 → 矩阵 → 阈值"] },
    { t: "资源接口", c: C.red, soft: C.redSoft, cap: "接口变化才生成新 Shader（第 02 页）", items: ["sampler 数量与 binding 布局", "目标读取方式", "例：有无设备遮罩"] },
    { t: "运行时参数", c: C.green, soft: C.greenSoft, cap: "参数变化不生成 Shader（第 02 页）", items: ["矩阵、阈值、AlphaOnly", "纹理对象", "运行时更新，不换版本"] },
  ];
  cols.forEach((col, i) => {
    const x = M + i * (cw + 0.4);
    box(s, x, 1.56, cw, 2.94, C.white, col.c, 1.5);
    box(s, x, 1.56, cw, 0.1, col.c, col.c, 0);
    addText(s, col.t, x + 0.22, 1.76, cw - 0.44, 0.32, { size: 14.5, color: col.c, bold: true });
    col.items.forEach((it, j) => {
      chip(s, it, x + 0.22, 2.2 + j * 0.56, cw - 0.44, 0.46, { fill: col.soft, color: col.c, size: 11.5 });
    });
    addText(s, col.cap, x + 0.22, 3.9, cw - 0.44, 0.56, { size: 11, color: C.muted, lineSpacing: 1.2 });
  });
  addText(s, "最小 GLSL 证明：三层共存于同一份 Shader", M, 4.66, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "#if HAS_DEVICE_MASK\n" +
    "uniform sampler2D MaskTextureSampler;\n" +
    "#endif\n" +
    "vec4 color = texture(TextureSampler_0, coord);\n" +
    "if (AlphaOnly != 0) color = vec4(color.r);",
    M, 5.02, 7.1, 1.5);
  addText(s, "#if 编译期分支 = 资源接口（静态）\ntexture 采样行 = 固定算法代码\nAlphaOnly 分支 = 运行时参数（uniform）", 8.1, 5.02, W - M - 8.1, 1.5, { size: 12, color: C.text, lineSpacing: 1.4, valign: "mid" });
  conclude(s, "资源接口决定静态版本；算法代码决定 Shader 归属；参数值留在运行时。");
  notes(s, "源码依据：src/gpu/shaders/glsl/level1/texture_fill.frag:35-46（HAS_DEVICE_MASK 编译期分支与 MaskTextureSampler 声明）、:61-65（AlphaOnly 运行时 uniform 分支）；src/gpu/shaders/level1/TextureFillShader.h:37-52。删减说明：GLSL 为原文件节选，只保留同时证明三层共存的代表性行。适用边界：分层按“是否改变静态版本”划分，不是按数据类型划分。");
}

// ---------------------------------------------------------------- 12
{
  const s = addBase("12", "TGFX 分层 / LAYERING", "一个Shader的静态版本由源码和接口取值共同决定");
  addText(s, "静态维度的真实声明（TextureFillShader 源码原文）", M, 1.52, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "static PermutationDomain domain() {\n" +
    "  return PermutationDomain({\n" +
    "      PermutationInt(\"HAS_XP\", 3),\n" +
    "      PermutationBool(\"HAS_DEVICE_MASK\"),\n" +
    "  });\n" +
    "}",
    M, 1.9, 6.5, 1.96);
  addText(s, "维度只含接口开关，且每个开关取值有限、可数。", M, 4.06, 6.5, 0.6, { size: 12, color: C.muted, lineSpacing: 1.3 });
  const rx = 7.5, rw = W - M - rx;
  box(s, rx, 1.9, rw, 1.96, C.white, C.navy, 1.5);
  box(s, rx, 1.9, rw, 0.1, C.navy, C.navy, 0);
  addText(s, "3 × 2 = 6", rx, 2.16, rw, 0.6, { size: 30, color: C.navy, bold: true, align: "center", mono: true });
  addText(s, "个候选 FS 接口（3 种目标读取 × 遮罩 0/1）", rx, 2.84, rw, 0.3, { size: 12.5, color: C.text, align: "center" });
  addText(s, "VS 接口不变 → 只有 1 个版本", rx, 3.32, rw, 0.34, { size: 13.5, color: C.green, bold: true, align: "center" });
  chip(s, "AlphaOnly、ColorMatrix、Threshold 不在静态维度中：它们是运行时 uniform，不参与版本划分", M + 0.6, 4.42, W - 2 * M - 1.2, 0.52, { fill: C.greenSoft, color: C.green, size: 12.5 });
  addText(s, "枚举结果：逐项展开，无手工列表", M, 4.98, W - 2 * M, 0.3, { size: 12.5, color: C.navy, bold: true, align: "center" });
  const vw = 0.95, vgap = 0.18, rowW = 6 * vw + 5 * vgap + 0.5 + 1.75;
  const x0 = (W - rowW) / 2;
  for (let i = 0; i < 6; i++) {
    const hot = i === 3;
    flowNode(s, "FS v" + i, x0 + i * (vw + vgap), 5.42, vw, 0.52, { size: 11.5, mono: true, fill: hot ? C.navy : C.white, color: hot ? "FFFFFF" : C.navy });
  }
  addText(s, "+", x0 + 6 * (vw + vgap) - vgap + 0.08, 5.42, 0.36, 0.52, { size: 16, color: C.muted, bold: true, align: "center", valign: "mid" });
  chip(s, "VS 仅 1 个版本", x0 + 6 * (vw + vgap) + 0.5, 5.42, 1.75, 0.52, { fill: C.greenSoft, color: C.green, size: 12 });
  conclude(s, "开发者声明开关与源码，不手写最终构建清单。");
  notes(s, "源码依据：src/gpu/shaders/level1/TextureFillShader.h:47-52（domain() 声明原文：HAS_XP 3 值 × HAS_DEVICE_MASK 2 值）、:37-45（AlphaOnly/HasRgbaaa 折叠为运行时 uniform，ColorMatrix/Threshold 同为运行时数据）；VS 侧接口不随这两个开关变化。删减说明：右侧只展开 FS 的乘法；高亮的 FS v3 对应第 19 页 Mask=1, XP=0 → v=3 的例子，版本号编码与查找细节见第 19 页。适用边界：声明给出候选上界，真实编译范围由第 17 页的可达性筛选决定。");
}

// ---------------------------------------------------------------- 13
{
  const s = addBase("13", "TGFX 分层 / LAYERING", "Program、单阶段资产和Pipeline具有不同复用范围");
  addTable(s, [
    headRow(["对象", "身份的键", "创建时机", "能否随包分发"]),
    [{ text: "Stage 资产（VS/FS 二进制）", options: { bold: true, color: C.green } }, { text: "shader 名 + 单 Stage 取值 + 后端 profile" }, { text: "构建期产物" }, { text: "可以", options: { color: C.green, bold: true } }],
    [{ text: "Program（GPU 程序对象）", options: { bold: true, color: C.orange } }, { text: "结构 + 资源接口 + 关键绘制状态" }, { text: "运行时创建并缓存" }, { text: "不能", options: { color: C.orange, bold: true } }],
    [{ text: "Pipeline（渲染管线对象）", options: { bold: true, color: C.orange } }, { text: "附件格式 / 采样数 / 混合 / 深度模板" }, { text: "运行时按绘制状态组装" }, { text: "不能", options: { color: C.orange, bold: true } }],
  ], M, 1.56, W - 2 * M, [3.5, 4.3, 2.3, W - 2 * M - 10.1], { size: 12 });
  addText(s, "VS 与 FS 是分开的资产：同一 FS 可服务多个 VS，复用以 Stage 为粒度。", M, 4.0, W - 2 * M, 0.34, { size: 12.5, color: C.muted });
  flowNode(s, "可分发的是 Stage 资产；Program 与 Pipeline 永远留在运行时", M + 1.6, 4.6, W - 2 * M - 3.2, 0.7, { fill: C.greenSoft, line: C.green, color: C.green, size: 14 });
  conclude(s, "三者的键、时机与生命周期不同，复用范围不能混为一谈。", { color: C.green });
  notes(s, "源码依据：src/gpu/ProgramInfo.cpp:151-175（Program 键的完整组成）；构建产物 cmake-build-debug-metal/generated/shaders/shader_build_report.json 中 artifactProfiles 以 VS/FS 分开计数（101/274 逻辑、78/250 去重）。删减说明：Stage 键压缩为一行文字。适用边界：OpenGL 无独立 Pipeline 对象，其“Pipeline”指等价的绘制时状态绑定。");
}

// ---------------------------------------------------------------- 14
{
  const s = addBase("14", "TGFX 分层 / LAYERING", "算法代码或资源接口变化才需要新的静态版本");
  const colW = (W - 2 * M - 0.4) / 2;
  box(s, M, 1.56, colW, 4.5, C.white, C.red, 1.5);
  box(s, M, 1.56, colW, 0.1, C.red, C.red, 0);
  addText(s, "资源接口变化 → 新静态版本", M + 0.24, 1.8, colW - 0.48, 0.32, { size: 14, color: C.red, bold: true });
  codeBlock(s,
    "#if HAS_DEVICE_MASK\nlayout(set = 1, binding = 1)\n  uniform sampler2D MaskTextureSampler;\n#endif",
    M + 0.24, 2.24, colW - 0.48, 1.24);
  addText(s, "编译期分支改变 sampler 绑定布局，两个取值对应两份真实 FS。", M + 0.24, 3.66, colW - 0.48, 1.0, { size: 12.5, color: C.muted, lineSpacing: 1.3 });
  const rx = M + colW + 0.4;
  box(s, rx, 1.56, colW, 4.5, C.white, C.green, 1.5);
  box(s, rx, 1.56, colW, 0.1, C.green, C.green, 0);
  addText(s, "纯参数变化 → 同一版本", rx + 0.24, 1.8, colW - 0.48, 0.32, { size: 14, color: C.green, bold: true });
  codeBlock(s,
    "if (AlphaOnly != 0) {\n  color = vec4(color.r);\n}",
    rx + 0.24, 2.24, colW - 0.48, 0.94);
  addText(s, "uniform 分支不改资源接口，两个取值共享同一份 FS，运行时只改 uniform。", rx + 0.24, 3.36, colW - 0.48, 1.0, { size: 12.5, color: C.muted, lineSpacing: 1.3 });
  chip(s, "判据：是否改变资源绑定，而不是“代码看起来不同”", rx + 0.24, 4.6, colW - 0.48, 0.5, { fill: C.blueSoft, color: C.navy, size: 12 });
  conclude(s, "版本划分的唯一判据是算法结构与资源绑定，不是行为差异本身。");
  notes(s, "源码依据：src/gpu/shaders/glsl/level1/texture_fill.frag:37-46（HAS_DEVICE_MASK 编译期维度）、:61-65（AlphaOnly 运行时分支）；src/gpu/shaders/level1/TextureFillShader.h:41-42。删减说明：两段 GLSL 为原文件节选。适用边界：纯 fragment 数学才可折叠为 uniform；改绑定布局或顶点接口的差异不能折叠。");
}

// ---------------------------------------------------------------- 15
{
  const s = addBase("15", "TGFX 分层 / LAYERING", "全功能Shader会扩大资源接口、分支和验证范围");
  const colW = (W - 2 * M - 0.4) / 2;
  box(s, M, 1.56, colW, 4.5, C.white, C.red, 1.5);
  box(s, M, 1.56, colW, 0.1, C.red, C.red, 0);
  addText(s, "全功能单 Shader", M + 0.24, 1.8, colW - 0.48, 0.32, { size: 14.5, color: C.red, bold: true });
  addText(s, "资源接口：所有可能 sampler 的并集\n分支：全部路径堆叠在一份源码里\n验证：组合数随特性乘法膨胀\n结果：任何一个特性变化都牵动整份资产", M + 0.24, 2.3, colW - 0.48, 2.4, { size: 12.5, color: C.text, lineSpacing: 1.45 });
  chip(s, "接口、分支、验证同时被放大", M + 0.24, 5.0, colW - 0.48, 0.5, { fill: C.redSoft, color: C.red, size: 12 });
  const rx = M + colW + 0.4;
  box(s, rx, 1.56, colW, 4.5, C.white, C.green, 1.5);
  box(s, rx, 1.56, colW, 0.1, C.green, C.green, 0);
  addText(s, "按维度拆分的有限版本", rx + 0.24, 1.8, colW - 0.48, 0.32, { size: 14.5, color: C.green, bold: true });
  addText(s, "资源接口：按维度声明，只有用到的 sampler\n分支：每个版本只含自己的路径\n验证：按版本独立对比与回归\n结果：单个维度变化只影响自己的版本族", rx + 0.24, 2.3, colW - 0.48, 2.4, { size: 12.5, color: C.text, lineSpacing: 1.45 });
  chip(s, "版本数受控 = 维度独立 × 取值有限", rx + 0.24, 5.0, colW - 0.48, 0.5, { fill: C.greenSoft, color: C.green, size: 12 });
  addText(s, "全库声明候选 4164 个，规模见后文。", rx + 0.24, 5.62, colW - 0.48, 0.34, { size: 11.5, color: C.muted });
  conclude(s, "拒绝全功能 Shader，是为了让接口、分支与验证都可逐一掌控。", { color: C.red });
  notes(s, "设计依据：src/gpu/shaders/level1/ 下每个 shader 声明自己的有限维度（如 TextureFillShader.h:43-53），而非合并为单一全能 FS；变体体积分析见项目记录（混合表等逻辑体积来源）。删减说明：以对照形式陈述，不展开具体 shader。适用边界：拆分的前提是维度相互独立且取值有限；维度之间耦合会重新引入乘法膨胀。");
}

// ---------------------------------------------------------------- 16
{
  const s = addBase("16", "闭环 / BUILD-RUNTIME LOOP", "构建期按开关枚举并编译，写入各后端Bundle");
  const steps = ["声明开关", "逐项枚举", "注入宏", "同一份 GLSL", "分后端编译", "Bundle"];
  const bw = 1.75, gap = (W - 2 * M - 6 * bw) / 5;
  steps.forEach((t, i) => {
    const x = M + i * (bw + gap);
    flowNode(s, t, x, 1.86, bw, 0.72, { fill: i === 5 ? C.greenSoft : C.white, line: i === 5 ? C.green : C.navy, color: i === 5 ? C.green : C.navy, size: 13 });
    if (i < 5) hArrow(s, x + bw + 0.03, 2.22, gap - 0.06);
  });
  addText(s, "以 TextureFillShader 为例：同一份 GLSL 源，注入 6 组宏", M, 2.98, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "// 源码唯一：level1/texture_fill.frag\n" +
    "-DHAS_XP=0 -DHAS_DEVICE_MASK=0\n" +
    "-DHAS_XP=0 -DHAS_DEVICE_MASK=1\n" +
    "-DHAS_XP=1 -DHAS_DEVICE_MASK=0\n" +
    "-DHAS_XP=1 -DHAS_DEVICE_MASK=1\n" +
    "-DHAS_XP=2 -DHAS_DEVICE_MASK=0\n" +
    "-DHAS_XP=2 -DHAS_DEVICE_MASK=1",
    M, 3.36, 7.1, 2.1);
  const rx = 8.1, rw = W - M - rx;
  box(s, rx, 3.36, rw, 0.94, C.white, C.navy, 1.4);
  addText(s, "VS 接口不变", rx + 0.2, 3.5, rw - 1.6, 0.3, { size: 12.5, color: C.text });
  addText(s, "1 个版本", rx + rw - 1.5, 3.44, 1.3, 0.4, { size: 17, color: C.green, bold: true, align: "right" });
  box(s, rx, 4.52, rw, 0.94, C.white, C.navy, 1.4);
  addText(s, "FS 接口 6 种取值组合", rx + 0.2, 4.66, rw - 1.6, 0.3, { size: 12.5, color: C.text });
  addText(s, "6 个版本", rx + rw - 1.5, 4.6, 1.3, 0.4, { size: 17, color: C.navy, bold: true, align: "right" });
  addText(s, "每个版本按各后端 profile 分别编译一次。", rx, 5.62, rw, 0.6, { size: 12, color: C.muted, lineSpacing: 1.3 });
  addText(s, "全库当前 30 种算法模板 · 308 个可达版本 · 328 个唯一 Stage 二进制（Metal 2026-08-19 快照）", M, 6.06, W - 2 * M, 0.3, { size: 11, color: C.muted, mono: true, align: "center" });
  conclude(s, "同一份源码按开关展开为有限版本；分后端编译全部在构建期完成并写入 Bundle。", { color: C.green });
  notes(s, "源码依据：构建管线按 domain() 声明逐项枚举并注入宏，同一份 GLSL 分后端编译；数据来自 cmake-build-debug-metal/generated/shaders/shader_build_report.json（30 个模板、compiledCount 合计 308、unique 合计 328，Metal 2026-08-19 本地快照）。删减说明：流程压缩为六个节点；宏组合只列 TextureFillShader 一例，可达性筛选的逐 shader 数字见第 17 页。适用边界：308/328 为 Metal 快照，其他后端同规则、不同 profile，数值不同。");
}

// ---------------------------------------------------------------- 17
{
  const s = addBase("17", "闭环 / BUILD-RUNTIME LOOP", "Build只编译支持规则能够产生的Shader版本");
  addText(s, "可达性筛选的真实数字（Metal，2026-08-19 本地快照）", M, 1.52, 10, 0.3, { size: 13, color: C.navy, bold: true });
  addTable(s, [
    headRow(["Shader", "声明候选", "实际编译", "被排除的组合"]),
    [{ text: "PerlinNoiseFillShader" }, { text: "12", options: { mono: true } }, { text: "6", options: { mono: true, color: C.green, bold: true } }, { text: "规则永远不会产生的搭配" }],
    [{ text: "AtlasTextFillShader" }, { text: "48", options: { mono: true } }, { text: "12", options: { mono: true, color: C.green, bold: true } }, { text: "同上" }],
    [{ text: "NonAARRectFillShader" }, { text: "192", options: { mono: true } }, { text: "18", options: { mono: true, color: C.green, bold: true } }, { text: "同上" }],
    [{ text: "全库合计", options: { bold: true } }, { text: "4164", options: { mono: true, bold: true } }, { text: "308", options: { mono: true, color: C.green, bold: true } }, { text: "约 93% 的候选不可达", options: { color: C.red } }],
  ], M, 1.92, W - 2 * M, [3.6, 1.9, 1.9, W - 2 * M - 7.4], { size: 12 });
  addText(s, "“可达”由运行时同一份支持规则判定：规则在绘制时产生不了的组合，构建期就不编译。", M, 4.6, W - 2 * M, 0.34, { size: 12.5, color: C.text });
  chip(s, "编译清单的唯一作者是规则，不是人工维护的列表", M + 1.6, 5.2, W - 2 * M - 3.2, 0.52, { fill: C.greenSoft, color: C.green, size: 13 });
  conclude(s, "构建期与运行时用同一规则说话，清单之外即不可达。", { color: C.green });
  notes(s, "数据来源：cmake-build-debug-metal/generated/shaders/shader_build_report.json 逐 shader 的 rawCount 与 compiledCount；规则单一作者见 src/gpu 内 Compose 映射（规则可达集迁移已完成，清单由规则生成）。删减说明：30 个 shader 只列三个代表加合计。适用边界：数字随规则演进变化，仅 Metal 快照；其他后端数值不同。");
}

// ---------------------------------------------------------------- 17
{
  const s = addBase("18", "闭环 / BUILD-RUNTIME LOOP", "可达性筛选与内容去重将4164个候选压缩为328个Stage二进制");
  const nums = [
    { n: "4164", t: "声明候选", d: "每个开关只允许几个固定取值，逐项枚举出的组合总数", c: C.navy },
    { n: "308", t: "规则可达", d: "支持规则真正能够产生的版本", c: C.navy },
    { n: "375", t: "逻辑 Stage", d: "按 VS / FS 分开计数", c: C.blue },
    { n: "328", t: "唯一二进制", d: "内容相同的只保留一份", c: C.green },
  ];
  const bw = 2.6, gap = (W - 2 * M - 4 * bw) / 3;
  nums.forEach((it, i) => {
    const x = M + i * (bw + gap);
    box(s, x, 1.72, bw, 2.5, C.white, it.c, 1.5);
    addText(s, it.n, x, 1.94, bw, 0.7, { size: 34, color: it.c, bold: true, align: "center", mono: true });
    addText(s, it.t, x, 2.72, bw, 0.34, { size: 14, color: C.ink, bold: true, align: "center" });
    addText(s, it.d, x + 0.16, 3.14, bw - 0.32, 0.9, { size: 11.5, color: C.muted, align: "center", lineSpacing: 1.2 });
    if (i < 3) hArrow(s, x + bw + 0.04, 2.95, gap - 0.08);
  });
  chip(s, "4164 → 308：可达性筛选", M + 0.5, 4.62, 3.6, 0.44, { fill: C.redSoft, color: C.red, size: 11.5 });
  chip(s, "308 → 375：拆为独立 Stage", M + 4.35, 4.62, 3.6, 0.44, { fill: C.blueSoft, color: C.blue, size: 11.5 });
  chip(s, "375 → 328：内容去重", M + 8.2, 4.62, 3.6, 0.44, { fill: C.greenSoft, color: C.green, size: 11.5 });
  addText(s, "口径依次为：声明候选（按维度取值逐项枚举）→ 规则可达（支持规则能产生）→ 逻辑 Stage（VS / FS 分开计数）→ 唯一二进制（内容去重）", M, 5.26, W - 2 * M, 0.3, { size: 11.5, color: C.text, align: "center" });
  addText(s, "数据来源：shader_build_report.json · Metal 后端 · 2026-08-19 本地快照", M, 5.64, W - 2 * M, 0.3, { size: 11, color: C.muted, mono: true, align: "center" });
  conclude(s, "两级压缩都来自规则本身，不来自手工删减清单。", { color: C.green });
  notes(s, "数据来源：cmake-build-debug-metal/generated/shaders/shader_build_report.json（rawCount 合计 4164，compiledCount 合计 308，logical 101+274=375，unique 78+250=328）。删减说明：308 → 375 增大是因为同一版本的 VS/FS 作为独立 Stage 计数。适用边界：数字为 Metal 后端本地快照，其他后端同规则、不同 profile，数值会变；逻辑口径的数字不等于分发体积。");
}

// ---------------------------------------------------------------- 19
{
  const s = addBase("19", "闭环 / BUILD-RUNTIME LOOP", "运行时算同一版本号，从Bundle取资产并组装Pipeline");
  const lw = W - 2 * M;
  box(s, M, 1.5, lw, 1.24, C.white, C.green, 1.5);
  addText(s, "BUILD 泳道", M + 0.24, 1.62, 2, 0.26, { size: 12, color: C.green, bold: true, mono: true });
  const bSteps = ["规则输入", "同一映射", "版本号 v", "编译", "Bundle"];
  const bw1 = 2.1, bgap1 = (lw - 5 * bw1) / 4;
  bSteps.forEach((t, i) => {
    flowNode(s, t, M + i * (bw1 + bgap1), 2.0, bw1, 0.6, { fill: i === 4 ? C.greenSoft : C.white, line: i === 4 ? C.green : C.line, color: i === 4 ? C.green : C.navy, size: 13 });
    if (i < 4) hArrow(s, M + i * (bw1 + bgap1) + bw1 + 0.03, 2.3, bgap1 - 0.06);
  });
  box(s, M, 3.62, lw, 1.24, C.white, C.blue, 1.5);
  addText(s, "RUNTIME 泳道", M + 0.24, 3.74, 2.4, 0.26, { size: 12, color: C.blue, bold: true, mono: true });
  const rSteps = ["当前 Draw", "提取同类输入", "同一映射", "版本号 v", "查 Bundle", "创建 Module", "按当前状态\n创建 Pipeline", "Draw"];
  const bw2 = 1.35, bgap2 = (lw - 8 * bw2) / 7;
  rSteps.forEach((t, i) => {
    flowNode(s, t, M + i * (bw2 + bgap2), 4.12, bw2, 0.6, { fill: i === 7 ? C.greenSoft : C.white, line: i === 7 ? C.green : C.line, color: i === 7 ? C.green : C.navy, size: i === 6 ? 10.5 : 11.5 });
    if (i < 7) hArrow(s, M + i * (bw2 + bgap2) + bw2 + 0.02, 4.42, bgap2 - 0.04);
  });
  s.addShape("line", { x: 5.9, y: 2.6, w: 0.77, h: 1.52, line: { color: C.muted, width: 1.4, dashType: "dash" } });
  chip(s, "同一版本号", 6.95, 2.82, 1.7, 0.36, { fill: C.navy, color: "FFFFFF", size: 11 });
  chip(s, "Mask=1, XP=0 → v=3", 6.95, 3.24, 2.2, 0.32, { fill: C.blueSoft, color: C.navy, size: 10.5 });
  addText(s, "首次使用：按版本号取资产，按当前状态组装", M, 5.06, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "auto vh = VertexKey(\"TextureFillShader\", 0, profile);\n" +
    "auto fh = FragmentKey(\"TextureFillShader\", 3, profile);\n" +
    "auto frag = bundle->findFragment(fh);\n" +
    "auto pipeline = gpu->createRenderPipeline(desc);",
    M, 5.42, 7.0, 1.14);
  const rx = 8.0, rw = W - M - rx;
  box(s, rx, 5.42, rw, 1.14, C.white, C.orange, 1.4);
  addText(s, "Bundle 之外仍在运行时", rx + 0.2, 5.54, rw - 0.4, 0.3, { size: 12.5, color: C.orange, bold: true });
  addText(s, "后端编译 / 链接 · 创建 Module · 组装 Pipeline", rx + 0.2, 5.92, rw - 0.4, 0.5, { size: 12, color: C.text, lineSpacing: 1.25 });
  conclude(s, "Bundle 消除源码生成与前端编译，不消除后端编译/链接与 Pipeline 创建。", { color: C.orange });
  notes(s, "源码依据：src/gpu/shaders/level1/TextureFillShader.h:73-76 的混合进制编码同时服务构建期展开与运行时查找；src/gpu/PermutationMatcher.cpp 的运行时匹配（如 :585-586 的编码后索引）；src/gpu/ProgramInfo.cpp:177-216（findProgram → PrecompiledProgramCreator::CreateProgram）。删减说明：右侧代码为查找与组装的示意形态，压缩了类型与错误处理；两条泳道各合并了中间步骤。适用边界：OpenGL 的“后端编译/链接”指 driver 的 compile+link，Vulkan/Metal/WebGPU 指 module 与 pipeline 对象创建。");
}

// ---------------------------------------------------------------- 20
{
  const s = addBase("20", "开放组合 / OPEN COMPOSITION", "公开API可以递归产生开放的颜色结构");
  codeBlock(s,
    "auto image  = Shader::MakeImageShader(picture);\n" +
    "auto tinted = Shader::MakeBlend(BlendMode::kPlus,\n" +
    "                 image, Shader::MakeColorShader(color));\n" +
    "paint.setShader(Shader::MakeBlend(\n" +
    "    BlendMode::kSrcIn, tinted, mask));",
    M, 1.62, 6.3, 1.9);
  addText(s, "公开 API：Shader.h:58 MakeBlend 可任意嵌套", M, 3.68, 6.3, 0.3, { size: 11.5, color: C.muted, mono: true });
  const rx = 7.4;
  flowNode(s, "Blend(SrcIn)", rx + 1.5, 1.62, 2.4, 0.56, { size: 12.5 });
  vArrow(s, rx + 2.0, 2.2, 0.3); vArrow(s, rx + 3.4, 2.2, 0.3);
  flowNode(s, "Blend(Plus)", rx + 0.6, 2.54, 2.0, 0.56, { size: 12.5 });
  flowNode(s, "Mask", rx + 3.0, 2.54, 1.6, 0.56, { size: 12.5 });
  vArrow(s, rx + 1.1, 3.12, 0.3); vArrow(s, rx + 2.1, 3.12, 0.3);
  flowNode(s, "Image", rx + 0.2, 3.46, 1.4, 0.56, { size: 12.5 });
  flowNode(s, "ConstColor", rx + 1.8, 3.46, 1.8, 0.56, { size: 12.5 });
  chip(s, "深度与宽度都不受 API 限制", rx + 0.6, 4.4, 4.4, 0.46, { fill: C.redSoft, color: C.red, size: 12 });
  addText(s, "组合无界，但算子种类有限：采样、颜色矩阵、亮度、阈值、色彩空间、常量色、混合。", M, 5.2, W - 2 * M, 0.6, { size: 13, color: C.text, lineSpacing: 1.3 });
  conclude(s, "开放的是组合形状，不是算子集合。", { color: C.red });
  notes(s, "源码依据：include/tgfx/core/Shader.h:50-59（MakeImageShader / MakeBlend 公开签名，可递归嵌套）。删减说明：示例为三层嵌套的真实 API 调用，省略错误处理。适用边界：下游不改库且参数无界（项目约束），因此“开放组合”是必须接受的前提，不可通过收窄 API 消除。");
}

// ---------------------------------------------------------------- 21
{
  const s = addBase("21", "开放组合 / OPEN COMPOSITION", "运行时组合先转换为可验证的颜色关系图");
  addText(s, "Shader 嵌套树", M, 1.52, 3, 0.3, { size: 13, color: C.navy, bold: true });
  flowNode(s, "Blend", M + 0.9, 1.95, 1.6, 0.52, { size: 12 });
  vArrow(s, M + 1.3, 2.49, 0.26); vArrow(s, M + 2.1, 2.49, 0.26);
  flowNode(s, "ColorMatrix", M + 0.2, 2.79, 1.9, 0.52, { size: 12 });
  flowNode(s, "ConstColor", M + 2.3, 2.79, 1.9, 0.52, { size: 12 });
  vArrow(s, M + 1.15, 3.33, 0.26);
  flowNode(s, "Texture", M + 0.35, 3.63, 1.6, 0.52, { size: 12 });
  hArrow(s, 4.9, 3.0, 0.9);
  addText(s, "颜色关系图", 6.1, 1.52, 3, 0.3, { size: 13, color: C.navy, bold: true });
  const g = [
    { t: "① 几何颜色", x: 6.1, y: 2.0 }, { t: "② 纹理采样", x: 8.0, y: 2.0 },
    { t: "③ 颜色矩阵", x: 8.0, y: 3.0 }, { t: "④ 常量色", x: 6.1, y: 3.0 },
    { t: "⑤ 混合（③,④）", x: 7.0, y: 4.0 },
  ];
  g.forEach((n) => flowNode(s, n.t, n.x, n.y, 1.75, 0.56, { size: 11.5, fill: C.white, line: C.line, color: C.navy }));
  vArrow(s, 8.875, 2.58, 0.4, C.muted); vArrow(s, 7.85, 3.58, 0.4, C.muted);
  const rx = 10.3, rw = W - M - rx;
  box(s, rx, 1.95, rw, 3.0, C.white, C.line, 1.2);
  addText(s, "可验证的三条性质", rx + 0.2, 2.13, rw - 0.4, 0.3, { size: 12.5, color: C.navy, bold: true });
  addText(s, "节点种类 ∈ 有限算子集\n边只表达数据流\n无环 → 可拓扑排序求值", rx + 0.2, 2.53, rw - 0.4, 2.0, { size: 12, color: C.text, lineSpacing: 1.5 });
  conclude(s, "图把“任意组合”改写为“有限算子的任意接线”，从而可以逐项检查。");
  notes(s, "源码依据：src/gpu/AOTEffectDecomposer.cpp 的节点种类枚举与图校验（如 IsPointwiseTailOp :144-149 只接受四种一元颜色运算）。删减说明：图为第 20 页示例的关系化表示，省略坐标与几何细节。适用边界：只覆盖逐点颜色运算；含几何依赖或非局部效果的组合不在此转换范围内。");
}

// ---------------------------------------------------------------- 22
{
  const s = addBase("22", "开放组合 / OPEN COMPOSITION", "有界分支颜色关系可以作为运行时数据在单Pass内执行");
  addText(s, "分支图（两个纹理输入）", M, 1.52, 5, 0.3, { size: 13, color: C.navy, bold: true });
  flowNode(s, "T1 采样", M + 0.2, 1.95, 1.6, 0.52, { size: 12 });
  flowNode(s, "T2 采样", M + 2.3, 1.95, 1.6, 0.52, { size: 12 });
  vArrow(s, M + 1.0, 2.49, 0.28); vArrow(s, M + 3.1, 2.49, 0.28);
  flowNode(s, "③ 混合(①,②)", M + 1.25, 2.81, 1.85, 0.52, { size: 12 });
  vArrow(s, M + 2.17, 3.35, 0.28);
  flowNode(s, "④ 颜色矩阵(③)", M + 1.25, 3.67, 1.85, 0.52, { size: 12 });
  chip(s, "拓扑是数据，不是代码", M + 0.7, 4.5, 3.2, 0.46, { fill: C.blueSoft, color: C.navy, size: 12 });
  const rx = 5.4;
  addText(s, "同一份 FS 读取的运行时记录", rx, 1.52, 6, 0.3, { size: 13, color: C.navy, bold: true });
  addTable(s, [
    headRow(["记录", "运算", "输入 0", "输入 1", "参数"]),
    [{ text: "①" }, { text: "Sample" }, { text: "—" }, { text: "—" }, { text: "T1 绑定" }],
    [{ text: "②" }, { text: "Sample" }, { text: "—" }, { text: "—" }, { text: "T2 绑定" }],
    [{ text: "③" }, { text: "Blend" }, { text: "①" }, { text: "②" }, { text: "Plus" }],
    [{ text: "④" }, { text: "ColorMatrix" }, { text: "③" }, { text: "—" }, { text: "20 个 float" }],
  ], rx, 1.92, W - M - rx, undefined, { size: 11.5 });
  addText(s, "换一种接线 = 换一组记录，Shader 不变。", rx, 4.7, W - M - rx, 0.34, { size: 12.5, color: C.text });
  chip(s, "一个版本服务同预算内的任意拓扑", rx + 1.0, 5.2, W - M - rx - 2.0, 0.48, { fill: C.greenSoft, color: C.green, size: 12.5 });
  conclude(s, "接线放在 uniform 里，代码只有一份；单 Pass 内完成全部运算。", { color: C.green });
  notes(s, "源码依据：src/gpu/processors/AOTPointwiseChainProcessor.h:78-87（拓扑、运算种类、参数全部走 uniform，sampler 索引保持静态）；src/gpu/shaders/level1/PointwiseChainShader.h:25-30。删减说明：记录表为四节点示意，真实记录含更多参数位。适用边界：只覆盖逐点颜色 DAG；运算种类超出有限集合或超出预算时不走此路径。");
}

// ---------------------------------------------------------------- 23
{
  const s = addBase("23", "开放组合 / OPEN COMPOSITION", "单Pass的固定预算是16个记录和最多4个纹理输入");
  const bw = 3.4, gap = 0.5;
  box(s, M, 1.66, bw, 2.6, C.white, C.navy, 1.5);
  addText(s, "16", M, 1.9, bw, 0.75, { size: 40, color: C.navy, bold: true, align: "center", mono: true });
  addText(s, "颜色记录上限", M, 2.72, bw, 0.32, { size: 14, color: C.ink, bold: true, align: "center" });
  addText(s, "每个节点占一条记录；\n记录数 ≤ 16 才能单 Pass", M + 0.2, 3.12, bw - 0.4, 1.0, { size: 11.5, color: C.muted, align: "center", lineSpacing: 1.25 });
  const x2 = M + bw + gap;
  box(s, x2, 1.66, bw, 2.6, C.white, C.navy, 1.5);
  addText(s, "4", x2, 1.9, bw, 0.75, { size: 40, color: C.navy, bold: true, align: "center", mono: true });
  addText(s, "纹理输入上限", x2, 2.72, bw, 0.32, { size: 14, color: C.ink, bold: true, align: "center" });
  addText(s, "非空资产固定 4 个 sampler 接口；\n空链用 0 个的版本", x2 + 0.2, 3.12, bw - 0.4, 1.0, { size: 11.5, color: C.muted, align: "center", lineSpacing: 1.25 });
  const x3 = x2 + bw + gap;
  box(s, x3, 1.66, W - M - x3, 2.6, C.white, C.red, 1.5);
  addText(s, "当前路由口径", x3 + 0.22, 1.86, W - M - x3 - 0.44, 0.32, { size: 13.5, color: C.red, bold: true });
  chip(s, "接受：0 / 1 / 2 / 4 个真实纹理源", x3 + 0.22, 2.3, W - M - x3 - 0.44, 0.46, { fill: C.greenSoft, color: C.green, size: 12 });
  chip(s, "拒绝：3 个真实纹理源", x3 + 0.22, 2.88, W - M - x3 - 0.44, 0.46, { fill: C.redSoft, color: C.red, size: 12 });
  addText(s, "1-2 个真实源用占位 sampler 补齐到 4 个接口。", x3 + 0.22, 3.5, W - M - x3 - 0.44, 0.7, { size: 11.5, color: C.muted, lineSpacing: 1.25 });
  addText(s, "预算是资产的静态形状：超过 16 条记录或不在接受口径内的图，不进入单 Pass 路径。", M, 4.6, W - 2 * M, 0.6, { size: 13, color: C.text, lineSpacing: 1.3 });
  conclude(s, "预算硬边界：16 条记录、最多 4 个纹理输入；超出即改走其他路径。", { color: C.red });
  notes(s, "源码依据：src/gpu/processors/AOTPointwiseChainProcessor.h:91（MaxSlots = 16）；src/gpu/PermutationMatcher.cpp:703-707（非空链以四个 sampler 子节点出现，路由接受口径 0/1/2/4 真实源、3 拒绝，幻影补齐见 :115-119 与同文件 padding 逻辑）。删减说明：拒绝原因只保留路由口径，不展开双色彩空间等次级拒绝。适用边界：预算指单 Pass 分支路径；线性长链另有路径（第 24-25 页）。");
}

// ---------------------------------------------------------------- 24
{
  const s = addBase("24", "开放组合 / OPEN COMPOSITION", "长线性颜色链采用另一条补充执行路径");
  addTable(s, [
    headRow(["", "分支路径（任意有向无环图）", "线性路径（链式长组合）"]),
    [{ text: "图形状", options: { bold: true } }, { text: "任意有向无环图（含分支）" }, { text: "链：每个节点只有一个输入" }],
    [{ text: "预算", options: { bold: true } }, { text: "≤16 条记录，≤4 个纹理输入" }, { text: "每段 ≤2 个颜色运算，链长不限" }],
    [{ text: "执行", options: { bold: true } }, { text: "单 Pass 一次完成" }, { text: "按顺序拆成多个 Pass" }],
    [{ text: "适用", options: { bold: true } }, { text: "短而宽的组合" }, { text: "长而窄的组合" }],
  ], M, 1.56, W - 2 * M, [1.7, (W - 2 * M - 1.7) / 2, (W - 2 * M - 1.7) / 2], { size: 12 });
  addText(s, "为什么不矛盾", M, 4.2, 6, 0.3, { size: 13, color: C.navy, bold: true });
  addText(s, "链是图的特例，但长度无界：16 条记录装不下任意长的链。线性路径用另一种预算（每段 ≤2 个运算）换取链长不受限，代价是中间结果要落纹理。两条路径覆盖不同形状，都不在运行时生成代码。", M, 4.58, W - 2 * M, 1.3, { size: 12.5, color: C.text, lineSpacing: 1.35 });
  conclude(s, "16 记录单 Pass 与 2 运算多 Pass 是互补的两条局部路径，不是同一预算的例外。");
  notes(s, "源码依据：src/gpu/AOTEffectDecomposer.cpp:151-193（线性链拆分，slotCount < 2 即每段最多 2 个颜色运算）、:350（线性路径优先判定）。删减说明：不展开每段的 kernel 细节。适用边界：线性路径只接受一元颜色运算链（颜色矩阵/亮度/阈值/色彩空间），含分支即回到分支路径或拒绝。");
}

// ---------------------------------------------------------------- 25
{
  const s = addBase("25", "开放组合 / OPEN COMPOSITION", "线性颜色链按顺序拆分为多个Pass");
  addText(s, "链：Texture → ColorMatrix → Luma → ColorMatrix（4 个节点）", M, 1.52, W - 2 * M, 0.3, { size: 13, color: C.navy, bold: true });
  const py = 2.1;
  flowNode(s, "Pass 1\n纹理采样 + 颜色矩阵", M + 0.3, py, 2.9, 0.94, { size: 12.5, fill: C.white, line: C.navy });
  hArrow(s, M + 3.25, py + 0.47, 0.85);
  flowNode(s, "中间纹理", M + 4.15, py + 0.12, 1.8, 0.7, { size: 12, fill: C.orangeSoft, line: C.orange, color: C.orange });
  hArrow(s, M + 6.0, py + 0.47, 0.85);
  flowNode(s, "Pass 2\n亮度 + 颜色矩阵", M + 6.9, py, 2.9, 0.94, { size: 12.5, fill: C.white, line: C.navy });
  hArrow(s, M + 9.85, py + 0.47, 0.85);
  flowNode(s, "输出", M + 10.75, py + 0.12, 1.35, 0.7, { size: 12, fill: C.greenSoft, line: C.green, color: C.green });
  chip(s, "每个 Pass 最多 2 个颜色运算", M + 4.0, 3.4, 4.2, 0.44, { fill: C.blueSoft, color: C.navy, size: 12 });
  addTable(s, [
    headRow(["链节点", "所在 Pass", "段内序号"]),
    [{ text: "Texture（源）" }, { text: "Pass 1" }, { text: "源节点，不计运算" }],
    [{ text: "ColorMatrix #1" }, { text: "Pass 1" }, { text: "运算 1 / 2" }],
    [{ text: "Luma" }, { text: "Pass 2" }, { text: "运算 1 / 2" }],
    [{ text: "ColorMatrix #2" }, { text: "Pass 2" }, { text: "运算 2 / 2" }],
  ], M, 4.1, 6.4, [2.6, 1.9, 1.9], { size: 11.5 });
  addText(s, "链更长 → Pass 更多；顺序与结果不变，只是中间值落纹理。", 7.4, 4.2, W - M - 7.4, 1.6, { size: 12.5, color: C.text, lineSpacing: 1.35 });
  conclude(s, "拆分只改变中间结果的存放位置，不改变链的执行语义。");
  notes(s, "源码依据：src/gpu/AOTEffectDecomposer.cpp:167-192（首段含源节点、后续段依赖前一段、每段 slotCount < 2、中间段 materializesOutput）。删减说明：示例为 4 节点链的最小多 Pass 形态。适用边界：拆分仅对一元颜色运算链成立；准备失败仅在迁移期回放，终态不依赖 JIT。");
}

// ---------------------------------------------------------------- 26
{
  const s = addBase("26", "成本账本 / COST LEDGER", "AOT前移Shader成本，Multi-pass额外增加GPU工作");
  addTable(s, [
    headRow(["策略", "成本发生点", "首次绘制", "额外 GPU 工作"]),
    [{ text: "运行时编译" }, { text: "首次绘制时", options: { color: C.red } }, { text: "生成源码 + 编译 + 链接", options: { color: C.red } }, { text: "无" }],
    [{ text: "AOT 单 Pass" }, { text: "构建期", options: { color: C.green } }, { text: "加载资产 + 组装 Pipeline", options: { color: C.orange } }, { text: "无", options: { color: C.green } }],
    [{ text: "AOT 多 Pass" }, { text: "构建期", options: { color: C.green } }, { text: "加载资产 + 组装 Pipeline", options: { color: C.orange } }, { text: "中间纹理 + 多次绘制", options: { color: C.red } }],
  ], M, 1.56, W - 2 * M, [2.1, 1.9, 4.1, W - 2 * M - 8.1], { size: 12 });
  addText(s, "中间纹理的实际字节（以 128×128 为例）", M, 3.9, 8, 0.3, { size: 13, color: C.navy, bold: true });
  codeBlock(s,
    "128 × 128 × 4 B = 65,536 B ≈ 64 KB / 段\n每段一次写入 + 一次读取；N 段 → N-1 张中间纹理",
    M, 4.28, 6.6, 1.0);
  addText(s, "多 Pass 用 GPU 显存与带宽，换取消运行时编译；链越短越省，链越长越贵，但成本可预期。", 7.6, 4.2, W - M - 7.6, 1.6, { size: 12.5, color: C.text, lineSpacing: 1.35 });
  conclude(s, "AOT 把成本前移到构建期；多 Pass 是其中唯一新增 GPU 工作的路径。");
  notes(s, "依据：第 19 页（AOT 后仍有后端编译与 Pipeline 组装）、第 25 页（中间纹理物化）。字节计算：RGBA8 128×128×4 = 65,536 B。删减说明：只算一张中间纹理的稳态占用，不计分配策略与复用。适用边界：多 Pass 仅用于线性长链；单 Pass 路径无此项开销。");
}

// ---------------------------------------------------------------- 27
{
  const s = addBase("27", "包体收益 / BUNDLE SIZE", "AOT 的包体收益：分发 3.5 MB Bundle，工具链与静态库留在构建机");
  chip(s, "AOT 把编译工具链从运行时转移到构建机：应用只携带约 3.5 MB Bundle，构建中间产物不随应用分发。", M, 1.5, W - 2 * M, 0.5, { fill: C.greenSoft, color: C.green, size: 13 });
  const gap = 0.3, colW = (W - 2 * M - gap) / 2;
  const x1 = M, x2 = M + colW + gap;
  addText(s, "随应用分发的 Shader 资产", x1, 2.2, colW, 0.3, { size: 13, color: C.navy, bold: true });
  addText(s, "resources/shaders/ · 格式 v4 · zstd", x1, 2.52, colW, 0.24, { size: 10.5, color: C.muted });
  addTable(s, [
    headRow(["后端", "体积", "条目数"]),
    [{ text: "Metal" }, { text: "1.14 MB", options: { mono: true } }, { text: "101+274=375", options: { mono: true } }],
    [{ text: "Vulkan" }, { text: "1.77 MB", options: { mono: true } }, { text: "101+274=375", options: { mono: true } }],
    [{ text: "OpenGL" }, { text: "153 KB", options: { mono: true } }, { text: "101+274=375", options: { mono: true } }],
    [{ text: "WebGPU" }, { text: "545 KB", options: { mono: true } }, { text: "99+184=283", options: { mono: true } }],
    [{ text: "合计", options: { bold: true } }, { text: "约 3.5 MB", options: { mono: true, bold: true, color: C.green } }, { text: "四后端分发", options: { bold: true, color: C.green } }],
  ], x1, 2.86, colW, [0.95, 1.1, colW - 2.05], { size: 11 });
  addText(s, "WebGPU 为 WGSL 文本：未压缩约 8.4 MB，zstd 压缩比约 15:1。", x1, 4.92, colW, 0.5, { size: 10.5, color: C.muted, lineSpacing: 1.2 });
  box(s, x1, 5.5, colW, 0.98, C.white, C.green, 1.5);
  box(s, x1, 5.5, colW, 0.08, C.green, C.green, 0);
  addText(s, "≈ 3.5 MB", x1 + 0.18, 5.68, 1.85, 0.6, { size: 24, color: C.green, bold: true, mono: true });
  addText(s, "四后端合计，随应用分发的全部 Shader 资产。", x1 + 2.05, 5.62, colW - 2.23, 0.74, { size: 10.5, color: C.text, lineSpacing: 1.2, valign: "mid" });
  addText(s, "留在构建机的中间产物", x2, 2.2, colW, 0.3, { size: 13, color: C.navy, bold: true });
  addText(s, "仅存在于构建目录 · 不随应用分发", x2, 2.52, colW, 0.24, { size: 10.5, color: C.muted });
  addTable(s, [
    headRow(["中间产物", "体积 / 用途"]),
    [{ text: "libshader-tool-vendor.a", options: { mono: true } }, { text: "约 19 MB / 后端 · 编译工具链", options: { mono: true } }],
    [{ text: "tgfx.a", options: { mono: true } }, { text: "Metal 418 / Vulkan 423 / OpenGL 398 MB · 完整静态库", options: { mono: true } }],
  ], x2, 2.86, colW, [2.35, colW - 2.35], { size: 11 });
  addText(s, "WebGPU 的 libtgfx_zstd.a 仅为嵌入 Bundle 的 zstd 压缩辅助库；完整 libtgfx 未在当前构建中生成。", x2, 4.0, colW, 0.5, { size: 10.5, color: C.muted, lineSpacing: 1.2 });
  addText(s, "CI 最终上传的是应用包或 Web 产物，静态库和工具链库仅作为链接输入，不进入分发包。", x2, 4.66, colW, 0.62, { size: 11, color: C.text, lineSpacing: 1.25 });
  box(s, x2, 5.5, colW, 0.98, C.white, C.red, 1.5);
  box(s, x2, 5.5, colW, 0.08, C.red, C.red, 0);
  addText(s, "0 B", x2 + 0.18, 5.68, 1.85, 0.6, { size: 24, color: C.red, bold: true, mono: true });
  addText(s, "中间产物不进入应用包或 Web 产物。", x2 + 2.05, 5.62, colW - 2.23, 0.74, { size: 10.5, color: C.text, lineSpacing: 1.2, valign: "mid" });
  conclude(s, "Bundle 是应用携带的 Shader 资产；工具链库与静态库只用于构建链接，这是 AOT 的包体收益。", { color: C.green });
  notes(s, "数据来源：resources/shaders/ 当前产物（格式 v4，zstd 压缩）：Metal 1.14 MB、Vulkan 1.77 MB、OpenGL 153 KB、WebGPU 545 KB（WGSL 文本未压缩约 8.4 MB，压缩比约 15:1）；条目数 101 VS + 274 FS = 375（WebGPU 99 VS + 184 FS = 283）。工具链库：cmake-build-*/CMakeFiles/shader-tool-vendor.dir/arm64/libshader-tool-vendor.a 各后端约 19 MB；WebGPU 另需 tint。静态库：cmake-build-* 根目录 tgfx.a，Metal 418 MB、Vulkan 423 MB、OpenGL 398 MB；WebGPU 的 libtgfx_zstd.a 仅为嵌入 Bundle 的 zstd 压缩辅助库，完整 libtgfx 未在当前构建中生成。两列口径：左列为随应用分发的 Bundle 资产，右列为仅存在于构建目录的中间产物——CI 最终上传的是应用包或 Web 产物，静态库与工具链库仅作为链接输入，不进入分发包。删减说明：右列不再区分工具链库与静态库两个小类，合并为一张两行表。适用边界：所有数字为当前构建快照，随构建配置与变体集演进变化；静态库体积为 Release 配置产物，分发体积以 Bundle 为准。");
}

// ---------------------------------------------------------------- 28
{
  const s = addBase("28", "证据门禁 / EVIDENCE GATES", "测试已定义正确性门禁，但当前版本仍需独立执行记录");
  const steps = [
    { t: "规则迁移完成", d: "30/30 编译清单由规则生成", c: C.green },
    { t: "生成端验证", d: "WebGPU 532/546，真实 miss = 0", c: C.green },
    { t: "AOT-vs-JIT 门禁", d: "同输入双路径逐像素对比已建成", c: C.green },
    { t: "当前版本独立执行", d: "每个后端单独运行并留存记录", c: C.orange },
  ];
  steps.forEach((st, i) => {
    const x = M + i * 2.95, y = 4.32 - i * 0.85;
    box(s, x, y, 2.7, 0.92, C.white, st.c, 1.6);
    box(s, x, y, 2.7, 0.08, st.c, st.c, 0);
    addText(s, st.t, x + 0.16, y + 0.14, 2.4, 0.3, { size: 12.5, color: st.c, bold: true });
    addText(s, st.d, x + 0.16, y + 0.46, 2.4, 0.42, { size: 10, color: C.muted, lineSpacing: 1.1 });
  });
  chip(s, "测试源码定义了正确性标准", M, 5.52, 3.6, 0.46, { fill: C.greenSoft, color: C.green, size: 12 });
  chip(s, "≠ 当前工作树已执行通过", M + 3.8, 5.52, 3.4, 0.46, { fill: C.redSoft, color: C.red, size: 12 });
  addText(s, "发布口径：以各后端当次独立执行的记录为准，不以门禁存在代替执行证据。", M + 7.4, 5.49, W - M - (M + 7.4), 0.6, { size: 11.5, color: C.muted, lineSpacing: 1.2 });
  conclude(s, "门禁回答“什么算对”；执行记录回答“这次对不对”，两者不可互相替代。", { color: C.orange });
  notes(s, "依据：test/src/ShaderPermutationTest.cpp（含 findProgram 双键对比 :891-892）与 AOT-vs-JIT 门禁测试；WebGPU 本地验证链 web/test（npm run test:webgpu），生成端 532/546、真实 miss 0 为 2026-08-25 快照；规则迁移 30/30 为 2026-08-19 项目记录。删减说明：阶梯四级各保留一句。适用边界：所有数字是各自日期的快照；当前工作树是否通过必须当次执行确认。");
}

// ---------------------------------------------------------------- 29
{
  const s = addBase("29", "发布路径 / ROADMAP", "后续工作：把 AOT 从“可运行”推进到“可发布”");
  const gapX = 0.4, gapY = 0.35;
  const cw = (W - 2 * M - gapX) / 2, ch = 2.3;
  const quads = [
    { t: "正确性与效果验证", c: C.red, soft: C.redSoft, items: ["AOT 与 JIT 像素一致性基线", "多后端一致性门禁", "回归基线机制"] },
    { t: "性能与负载", c: C.orange, soft: C.orangeSoft, items: ["冷启动与首帧延迟分项测量", "Bundle 体积与加载时间预算", "真实负载回放统计 hitRate / fallback / multi-pass"] },
    { t: "产品接入与覆盖", c: C.green, soft: C.greenSoft, items: ["libpag 接入收集 miss", "ardot 接入覆盖复杂合成", "按真实负载优先补充低成本变体"] },
    { t: "开发调试与工具链", c: C.blue, soft: C.blueSoft, items: ["AOT 诊断面板：hit/miss、当前 Shader、变体、Pass、降级原因", "规则覆盖率报告纳入 CI", "自动化基线更新工具"] },
  ];
  quads.forEach((q, i) => {
    const x = M + (i % 2) * (cw + gapX), y = 1.56 + Math.floor(i / 2) * (ch + gapY);
    box(s, x, y, cw, ch, C.white, q.c, 1.5);
    box(s, x, y, cw, 0.1, q.c, q.c, 0);
    addText(s, q.t, x + 0.22, y + 0.2, cw - 0.44, 0.3, { size: 14, color: q.c, bold: true });
    q.items.forEach((it, j) => {
      chip(s, it, x + 0.22, y + 0.62 + j * 0.56, cw - 0.44, 0.46, { fill: q.soft, color: q.c, size: 11.5 });
    });
  });
  conclude(s, "正确性基线决定能否发布；真实负载决定覆盖范围；工具链决定长期维护成本。");
  notes(s, "依据：第 28 页证据门禁缺口（当前版本仍需独立执行记录）导出第一象限；第 26 页成本账本与第 27 页包体数据导出第二象限的测量项；开放组合三页（20-25）的覆盖边界导出第三象限；第 17 页“清单唯一作者是规则”导出第四象限的 CI 覆盖率报告。删减说明：每象限压缩为 2-3 条可执行任务，不展开排期与责任人。适用边界：本页是方向清单，不是承诺；优先级随真实负载数据调整。");
}

pptx.writeFile({ fileName: "TGFX-AOT-复合绘制架构.pptx" }).then(() => {
  console.log("OK: TGFX-AOT-复合绘制架构.pptx, slides = " + page);
});
