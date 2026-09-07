// Generates the TGFX Shader Permutation report deck (pptx) from the design doc content.
// Design system: flat, 3-color palette (NAVY / TEAL / CORAL), 2 fonts (PingFang SC + SF Mono),
// unified grid, one core message per slide, no icon badges, no drop shadows.
// Run: node generate.js
const pptxgen = require("pptxgenjs");

// ---------- design tokens ----------
const NAVY_DARK = "0D1230"; // cover / closing background
const NAVY = "10173A"; // structural dark: header band, dark panels, table header
const TEAL = "00B8A9"; // sole accent color
const CORAL = "E94F4F"; // sole warning color — reserved for genuine risk callouts only
const WHITE = "FFFFFF";
const BG_LIGHT = "F7F8FC";
const TEXT_DARK = "1A2452";
const TEXT_MUTED = "6B7494";
const CARD_BORDER = "E3E6F2";
const ICE = "C7CEEF";
const TINT_TEAL = "E6F7F5";
const TINT_CORAL = "FCEBEB";

const FONT = "PingFang SC";
const MONO = "SF Mono";

// ---------- grid ----------
const PAGE_W = 13.333, PAGE_H = 7.5;
const MARGIN = 1.0;
const CONTENT_W = PAGE_W - MARGIN * 2; // 11.333
const CONTENT_TOP = 1.85;
const CONTENT_BOTTOM = 6.7;

function cols(n, gap) {
  const w = (CONTENT_W - gap * (n - 1)) / n;
  return Array.from({ length: n }, (_, i) => ({ x: MARGIN + i * (w + gap), w }));
}

// ---------- shared building blocks ----------
function header(slide, kicker, title) {
  slide.background = { color: BG_LIGHT };
  slide.addShape("rect", { x: 0, y: 0, w: PAGE_W, h: 0.05, fill: { color: NAVY }, line: { type: "none" } });
  slide.addShape("rect", { x: MARGIN, y: 0.52, w: 0.14, h: 0.14, fill: { color: TEAL }, line: { type: "none" } });
  slide.addText(kicker.toUpperCase(), {
    x: MARGIN + 0.26, y: 0.46, w: 9, h: 0.28, fontFace: FONT, fontSize: 13, color: TEAL, bold: true, charSpacing: 2, margin: 0,
  });
  slide.addText(title, {
    x: MARGIN, y: 0.82, w: CONTENT_W, h: 0.75, fontFace: FONT, fontSize: 28, color: TEXT_DARK, bold: true, margin: 0, lineSpacingMultiple: 1.08,
  });
  slide.addShape("line", { x: MARGIN, y: 1.62, w: CONTENT_W, h: 0, line: { color: CARD_BORDER, width: 1 } });
}

function footer(slide, pageNum, section, opts = {}) {
  const color = opts.color || TEXT_MUTED;
  const lineColor = opts.lineColor || CARD_BORDER;
  slide.addShape("line", { x: MARGIN, y: 6.85, w: CONTENT_W, h: 0, line: { color: lineColor, width: 0.75 } });
  slide.addText([
    { text: "TGFX  ", options: { bold: true, color: opts.brandColor || TEAL } },
    { text: "·  " + section, options: { color } },
  ], { x: MARGIN, y: 6.95, w: 9, h: 0.3, fontFace: FONT, fontSize: 11, align: "left", margin: 0 });
  slide.addText(String(pageNum), {
    x: PAGE_W - MARGIN - 0.4, y: 6.95, w: 0.4, h: 0.3, fontFace: FONT, fontSize: 11, color, align: "right", margin: 0,
  });
}

function panel(slide, x, y, w, h, opts = {}) {
  slide.addShape("roundRect", {
    x, y, w, h, rectRadius: 0.05,
    fill: { color: opts.fill || WHITE },
    line: { color: opts.line || CARD_BORDER, width: 1 },
  });
}

function bullets(slide, items, x, y, w, h, opts = {}) {
  const size = opts.fontSize || 16;
  const arr = items.map((t, i) => ({
    text: t,
    options: {
      bullet: { code: "25AA", color: opts.bulletColor || TEAL, indent: 20 },
      breakLine: i < items.length - 1,
      color: opts.color || TEXT_DARK,
      fontSize: size,
      paraSpaceAfter: opts.spaceAfter != null ? opts.spaceAfter : 12,
    },
  }));
  slide.addText(arr, { x, y, w, h, fontFace: FONT, valign: "top", margin: 0, lineSpacingMultiple: 1.22 });
}

function numberedList(slide, items, x, y, w, h, opts = {}) {
  const rowH = opts.rowH || (h / items.length);
  items.forEach((t, i) => {
    const ry = y + i * rowH;
    slide.addShape("ellipse", { x, y: ry + 0.03, w: 0.4, h: 0.4, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText(String(i + 1), { x, y: ry + 0.03, w: 0.4, h: 0.4, fontFace: FONT, fontSize: 14, bold: true, color: WHITE, align: "center", valign: "middle", margin: 0 });
    slide.addText(t, { x: x + 0.6, y: ry, w: w - 0.6, h: rowH, fontFace: FONT, fontSize: opts.fontSize || 15, color: TEXT_DARK, valign: "middle", margin: 0, lineSpacingMultiple: 1.2 });
  });
}

// Horizontal timeline: title above the dot, description below the dot.
function timeline(slide, steps, x, y, w, opts = {}) {
  const n = steps.length;
  const stepW = w / n;
  const lineY = y + (opts.lineOffset || 0.85);
  slide.addShape("line", { x: x + stepW * 0.5, y: lineY, w: w - stepW, h: 0, line: { color: CARD_BORDER, width: 1.5 } });
  steps.forEach((s, i) => {
    const cx = x + stepW * (i + 0.5);
    slide.addShape("ellipse", { x: cx - 0.12, y: lineY - 0.12, w: 0.24, h: 0.24, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText(String(i + 1), { x: cx - 0.12, y: lineY - 0.12, w: 0.24, h: 0.24, fontFace: FONT, fontSize: 9.5, bold: true, color: WHITE, align: "center", valign: "middle", margin: 0 });
    slide.addText(s.t, {
      x: x + i * stepW + 0.05, y, w: stepW - 0.1, h: opts.lineOffset ? opts.lineOffset - 0.15 : 0.7,
      fontFace: FONT, fontSize: 14.5, bold: true, color: TEXT_DARK, align: "center", valign: "bottom", margin: 0, lineSpacingMultiple: 1.1,
    });
    if (s.d) {
      slide.addText(s.d, {
        x: x + i * stepW + 0.08, y: lineY + 0.28, w: stepW - 0.16, h: opts.descH || 1.15,
        fontFace: FONT, fontSize: 12.5, color: TEXT_MUTED, align: "center", valign: "top", margin: 0, lineSpacingMultiple: 1.25,
      });
    }
  });
}

function callout(slide, x, y, w, h, text, opts = {}) {
  const tint = opts.tone === "coral" ? TINT_CORAL : TINT_TEAL;
  const accent = opts.tone === "coral" ? CORAL : TEAL;
  slide.addShape("rect", { x, y, w, h, fill: { color: tint }, line: { type: "none" } });
  slide.addShape("rect", { x, y, w: 0.07, h, fill: { color: accent }, line: { type: "none" } });
  slide.addText(text, {
    x: x + 0.4, y, w: w - 0.7, h, fontFace: FONT, fontSize: opts.fontSize || 14, bold: opts.bold, color: opts.color || TEXT_DARK, valign: "middle", margin: 0, lineSpacingMultiple: 1.25,
  });
}

function note(slide, text, x, y, w, h, opts = {}) {
  slide.addText(text, {
    x, y, w, h, fontFace: FONT, fontSize: opts.fontSize || 13, italic: true, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.25, align: opts.align || "left",
  });
}

function statBox(slide, x, y, w, h, number, label, desc, opts = {}) {
  panel(slide, x, y, w, h, { fill: NAVY, line: NAVY });
  slide.addText(number, { x: x + 0.35, y: y + 0.32, w: w - 0.7, h: 0.75, fontFace: MONO, fontSize: 32, bold: true, color: WHITE, margin: 0 });
  slide.addText(label, { x: x + 0.35, y: y + 1.1, w: w - 0.7, h: 0.35, fontFace: FONT, fontSize: 15, bold: true, color: TEAL, margin: 0 });
  slide.addText(desc, { x: x + 0.35, y: y + 1.5, w: w - 0.7, h: h - 1.75, fontFace: FONT, fontSize: 12.5, color: ICE, margin: 0, lineSpacingMultiple: 1.25 });
}

function codeBlock(slide, x, y, w, h, title, runs, opts = {}) {
  panel(slide, x, y, w, h, { fill: BG_LIGHT });
  slide.addShape("rect", { x, y, w, h: 0.42, fill: { color: NAVY }, line: { type: "none" } });
  slide.addText(title, { x: x + 0.22, y, w: w - 0.44, h: 0.42, fontFace: MONO, fontSize: 12.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
  slide.addText(runs, {
    x: x + 0.28, y: y + 0.58, w: w - 0.56, h: h - 0.8, fontFace: MONO, fontSize: opts.fontSize || 13.5, margin: 0, lineSpacingMultiple: 1.4, valign: "top",
  });
}

function table(slide, rows, x, y, w, h, colW, opts = {}) {
  slide.addTable(rows, {
    x, y, w, h, fontFace: FONT, fontSize: opts.fontSize || 13, color: TEXT_DARK,
    border: { pt: 0.5, color: CARD_BORDER }, fill: { color: WHITE }, colW, valign: "middle", autoPage: false,
  });
}
function headCell(text) {
  return { text, options: { fill: { color: NAVY }, color: WHITE, bold: true, fontSize: 12.5 } };
}

function darkSlide(pres) {
  const slide = pres.addSlide();
  slide.background = { color: NAVY_DARK };
  return slide;
}

// ============================================================
async function main() {
  const pres = new pptxgen();
  pres.layout = "LAYOUT_WIDE"; // 13.333 x 7.5
  pres.author = "TGFX Graphics Team";
  pres.title = "TGFX Shader Permutation 系统设计方案";

  let page = 0;
  const SECTION = "TGFX Shader Permutation 系统设计方案";

  // ---------------- Slide 1: Cover ----------------
  {
    page++;
    const slide = darkSlide(pres);
    slide.addShape("rect", { x: MARGIN, y: 2.5, w: 0.5, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText("架构设计汇报", {
      x: MARGIN, y: 2.7, w: 8, h: 0.4, fontFace: FONT, fontSize: 14, color: TEAL, bold: true, charSpacing: 3, margin: 0,
    });
    slide.addText("Shader Permutation 预编译系统", {
      x: MARGIN, y: 3.2, w: 11, h: 1.2, fontFace: FONT, fontSize: 40, color: WHITE, bold: true, margin: 0,
    });
    slide.addText("消除运行时 Shader 拼接与跨语言翻译，构建期编译、运行时查表", {
      x: MARGIN, y: 4.35, w: 10.5, h: 0.5, fontFace: FONT, fontSize: 17, color: ICE, margin: 0,
    });
    slide.addText("TGFX 图形引擎  ·  GPU 渲染管线", {
      x: MARGIN, y: 6.7, w: 10, h: 0.35, fontFace: FONT, fontSize: 12, color: "8291D6", margin: 0,
    });
  }

  // ---------------- Slide 2: 现状 · 拼接三步 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "问题背景", "现状：Shader 拼接开销卡在渲染关键路径上");

    note(slide,
      "DrawOp::execute() 关键路径上，ProgramBuilder::CreateProgram() 每遇到新的 programKey 都要完成以下三步：",
      MARGIN, CONTENT_TOP, CONTENT_W, 0.6, { fontSize: 15, italic: false });

    timeline(slide, [
      { t: "拼接 GLSL", d: "递归拼接 GP / FP[] / XP\n的 emitCode()，生成源码文本" },
      { t: "编译 SPIR-V", d: "调用 shaderc\n完成编译" },
      { t: "翻译目标语言", d: "SPIRV-Cross / Tint\n翻译为 MSL / WGSL" },
    ], MARGIN, 3.0, CONTENT_W, { lineOffset: 0.75, descH: 1.3 });

    callout(slide, MARGIN, 5.9, CONTENT_W, 0.7,
      "三者均发生在渲染提交路径上，而非构建期 —— 本应一次性的编译工作，被摊到了每一次首次命中的渲染帧里",
      { tone: "coral", fontSize: 14.5, bold: true, color: CORAL });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 3: 代价 · 两个数字 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "问题背景", "代价：首帧延迟与运行时体积");

    const c = cols(2, 0.5);
    statBox(slide, c[0].x, CONTENT_TOP, c[0].w, 2.9,
      "15–70 ms", "首帧延迟（非实测）",
      "按拼接 / 编译 / 翻译各阶段单次操作耗时（1–5 ms/次量级）乘以首帧预计命中的新 programKey 数量推算");
    statBox(slide, c[1].x, CONTENT_TOP, c[1].w, 2.9,
      "9–14 MB", "运行时编译器体积",
      "shaderc + SPIRV-Cross + Tint，Release arm64 静态库常驻运行时二进制");

    note(slide,
      "两项均为估算值，非端到端实测，未做逐符号核实。本方案不依赖这两个数字支撑架构决策，仅作动机参考。",
      MARGIN, 5.05, CONTENT_W, 0.9, { fontSize: 13.5 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 4: 根因分析 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "根因分析", "两点根本原因，导致无法在构建期完成编译");

    const c = cols(2, 0.5);
    slide.addText("原因一 · 时机错配", { x: c[0].x, y: CONTENT_TOP, w: c[0].w, h: 0.45, fontFace: FONT, fontSize: 18, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "拼接 GLSL、编译 SPIR-V、跨语言翻译三步都发生在 DrawOp::execute() 提交路径上",
      "本应一次性完成的编译工作，被摊到了每一次首次命中的渲染帧里",
    ], c[0].x, CONTENT_TOP + 0.6, c[0].w, 2.9, { fontSize: 15 });

    slide.addText("原因二 · 组合空间无界", { x: c[1].x, y: CONTENT_TOP, w: c[1].w, h: 0.45, fontFace: FONT, fontSize: 18, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "现有 programKey = GP.key + FP[].key（递归）+ XP.key 的拼接",
      "FP 树的嵌套深度由用户通过 Shader::MakeBlend() 在运行时决定",
      "以“整棵 Processor 树”为预编译单元，构建期无法穷举",
    ], c[1].x, CONTENT_TOP + 0.6, c[1].w, 2.9, { fontSize: 15, spaceAfter: 10 });

    callout(slide, MARGIN, 5.9, CONTENT_W, 0.7,
      "结论：并非“做不到构建期编译”，而是预编译粒度定得太粗——粗到覆盖了一个只能在运行时确定的开放空间",
      { fontSize: 14.5, bold: true, color: TEXT_DARK });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 5: 设计目标 Before/After ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "设计目标", "构建期完成编译，运行时只查表");

    const c = cols(2, 0.5);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.0, { fill: TINT_CORAL, line: CARD_BORDER });
    slide.addText("BEFORE", { x: c[0].x + 0.35, y: CONTENT_TOP + 0.25, w: 4, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: CORAL, charSpacing: 2, margin: 0 });
    slide.addText("运行时拼接 + 编译 + 翻译", { x: c[0].x + 0.35, y: CONTENT_TOP + 0.6, w: c[0].w - 0.7, h: 0.45, fontFace: FONT, fontSize: 18, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "每个新 programKey 触发一次 emitCode() 拼接",
      "shaderc → SPIRV-Cross / Tint 现场翻译",
      "首帧成本：15–70 ms* 累积卡顿",
      "三套编译器需常驻运行时二进制",
    ], c[0].x + 0.35, CONTENT_TOP + 1.2, c[0].w - 0.7, 2.6, { fontSize: 14 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.0, { fill: TINT_TEAL, line: CARD_BORDER });
    slide.addText("AFTER", { x: c[1].x + 0.35, y: CONTENT_TOP + 0.25, w: 4, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: TEAL, charSpacing: 2, margin: 0 });
    slide.addText("构建期编译，运行时查表", { x: c[1].x + 0.35, y: CONTENT_TOP + 0.6, w: c[1].w - 0.7, h: 0.45, fontFace: FONT, fontSize: 18, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "shader_build_tool 在 CMake 构建期枚举全部 Permutation",
      "运行时按 PipelineKey 二分查找已编译产物",
      "首帧成本：0（查表，不再拼接 / 翻译）",
      "瘦身构建下可移除运行时编译器依赖",
    ], c[1].x + 0.35, CONTENT_TOP + 1.2, c[1].w - 0.7, 2.6, { fontSize: 14 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 6: 关键约束 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "设计目标", "三条关键约束（评审要点）");

    numberedList(slide, [
      "用 C++ 类型系统声明变种维度，不依赖 shader 注释解析",
      "以单 Pass 为主，多 Pass 仅作复杂 Processor 组合的兜底手段",
      "RuntimeEffect（用户自定义着色器）保持独立运行时编译路径，不受本方案影响",
    ], MARGIN, CONTENT_TOP + 0.3, CONTENT_W, 3.6, { fontSize: 16, rowH: 1.15 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 7: 方案总览 · 构建期 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "方案总览 · 构建期", "从声明到 Bundle：构建期编译流程");

    timeline(slide, [
      { t: "开发者编写", d: ".h 声明维度\n.frag/.vert 逻辑" },
      { t: "自注册", d: "ShaderRegistry\n静态收集全部 Shader" },
      { t: "枚举组合", d: "PermutationDomain\n笛卡尔积展开" },
      { t: "裁剪", d: "ShouldCompile()\n剔除不可达组合" },
      { t: "编译产出", d: "GLSL→SPIR-V→MSL/WGSL\n打包 Bundle" },
    ], MARGIN, 1.95, CONTENT_W, { lineOffset: 0.75, descH: 1.15 });

    panel(slide, MARGIN, 4.5, CONTENT_W, 1.75, { fill: NAVY, line: NAVY });
    slide.addText("产物", { x: MARGIN + 0.35, y: 4.72, w: 2, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: TEAL, margin: 0 });
    slide.addText("build/generated/shader_bundle.{opengl|vulkan|metal|webgpu}.bin", {
      x: MARGIN + 0.35, y: 5.05, w: CONTENT_W - 0.7, h: 0.42, fontFace: MONO, fontSize: 15, bold: true, color: WHITE, margin: 0,
    });
    slide.addText("随 App 包体打入；每个已编译 Permutation 对应一条可二分查找的索引项", {
      x: MARGIN + 0.35, y: 5.55, w: CONTENT_W - 0.7, h: 0.5, fontFace: FONT, fontSize: 13, color: ICE, margin: 0,
    });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 8: 方案总览 · 运行时 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "方案总览 · 运行时", "从 draw 调用到 GPU 提交：运行时查表流程");

    timeline(slide, [
      { t: "分解 Processor 树", d: "EffectDecomposer\n::Decompose()" },
      { t: "计算 PipelineKey", d: "shaderName + index\n+ 渲染状态" },
      { t: "查表", d: "PrecompiledShaderCache\n::find()" },
      { t: "Program 缓存", d: "复用现有 GlobalCache\nLRU" },
      { t: "提交 GPU", d: "createShaderModule\ncreateRenderPipeline" },
    ], MARGIN, 1.95, CONTENT_W, { lineOffset: 0.75, descH: 1.0 });

    const c = cols(2, 0.4);
    callout(slide, c[0].x, 4.35, c[0].w, 0.95, "命中已注册 Shader Permutation → 单 Pass 直接查表", { fontSize: 13.5, bold: true, color: TEXT_DARK });
    callout(slide, c[1].x, 4.35, c[1].w, 0.95, "复合效果未命中 → 拆分为多个 Pass，每段仍各自查表，不产生拼接", { tone: "coral", fontSize: 13.5, bold: true, color: TEXT_DARK });

    note(slide, "不再调用 ProgramBuilder::CreateProgram() / emitCode() 拼接字符串", MARGIN, 5.55, CONTENT_W, 0.4, { fontSize: 14, align: "center" });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 9: 核心架构决策 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "核心架构决策", "预编译单元：Shader 类的 Permutation");

    panel(slide, MARGIN, CONTENT_TOP, CONTENT_W, 1.15, { fill: NAVY, line: NAVY });
    slide.addText([
      { text: "决策：", options: { bold: true, color: TEAL } },
      { text: "以「一个 C++ Shader 描述类声明的全部 Permutation」为预编译单元，", options: { color: WHITE } },
      { text: "不是", options: { bold: true, color: CORAL } },
      { text: "「某一次 draw 对应的完整 Processor 树」", options: { color: WHITE } },
    ], { x: MARGIN + 0.35, y: CONTENT_TOP + 0.15, w: CONTENT_W - 0.7, h: 0.85, fontFace: FONT, fontSize: 15.5, valign: "middle", margin: 0, lineSpacingMultiple: 1.25 });

    slide.addText("可行性依据", { x: MARGIN, y: 3.35, w: 6, h: 0.4, fontFace: FONT, fontSize: 17, bold: true, color: TEXT_DARK, margin: 0 });

    numberedList(slide, [
      "判断“一次 draw 能否预编译”，等价于判断它能否被分解为若干已注册 Shader 类的具体取值——这是封闭、可枚举的问题",
      "每个 Shader 类的 Permutation 维度都是引擎内部枚举（有限集合），构建期可以完整穷举",
      "命中的走单 Pass；命不中的复合效果（colorFilter + maskFilter + 高级 BlendMode 等）才拆多 Pass，每段仍是预编译产物",
    ], MARGIN, 3.85, CONTENT_W, 2.7, { fontSize: 14.5, rowH: 0.9 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 10: 完备性保证 · 判定表 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "完备性保证", "单 Pass 优先、多 Pass 兜底的判定表");

    const rows = [
      [headCell("场景"), headCell("处理方式")],
      ["GP + 单个 colorFP（纹理/渐变/常量色）+ 可选 coverageFP", { text: "单 Pass", options: { color: TEAL, bold: true } }],
      ["+ 类型有限的 colorFilter（ColorMatrix / AlphaThreshold 等）", { text: "单 Pass", options: { color: TEAL, bold: true } }],
      ["+ GaussianBlur（maskFilter 场景）", { text: "多 Pass", options: { color: CORAL, bold: true } }],
      ["ColorFilter + MaskFilter + 高级 BlendMode 同时出现", { text: "多 Pass", options: { color: CORAL, bold: true } }],
      ["Shader::MakeBlend() 递归嵌套子 Shader（唯一无界位置）", { text: "多 Pass（逐层拆解）", options: { color: CORAL, bold: true } }],
    ];
    table(slide, rows, MARGIN, CONTENT_TOP, CONTENT_W, 3.9, [7.9, 3.4], { fontSize: 14 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 11: 完备性保证 · 三层机制 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "完备性保证", "三层机制共同保证覆盖，均可在构建期验证");

    const mechs = [
      { t: "构建期检查", d: "扫描全部已注册 Processor 子类，核对匹配表是否有对应规则，缺失即报错阻断" },
      { t: "递归天然终止", d: "叶子 FP 类型有限，Decompose() 对任意深度的树都能在有限步内终止" },
      { t: "过渡期兜底", d: "未覆盖的组合保留 ProgramBuilder::CreateProgram() 动态拼接，不中断渲染" },
    ];
    const c = cols(3, 0.4);
    mechs.forEach((m, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 3.5);
      slide.addShape("rect", { x: c[i].x, y: CONTENT_TOP, w: c[i].w, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
      slide.addText(m.t, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.35, w: c[i].w - 0.6, h: 0.5, fontFace: FONT, fontSize: 16, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(m.d, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.95, w: c[i].w - 0.6, h: 2.4, fontFace: FONT, fontSize: 13.5, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 });
    });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 12: 变体规模评估 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "变体规模评估", "Permutation 变体规模评估结果");

    const stats = [
      { n: "256", l: "TextureEffect 裸组合数", d: "8 个独立 bool 维度，2⁸=256；裁剪结构性不可达组合后更少" },
      { n: "648", l: "TiledTextureEffect 裸组合数", d: "X/Y 两轴各 9 种 ShaderMode + isAlphaOnly + constraint + hasPerspective" },
      { n: "32", l: "PerlinNoise（待决议）", d: "已核对源码：3 个独立维度，8×2×2=32；上限收紧属公开 API 变更，需单独评审" },
    ];
    const c = cols(3, 0.4);
    stats.forEach((s, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 2.6, { fill: NAVY, line: NAVY });
      slide.addText(s.n, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.25, w: c[i].w - 0.6, h: 0.65, fontFace: MONO, fontSize: 30, bold: true, color: TEAL, margin: 0 });
      slide.addText(s.l, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.95, w: c[i].w - 0.6, h: 0.55, fontFace: FONT, fontSize: 13.5, bold: true, color: WHITE, margin: 0, lineSpacingMultiple: 1.15 });
      slide.addText(s.d, { x: c[i].x + 0.3, y: CONTENT_TOP + 1.55, w: c[i].w - 0.6, h: 0.95, fontFace: FONT, fontSize: 11.5, color: ICE, margin: 0, lineSpacingMultiple: 1.25 });
    });

    callout(slide, MARGIN, 4.75, CONTENT_W, 0.6, "判定阈值：单 Shader ≤ 256 个已编译变体（工程经验值），超过由构建工具告警，非自动阻断", { fontSize: 13, bold: true });

    note(slide, "当前置信度：TGFX 共 36 个 Processor 子类，本方案仅对 3 个代表性 FP 完成逐 bit 分析；其余标注为「待细化」，不影响架构方向", MARGIN, 5.55, CONTENT_W, 0.8, { fontSize: 13 });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 13: 开发者体验 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "开发者工作流", "新增一个渲染效果，只需改两个文件");

    const c = cols(2, 0.4);
    codeBlock(slide, c[0].x, CONTENT_TOP, c[0].w, 4.05, "TextureFillShader.h", [
      { text: "TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY,\n  HAS_RGBAAA, HAS_SUBSET);\n\n", options: { color: TEAL } },
      { text: "static bool ShouldCompile(...) {\n  return !(hasYuv &&\n    (alphaOnly || hasRgbaaa));\n}\n\n", options: { color: TEXT_DARK } },
      { text: "TGFX_REGISTER_SHADER(TextureFillShader)", options: { color: TEAL } },
    ], { fontSize: 14 });

    codeBlock(slide, c[1].x, CONTENT_TOP, c[1].w, 4.05, "texture_fill.frag", [
      { text: "#if HAS_YUV\n", options: { color: TEAL } },
      { text: "  uniform sampler2D u_texY, u_texU;\n", options: { color: TEXT_DARK } },
      { text: "#else\n", options: { color: TEAL } },
      { text: "  uniform sampler2D u_sampler;\n", options: { color: TEXT_DARK } },
      { text: "#endif\n", options: { color: TEAL } },
      { text: "void main() {\n  vec4 color = texture(u_sampler, uv);\n  fragColor = color;\n}", options: { color: TEXT_DARK } },
    ], { fontSize: 14 });

    callout(slide, MARGIN, 6.0, CONTENT_W, 0.55,
      "无需改动 PipelineKey 拼接逻辑 · 验证命令：cmake --build --target tgfx_shader_bundles", { fontSize: 13, bold: true });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 14: 实施路径 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "实施建议", "分阶段推进，新旧路径过渡期并存");

    timeline(slide, [
      { t: "Phase 1 · 声明系统 + 构建工具链", d: "PermutationDomain / PrecompiledShader\nshader_build_tool + CMake 集成" },
      { t: "Phase 2 · 运行时集成 + 逐步迁移", d: "PrecompiledShaderCache 落地\n与 ProgramBuilder 兜底并存" },
      { t: "Phase 3 · 覆盖率达标后瘦身", d: "移除运行时编译器依赖\nRuntimeEffect 单独评估" },
    ], MARGIN, 2.3, CONTENT_W, { lineOffset: 0.9, descH: 1.3 });

    note(slide, "下线运行时编译器的具体时间表是独立的里程碑决策，需后续单独评审，本方案不预设时间表",
      MARGIN, 5.9, CONTENT_W, 0.5, { fontSize: 13.5, align: "center" });

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 15: 风险与兜底 ----------------
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "风险与兜底", "过渡期风险控制机制，与需明确告知的关键风险");

    const risks = [
      { t: "过渡期兜底", d: "未覆盖组合保留动态拼接，不中断渲染" },
      { t: "9 项构建期检查", d: "语法/裁剪/覆盖率任一失败即中止构建" },
      { t: "RuntimeEffect 隔离", d: "用户自定义着色器不受本方案影响" },
    ];
    const c = cols(3, 0.4);
    risks.forEach((r, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 1.75);
      slide.addText(r.t, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.2, w: c[i].w - 0.56, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(r.d, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.68, w: c[i].w - 0.56, h: 0.95, fontFace: FONT, fontSize: 12.5, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.25 });
    });

    panel(slide, MARGIN, 3.85, CONTENT_W, 2.3, { fill: TINT_CORAL, line: CARD_BORDER });
    slide.addShape("rect", { x: MARGIN, y: 3.85, w: 0.07, h: 2.3, fill: { color: CORAL }, line: { type: "none" } });
    slide.addText("关键风险：Bundle 版本不匹配", { x: MARGIN + 0.4, y: 4.05, w: 10, h: 0.35, fontFace: FONT, fontSize: 15.5, bold: true, color: CORAL, margin: 0 });
    slide.addText(
      "瘦身构建下没有运行时编译器可回退：Debug 断言中止，Release 直接拒绝启动对应 backend 的渲染路径。这要求发布流程严格保证 Bundle 与源码同步构建。",
      { x: MARGIN + 0.4, y: 4.5, w: CONTENT_W - 0.7, h: 1.5, fontFace: FONT, fontSize: 14, color: TEXT_DARK, margin: 0, lineSpacingMultiple: 1.3 }
    );

    footer(slide, page, SECTION);
  }

  // ---------------- Slide 16: 收益总结 + Q&A ----------------
  {
    page++;
    const slide = darkSlide(pres);

    slide.addShape("rect", { x: MARGIN, y: 0.65, w: 0.5, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText("收益总结", { x: MARGIN, y: 0.85, w: 6, h: 0.4, fontFace: FONT, fontSize: 13, color: TEAL, bold: true, charSpacing: 3, margin: 0 });
    slide.addText("把不确定的运行时开销，换成确定的构建期投入", {
      x: MARGIN, y: 1.3, w: 11, h: 0.7, fontFace: FONT, fontSize: 26, bold: true, color: WHITE, margin: 0,
    });

    const benefits = [
      { n: "15–70 ms → 0", l: "首帧延迟", d: "查表替代拼接与翻译" },
      { n: "-9~14 MB", l: "运行时二进制体积", d: "瘦身构建下移除三套编译器" },
      { n: "2 个文件", l: "新增 Shader 的改动量", d: ".h 声明维度 + .frag/.vert 逻辑" },
    ];
    const c = cols(3, 0.4);
    benefits.forEach((b, i) => {
      slide.addShape("rect", { x: c[i].x, y: 2.25, w: c[i].w, h: 1.75, fill: { color: NAVY }, line: { type: "none" } });
      slide.addText(b.n, { x: c[i].x + 0.28, y: 2.45, w: c[i].w - 0.56, h: 0.55, fontFace: MONO, fontSize: 20, bold: true, color: TEAL, margin: 0 });
      slide.addText(b.l, { x: c[i].x + 0.28, y: 3.0, w: c[i].w - 0.56, h: 0.3, fontFace: FONT, fontSize: 13.5, bold: true, color: WHITE, margin: 0 });
      slide.addText(b.d, { x: c[i].x + 0.28, y: 3.32, w: c[i].w - 0.56, h: 0.6, fontFace: FONT, fontSize: 11.5, color: ICE, margin: 0, lineSpacingMultiple: 1.2 });
    });

    slide.addShape("line", { x: MARGIN, y: 4.4, w: CONTENT_W, h: 0, line: { color: "3E4F94", width: 1 } });

    slide.addText("Q & A", { x: MARGIN, y: 4.65, w: 4, h: 0.5, fontFace: FONT, fontSize: 20, bold: true, color: TEAL, margin: 0 });
    bullets(slide, [
      "变体规模评估目前仅覆盖 3 个代表性 Processor，其余待补齐同等深度分析",
      "PerlinNoise 阶数收紧属于公开 API 变更，需单独评审确认",
      "运行时编译器下线时间表尚未确定，作为独立里程碑单独决策",
    ], MARGIN, 5.2, CONTENT_W, 1.6, { color: ICE, fontSize: 14, spaceAfter: 8 });

    footer(slide, page, SECTION, { color: "8291D6", lineColor: "3E4F94" });
  }

  const outPath = __dirname + "/TGFX-Shader-Permutation-汇报.pptx";
  await pres.writeFile({ fileName: outPath });
  console.log("Written:", outPath);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
