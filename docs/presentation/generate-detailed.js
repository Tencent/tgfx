// Generates the detailed (full technical review) TGFX Shader Permutation deck (pptx).
// This is the long-form companion to generate.js's summary deck — it expands every
// major topic area into its own slide(s), for use in a formal architecture review meeting.
// Design system: flat, 3-color palette (NAVY / TEAL / CORAL), 2 fonts (PingFang SC + SF Mono),
// unified grid, one core message per slide, no icon badges, no drop shadows.
// Run: node generate-detailed.js
const pptxgen = require("pptxgenjs");

// ---------- design tokens ----------
const NAVY_DARK = "0D1230";
const NAVY = "10173A";
const TEAL = "00B8A9";
const CORAL = "E94F4F";
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
    x: MARGIN, y: 0.82, w: CONTENT_W, h: 0.75, fontFace: FONT, fontSize: 25, color: TEXT_DARK, bold: true, margin: 0, lineSpacingMultiple: 1.08,
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
  const size = opts.fontSize || 15;
  const arr = items.map((t, i) => ({
    text: t,
    options: {
      bullet: { code: "25AA", color: opts.bulletColor || TEAL, indent: 20 },
      breakLine: i < items.length - 1,
      color: opts.color || TEXT_DARK,
      fontSize: size,
      paraSpaceAfter: opts.spaceAfter != null ? opts.spaceAfter : 10,
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
    slide.addText(t, { x: x + 0.6, y: ry, w: w - 0.6, h: rowH, fontFace: FONT, fontSize: opts.fontSize || 14.5, color: TEXT_DARK, valign: "middle", margin: 0, lineSpacingMultiple: 1.22 });
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
      fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, align: "center", valign: "bottom", margin: 0, lineSpacingMultiple: 1.1,
    });
    if (s.d) {
      slide.addText(s.d, {
        x: x + i * stepW + 0.08, y: lineY + 0.28, w: stepW - 0.16, h: opts.descH || 1.15,
        fontFace: FONT, fontSize: 12, color: TEXT_MUTED, align: "center", valign: "top", margin: 0, lineSpacingMultiple: 1.22,
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
    x: x + 0.4, y, w: w - 0.7, h, fontFace: FONT, fontSize: opts.fontSize || 13.5, bold: opts.bold, color: opts.color || TEXT_DARK, valign: "middle", margin: 0, lineSpacingMultiple: 1.25,
  });
}

function note(slide, text, x, y, w, h, opts = {}) {
  slide.addText(text, {
    x, y, w, h, fontFace: FONT, fontSize: opts.fontSize || 13, italic: true, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.25, align: opts.align || "left",
  });
}

function statBox(slide, x, y, w, h, number, label, desc) {
  panel(slide, x, y, w, h, { fill: NAVY, line: NAVY });
  slide.addText(number, { x: x + 0.32, y: y + 0.28, w: w - 0.64, h: 0.7, fontFace: MONO, fontSize: 28, bold: true, color: WHITE, margin: 0 });
  slide.addText(label, { x: x + 0.32, y: y + 1.0, w: w - 0.64, h: 0.35, fontFace: FONT, fontSize: 13.5, bold: true, color: TEAL, margin: 0, lineSpacingMultiple: 1.15 });
  slide.addText(desc, { x: x + 0.32, y: y + 1.4, w: w - 0.64, h: h - 1.6, fontFace: FONT, fontSize: 11.5, color: ICE, margin: 0, lineSpacingMultiple: 1.25 });
}

function codeBlock(slide, x, y, w, h, title, runs, opts = {}) {
  panel(slide, x, y, w, h, { fill: BG_LIGHT });
  slide.addShape("rect", { x, y, w, h: 0.4, fill: { color: NAVY }, line: { type: "none" } });
  slide.addText(title, { x: x + 0.22, y, w: w - 0.44, h: 0.4, fontFace: MONO, fontSize: 12, bold: true, color: WHITE, valign: "middle", margin: 0 });
  slide.addText(runs, {
    x: x + 0.26, y: y + 0.55, w: w - 0.52, h: h - 0.75, fontFace: MONO, fontSize: opts.fontSize || 12.5, margin: 0, lineSpacingMultiple: 1.35, valign: "top",
  });
}

function table(slide, rows, x, y, w, h, colW, opts = {}) {
  slide.addTable(rows, {
    x, y, w, h, fontFace: FONT, fontSize: opts.fontSize || 12.5, color: TEXT_DARK,
    border: { pt: 0.5, color: CARD_BORDER }, fill: { color: WHITE }, colW, valign: "middle", autoPage: false,
  });
}
function headCell(text) {
  return { text, options: { fill: { color: NAVY }, color: WHITE, bold: true, fontSize: 12 } };
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
  pres.title = "TGFX Shader Permutation 系统设计方案 · 技术评审完整版";

  let page = 0;
  const SECTION = "TGFX Shader Permutation · 技术评审";

  // ================= Slide 1: Cover =================
  {
    page++;
    const slide = darkSlide(pres);
    slide.addShape("rect", { x: MARGIN, y: 1.85, w: 0.5, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText("架构技术评审 · 完整版", {
      x: MARGIN, y: 2.05, w: 8, h: 0.4, fontFace: FONT, fontSize: 14, color: TEAL, bold: true, charSpacing: 3, margin: 0,
    });
    slide.addText("Shader Permutation 预编译系统", {
      x: MARGIN, y: 2.55, w: 11, h: 1.2, fontFace: FONT, fontSize: 40, color: WHITE, bold: true, margin: 0,
    });
    slide.addText("消除运行时 Shader 拼接与跨语言翻译，构建期编译、运行时查表", {
      x: MARGIN, y: 3.7, w: 10.5, h: 0.5, fontFace: FONT, fontSize: 16, color: ICE, margin: 0,
    });

    panel(slide, MARGIN, 4.75, CONTENT_W, 1.7, { fill: NAVY, line: NAVY });
    slide.addText("本次评审覆盖范围", { x: MARGIN + 0.32, y: 4.95, w: 4, h: 0.3, fontFace: FONT, fontSize: 12.5, bold: true, color: TEAL, margin: 0 });
    slide.addText(
      "问题定义与目标、总体架构、开发者工作流、源文件规范、Processor 清单与变体规模分析、\n" +
      "声明系统详细设计、构建工具链、运行时集成机制、错误检查体系，以及与既往方案的关键决策差异对比",
      { x: MARGIN + 0.32, y: 5.28, w: CONTENT_W - 0.64, h: 1.1, fontFace: FONT, fontSize: 12.5, color: ICE, margin: 0, lineSpacingMultiple: 1.35 }
    );
    slide.addText("TGFX 图形引擎  ·  GPU 渲染管线", {
      x: MARGIN, y: 6.7, w: 10, h: 0.35, fontFace: FONT, fontSize: 12, color: "8291D6", margin: 0,
    });
  }

  // ================= Slide 2: 评审导读 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "评审导读", "本次评审需要确认的四个关键问题");

    const qs = [
      { t: "Q1 · 架构决策", d: "以「一个 Shader 类声明的全部 Permutation」取代「整棵 Processor 树」作为预编译单元，这个粒度下沉是否成立？" },
      { t: "Q2 · 完备性", d: "EffectDecomposer 递归分解 + 构建期检查 + 过渡期兜底三层机制，能否保证任意 draw 不产生运行时 miss？" },
      { t: "Q3 · 规模可控性", d: "目前仅 3 个代表性 Processor 完成逐 bit 分析；PerlinNoise 阶数上限收紧属于公开 API 变更，是否需要单独评审？" },
      { t: "Q4 · 方案取舍", d: "相较既往「全 multi-pass 分解 + shader 注释声明变体」方案，本方案的关键决策差异是否被认可？" },
    ];
    const gw = (CONTENT_W - 0.4) / 2, gh = 2.1, gap = 0.4;
    qs.forEach((q, i) => {
      const col = i % 2, row = Math.floor(i / 2);
      const x = MARGIN + col * (gw + gap), y = CONTENT_TOP + row * (gh + gap);
      panel(slide, x, y, gw, gh);
      slide.addShape("rect", { x, y, w: gw, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
      slide.addText(q.t, { x: x + 0.3, y: y + 0.28, w: gw - 0.6, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(q.d, { x: x + 0.3, y: y + 0.75, w: gw - 0.6, h: gh - 1.0, fontFace: FONT, fontSize: 12.5, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 });
    });

    footer(slide, page, SECTION);
  }

  // ================= Slide 3: 现状 · 拼接三步 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "问题背景", "现状：Shader 拼接开销卡在渲染关键路径上");

    note(slide,
      "DrawOp::execute() 关键路径上，ProgramBuilder::CreateProgram() 每遇到新的 programKey 都要完成以下三步：",
      MARGIN, CONTENT_TOP, CONTENT_W, 0.55, { fontSize: 14, italic: false });

    timeline(slide, [
      { t: "拼接 GLSL", d: "递归拼接 GP / FP[] / XP\n的 emitCode()，生成源码文本" },
      { t: "编译 SPIR-V", d: "调用 shaderc\n完成编译" },
      { t: "翻译目标语言", d: "SPIRV-Cross / Tint\n翻译为 MSL / WGSL" },
    ], MARGIN, 2.95, CONTENT_W, { lineOffset: 0.7, descH: 1.3 });

    callout(slide, MARGIN, 5.75, CONTENT_W, 0.8,
      "三者均发生在渲染提交路径上，而非构建期 —— 本应一次性的编译工作，被摊到了每一次首次命中的渲染帧里",
      { tone: "coral", fontSize: 14, bold: true, color: CORAL });

    footer(slide, page, SECTION);
  }

  // ================= Slide 4: 代价 · 两个数字 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "问题背景", "代价：首帧延迟与运行时体积（均为估算值）");

    const c = cols(2, 0.5);
    statBox(slide, c[0].x, CONTENT_TOP, c[0].w, 2.85,
      "15–70 ms", "首帧延迟（非实测）",
      "首帧遇到 10–20 个新 programKey 时，按拼接/编译/翻译单次操作耗时（1–5 ms/次量级）累加推算");
    statBox(slide, c[1].x, CONTENT_TOP, c[1].w, 2.85,
      "9–14 MB", "运行时编译器体积",
      "shaderc（含 glslang + SPIRV-Tools）+ SPIRV-Cross + Tint，Release arm64 静态库常驻");

    note(slide,
      "两项均为估算值，非端到端实测，编译器体积未做逐符号核实。本方案不依赖这两个数字支撑架构决策，仅作历史动机参考。",
      MARGIN, 4.95, CONTENT_W, 0.9, { fontSize: 13 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 5: 设计目标 Before/After =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "设计目标", "构建期完成编译，运行时只查表");

    const c = cols(2, 0.5);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.15, { fill: TINT_CORAL, line: CARD_BORDER });
    slide.addText("BEFORE", { x: c[0].x + 0.32, y: CONTENT_TOP + 0.22, w: 4, h: 0.3, fontFace: FONT, fontSize: 12.5, bold: true, color: CORAL, charSpacing: 2, margin: 0 });
    slide.addText("运行时拼接 + 编译 + 翻译", { x: c[0].x + 0.32, y: CONTENT_TOP + 0.55, w: c[0].w - 0.64, h: 0.42, fontFace: FONT, fontSize: 17, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "每个新 programKey 触发一次 emitCode() 拼接",
      "shaderc → SPIRV-Cross/Tint 现场翻译",
      "首帧成本：15–70 ms* 累积卡顿",
      "三套编译器需常驻运行时二进制",
    ], c[0].x + 0.32, CONTENT_TOP + 1.1, c[0].w - 0.64, 2.9, { fontSize: 14 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.15, { fill: TINT_TEAL, line: CARD_BORDER });
    slide.addText("AFTER", { x: c[1].x + 0.32, y: CONTENT_TOP + 0.22, w: 4, h: 0.3, fontFace: FONT, fontSize: 12.5, bold: true, color: TEAL, charSpacing: 2, margin: 0 });
    slide.addText("构建期编译，运行时查表", { x: c[1].x + 0.32, y: CONTENT_TOP + 0.55, w: c[1].w - 0.64, h: 0.42, fontFace: FONT, fontSize: 17, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "shader_build_tool 在 CMake 构建期枚举全部 Permutation",
      "运行时按 PipelineKey 二分查找已编译产物",
      "首帧成本：0（查表，不再拼接/翻译）",
      "瘦身构建下可移除运行时编译器依赖",
    ], c[1].x + 0.32, CONTENT_TOP + 1.1, c[1].w - 0.64, 2.9, { fontSize: 14 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 6: 约束条件 C1-C6 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "约束条件", "六项设计约束：本方案必须满足的边界条件");

    const rows = [
      [headCell("编号"), headCell("约束"), headCell("说明")],
      ["C1", "变种声明位置", "使用 C++ 类型系统声明 Permutation 维度，不使用 shader 文件内的注释元数据"],
      ["C2", "构建工具技术栈", "C++ 独立可执行文件，CMake 驱动；不引入 Python 依赖"],
      ["C3", "架构基线", "以单 Pass 为主（覆盖绝大多数场景），多 Pass 仅作为少数复杂 Processor 组合的兜底手段"],
      ["C4", "热重载", "不支持。修改 shader 源码等价于修改一个 .cpp，走正常的 CMake 增量构建"],
      ["C5", "RuntimeEffect 隔离", "用户自定义着色器（RuntimeEffect.h）保持独立运行时编译路径，不受本方案影响"],
      ["C6", "目标后端", "OpenGL / Vulkan / Metal / WebGPU，与 include/tgfx/gpu/Backend.h 中 Backend enum 一致"],
    ];
    table(slide, rows, MARGIN, CONTENT_TOP, CONTENT_W, 4.15, [0.9, 2.6, 7.83], { fontSize: 13 });

    note(slide, "既往方案在 C1 / C2 / C3 三项上的选择与本方案不同，具体差异对比见后文「决策差异」部分",
      MARGIN, 6.15, CONTENT_W, 0.4, { fontSize: 13 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 7: 构建期流程总览 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "总体架构", "从声明到 Bundle：构建期编译流程");

    timeline(slide, [
      { t: "开发者编写", d: ".h 声明维度\n.frag/.vert 逻辑" },
      { t: "自注册", d: "ShaderRegistry\n静态收集全部 Shader" },
      { t: "枚举组合", d: "PermutationDomain\n笛卡尔积展开" },
      { t: "裁剪", d: "ShouldCompile()\n剔除不可达组合" },
      { t: "编译产出", d: "GLSL→SPIR-V→MSL/WGSL\n打包 Bundle" },
    ], MARGIN, 1.95, CONTENT_W, { lineOffset: 0.7, descH: 1.1 });

    panel(slide, MARGIN, 4.4, CONTENT_W, 1.9, { fill: NAVY, line: NAVY });
    slide.addText("产物", { x: MARGIN + 0.32, y: 4.6, w: 2, h: 0.3, fontFace: FONT, fontSize: 12.5, bold: true, color: TEAL, margin: 0 });
    slide.addText("build/generated/shader_bundle.{opengl|vulkan|metal|webgpu}.bin", {
      x: MARGIN + 0.32, y: 4.92, w: CONTENT_W - 0.64, h: 0.4, fontFace: MONO, fontSize: 14, bold: true, color: WHITE, margin: 0,
    });
    slide.addText("随 App 包体打入；每个已编译 Permutation 对应一条可二分查找的索引项（Bundle 格式详见后文构建工具链部分）。", {
      x: MARGIN + 0.32, y: 5.36, w: CONTENT_W - 0.64, h: 0.4, fontFace: FONT, fontSize: 12, color: ICE, margin: 0,
    });
    slide.addText("驱动方式：CMake custom command，依赖 shader_build_tool 本身 + src/gpu/shaders/**/*.{h,vert,frag}", {
      x: MARGIN + 0.32, y: 5.72, w: CONTENT_W - 0.64, h: 0.4, fontFace: FONT, fontSize: 12, color: ICE, margin: 0,
    });
    footer(slide, page, SECTION);
  }

  // ================= Slide 8: 运行时流程总览 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "总体架构", "从 draw 调用到 GPU 提交：运行时查表流程");

    timeline(slide, [
      { t: "分解 Processor 树", d: "EffectDecomposer\n::Decompose()" },
      { t: "计算 PipelineKey", d: "shaderName + index\n+ 渲染状态" },
      { t: "查表", d: "PrecompiledShaderCache\n::find()" },
      { t: "Program 缓存", d: "复用现有 GlobalCache\nLRU" },
      { t: "提交 GPU", d: "createShaderModule\ncreateRenderPipeline" },
    ], MARGIN, 1.95, CONTENT_W, { lineOffset: 0.7, descH: 1.0 });

    const c = cols(2, 0.4);
    callout(slide, c[0].x, 4.35, c[0].w, 0.95, "命中已注册 Shader Permutation → 单 Pass 直接查表", { fontSize: 13, bold: true, color: TEXT_DARK });
    callout(slide, c[1].x, 4.35, c[1].w, 0.95, "复合效果未命中 → 拆分为多个 Pass，每段仍各自查表，不产生拼接", { tone: "coral", fontSize: 13, bold: true, color: TEXT_DARK });

    note(slide, "不再调用 ProgramBuilder::CreateProgram() / emitCode() 拼接字符串", MARGIN, 5.55, CONTENT_W, 0.4, { fontSize: 13.5, align: "center" });

    footer(slide, page, SECTION);
  }

  // ================= Slide 9: 核心架构决策 · 声明 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "核心架构决策", "预编译单元：Shader 类的 Permutation，而非整棵 Processor 树");

    panel(slide, MARGIN, 2.6, CONTENT_W, 1.5, { fill: NAVY, line: NAVY });
    slide.addText([
      { text: "决策：", options: { bold: true, color: TEAL } },
      { text: "预编译单元 = 「一个 C++ Shader 描述类声明的全部 Permutation」，", options: { color: WHITE } },
      { text: "不是", options: { bold: true, color: CORAL } },
      { text: "「某一次 draw 对应的完整 Processor 树」", options: { color: WHITE } },
    ], { x: MARGIN + 0.4, y: 2.6, w: CONTENT_W - 0.8, h: 1.5, fontFace: FONT, fontSize: 17, valign: "middle", margin: 0, lineSpacingMultiple: 1.3 });

    note(slide, "命中时走单 Pass；命不中的复合效果（colorFilter + maskFilter + 高级 BlendMode 等）才拆多 Pass，每段仍是查表得到的预编译 Shader，不产生新的拼接。具体论证见下一页",
      MARGIN, 4.5, CONTENT_W, 0.9, { fontSize: 14, align: "center" });

    footer(slide, page, SECTION);
  }

  // ================= Slide 10: 决策论证 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "核心架构决策", "决策论证：为何粒度下沉到 Shader 类");

    numberedList(slide, [
      "现有 programKey 是 GP.key + FP[].key（递归）+ XP.key 的拼接；FP 树的嵌套深度由用户通过 Shader::MakeBlend() 等 API 在运行时决定",
      "以「整棵 Processor 树」为预编译单元，其组合数量在编译期不可确定，构建期无法完整枚举",
      "将粒度下沉到「单个 Shader 类的 Permutation」后，一次 draw 是否可预编译，等价于它能否被分解为若干已注册 Shader 类的具体取值——这是封闭、可枚举的问题",
    ], MARGIN, CONTENT_TOP + 0.2, CONTENT_W, 3.3, { fontSize: 15, rowH: 1.1 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 11: 可行性论证 · 三个核心事实 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "可行性论证", "任意 draw 均可被覆盖的可行性论证");

    callout(slide, MARGIN, CONTENT_TOP, CONTENT_W, 0.65,
      "结论：任意合法 draw 产生的 Processor 树，都能被递归分解为有限数量的预编译 Shader 组合", { fontSize: 13.5, bold: true, color: TEXT_DARK });

    const facts = [
      { t: "事实一：其余位置类型有限", d: "除 colors[0]（brush.shader 产出的 FP）外，Processor 树其余位置的可能类型均由有限个内部类工厂方法决定" },
      { t: "事实二：colors[0] 是唯一无界位置", d: "Shader::MakeBlend() 通过 XfermodeFragmentProcessor 递归嵌套子 Shader，理论嵌套深度无界" },
      { t: "事实三：递归嵌套由多 Pass 兜住", d: "EffectDecomposer 对容器节点做递归拆解：每拆一层，子 Shader 先各自渲染到临时纹理（仍是单 Pass 查表），再用极小维度的合并 Shader 完成拼接" },
    ];
    let fy = 2.75;
    facts.forEach((f, i) => {
      panel(slide, MARGIN, fy, CONTENT_W, 1.15);
      slide.addShape("ellipse", { x: MARGIN + 0.28, y: fy + 0.3, w: 0.5, h: 0.5, fill: { color: TEAL }, line: { type: "none" } });
      slide.addText(String(i + 1), { x: MARGIN + 0.28, y: fy + 0.3, w: 0.5, h: 0.5, fontFace: FONT, fontSize: 16, bold: true, color: WHITE, align: "center", valign: "middle", margin: 0 });
      slide.addText(f.t, { x: MARGIN + 1.0, y: fy + 0.15, w: CONTENT_W - 1.4, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(f.d, { x: MARGIN + 1.0, y: fy + 0.52, w: CONTENT_W - 1.4, h: 0.55, fontFace: FONT, fontSize: 12, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.2 });
      fy += 1.3;
    });

    note(slide, "完备性由三层机制共同保证——构建期检查全量匹配 + Decompose() 递归天然终止 + 过渡期兜底，而非依赖对 FP 树形状的枚举（详见后文完备性保证章节）",
      MARGIN, 6.4, CONTENT_W, 0.4, { fontSize: 11.5, align: "center" });
    footer(slide, page, SECTION);
  }

  // ================= Slide 12: 贯穿示例 · 代码 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "贯穿示例", "教学简化版：4 维度 TextureFillShader（生产版为 8 维度，见后文）");

    const c = cols(2, 0.4);
    codeBlock(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5, "TextureFillShader.h", [
      { text: "TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY,\n    HAS_RGBAAA, HAS_SUBSET);\nusing D = Dims;\n\n", options: { color: TEAL } },
      { text: "PrecompiledShaderInfo info() const override {\n  return {\"TextureFillShader\", vs, fs,\n          D::domain(), ShouldCompile};\n}\n\n", options: { color: TEXT_DARK } },
      { text: "static bool ShouldCompile(const Values& v);\n// 排除 YUV 与 alpha-only / RGBAAA 同现的组合\n", options: { color: TEXT_DARK } },
      { text: "TGFX_REGISTER_SHADER(tgfx::TextureFillShader)", options: { color: TEAL } },
    ], { fontSize: 12.5 });

    codeBlock(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5, "texture_fill.frag", [
      { text: "#if HAS_YUV\n", options: { color: TEAL } },
      { text: "  uniform sampler2D u_texY, u_texU, u_texV;\n", options: { color: TEXT_DARK } },
      { text: "#else\n", options: { color: TEAL } },
      { text: "  uniform sampler2D u_sampler;\n", options: { color: TEXT_DARK } },
      { text: "#endif\n", options: { color: TEAL } },
      { text: "void main() {\n  vec4 color = SampleColor();\n", options: { color: TEXT_DARK } },
      { text: "  #if ALPHA_ONLY\n", options: { color: TEAL } },
      { text: "    color = vec4(0,0,0,color.a);\n", options: { color: TEXT_DARK } },
      { text: "  #elif HAS_RGBAAA\n", options: { color: TEAL } },
      { text: "    color.a = texture(u_alphaSampler, uv).r;\n", options: { color: TEXT_DARK } },
      { text: "  #endif\n}", options: { color: TEAL } },
    ], { fontSize: 11.5 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 13: 贯穿示例 · 构建期处理 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "贯穿示例", "构建期对声明的处理流程");

    numberedList(slide, [
      "从 ShaderRegistry::All() 取得工厂函数，构造描述类实例",
      "枚举 D::domain() 笛卡尔积：4 个 bool 维度 → 2⁴ = 16 个裸组合",
      "对每个组合调用 ShouldCompile：排除 6 个 YUV+(alpha) 组合",
      "剩余 10 个有效组合各自宏展开 → 走后端转换链路",
      "10 个变体 × 已启用 backend 数，写入对应 shader_bundle.{backend}.bin",
    ], MARGIN, CONTENT_TOP + 0.1, CONTENT_W, 4.0, { fontSize: 15.5, rowH: 0.76 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 14: 贯穿示例 · 运行时消费 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "贯穿示例", "运行时如何消费构建产物");

    codeBlock(slide, MARGIN, CONTENT_TOP, CONTENT_W, 4.3, "TextureEffect.cpp（运行时）", [
      { text: "// onComputePermutationValues()\n", options: { color: TEXT_MUTED } },
      { text: "using D = TextureFillShader::Dims;\n", options: { color: TEXT_DARK } },
      { text: "values[D::HAS_YUV] = yuvTexture ? 1 : 0;\nvalues[D::ALPHA_ONLY] = isAlphaOnly() ? 1 : 0;\nvalues[D::HAS_RGBAAA] = ...;\n", options: { color: TEXT_DARK } },
      { text: "// DrawOp::execute()\n", options: { color: TEXT_MUTED } },
      { text: "auto result = EffectDecomposer::Decompose(gp, fps, xp, mode);\nauto* program = globalCache()->findProgram(key.toKey());\nif (!program) program = CreateProgramFromBlob(\n    context, precompiledShaderCache()->find(key));", options: { color: TEXT_DARK } },
    ], { fontSize: 14 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 15: 开发者工作流 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "开发者工作流", "新增或修改一个 Shader，只需两项改动");

    const c = cols(2, 0.4);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 3.1);
    slide.addText("① 声明 Permutation 维度（.h）", { x: c[0].x + 0.3, y: CONTENT_TOP + 0.25, w: c[0].w - 0.6, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "定义/修改 PrecompiledShader 子类",
      "列出 Permutation 维度与 ShouldCompile 裁剪规则",
      "文件末尾调用 TGFX_REGISTER_SHADER(...) 自注册",
    ], c[0].x + 0.3, CONTENT_TOP + 0.85, c[0].w - 0.6, 2.1, { fontSize: 13.5, spaceAfter: 10 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 3.1);
    slide.addText("② 编写渲染逻辑（.frag/.vert）", { x: c[1].x + 0.3, y: CONTENT_TOP + 0.25, w: c[1].w - 0.6, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "使用纯 GLSL 编写渲染逻辑",
      "通过 #if DIMNAME 消费第①步声明的宏",
      "命名与格式约定见下一页「源文件规范」",
    ], c[1].x + 0.3, CONTENT_TOP + 0.85, c[1].w - 0.6, 2.1, { fontSize: 13.5, spaceAfter: 10 });

    callout(slide, MARGIN, 5.2, CONTENT_W, 0.65,
      "不需要改动：PipelineKey 拼接逻辑（运行时统一处理）、EffectDecomposer 匹配表（除非引入新的多 Pass 拆分点）", { fontSize: 13, bold: true });

    note(slide, "本地验证：① cmake --build --target tgfx_shader_bundles（构建期静态检查，报错含具体 Permutation 取值定位）  ② 运行截图测试验证渲染正确性",
      MARGIN, 6.05, CONTENT_W, 0.5, { fontSize: 12.5 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 16: Shader 源文件规范 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "Shader 源文件规范", ".vert/.frag 应如何组织、如何消费维度宏");

    const c = cols(2, 0.35);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("文件组织与命名约定", { x: c[0].x + 0.3, y: CONTENT_TOP + 0.22, w: c[0].w - 0.6, h: 0.35, fontFace: FONT, fontSize: 14.5, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "只包含渲染逻辑，不含变种元数据注释——维度已在 C++ 侧声明",
      "枚举维度用 #if DIMNAME == N，N 为 PermutationEnum::valueNames 下标",
      "bool 维度用 #if DIMNAME",
      "文件顶部注明依赖的宏与声明位置（纯文档作用，工具不解析）",
      "跨 Shader 复用代码放入 src/gpu/shaders/common/*.glsl，构建工具做 #include 级联",
    ], c[0].x + 0.3, CONTENT_TOP + 0.7, c[0].w - 0.6, 3.7, { fontSize: 12.5, spaceAfter: 11 });

    codeBlock(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5, "tiled_texture_fill.frag（枚举维度示例）", [
      { text: "uniform sampler2D u_sampler;\n#if HAS_SUBSET\n  uniform SubsetBlock { vec4 u_subset; };\n#endif\n", options: { color: TEXT_DARK } },
      { text: "void main() {\n  vec2 coord = v_texCoord;\n", options: { color: TEXT_DARK } },
      { text: "  #if SHADER_MODE_X == 1  // Clamp\n", options: { color: TEAL } },
      { text: "    coord.x = ClampSubsetX(coord.x, u_subset);\n", options: { color: TEXT_DARK } },
      { text: "  #endif\n  // ... 其余 SHADER_MODE_X/Y 分支\n", options: { color: TEXT_MUTED } },
      { text: "  fragColor = texture(u_sampler, coord);\n}", options: { color: TEXT_DARK } },
    ], { fontSize: 11.5 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 17: Processor 全景清单 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "Shader 清单", "现有 Processor 全景清单（12 GP + 22 FP + 2 XP）");

    const gpList = ["DefaultGeometryProcessor", "QuadPerEdgeAAGeometryProcessor", "NonAARRectGeometryProcessor", "ComplexNonAARRectGeometryProcessor", "RoundStrokeRectGeometryProcessor", "EllipseGeometryProcessor", "ComplexEllipseGeometryProcessor", "HairlineLineGeometryProcessor", "HairlineQuadGeometryProcessor", "MeshGeometryProcessor", "ShapeInstancedGeometryProcessor", "AtlasTextGeometryProcessor"];
    const fpList = ["AARectEffect", "AlphaThresholdFragmentProcessor", "ClampedGradientEffect", "ColorMatrixFragmentProcessor", "ColorSpaceXformEffect", "ComposeFragmentProcessor", "ConicGradientLayout", "ConstColorProcessor", "DeviceSpaceTextureEffect", "DiamondGradientLayout", "DualIntervalGradientColorizer", "GaussianBlur1DFragmentProcessor", "LinearGradientLayout", "LumaFragmentProcessor", "PerlinNoiseFragmentProcessor", "RadialGradientLayout", "SingleIntervalGradientColorizer", "TextureEffect", "TextureGradientColorizer", "TiledTextureEffect", "UnrolledBinaryGradientColorizer", "XfermodeFragmentProcessor"];
    const xpList = ["EmptyXferProcessor", "PorterDuffXferProcessor"];

    const c = cols(3, 0.3);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.95);
    slide.addShape("rect", { x: c[0].x, y: CONTENT_TOP, w: c[0].w, h: 0.4, fill: { color: NAVY }, line: { type: "none" } });
    slide.addText("GeometryProcessor（12）", { x: c[0].x + 0.15, y: CONTENT_TOP, w: c[0].w - 0.3, h: 0.4, fontFace: FONT, fontSize: 11.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
    slide.addText(gpList.join("\n"), { x: c[0].x + 0.2, y: CONTENT_TOP + 0.5, w: c[0].w - 0.4, h: 4.35, fontFace: MONO, fontSize: 9.5, color: TEXT_DARK, margin: 0, lineSpacingMultiple: 1.35 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.95);
    slide.addShape("rect", { x: c[1].x, y: CONTENT_TOP, w: c[1].w, h: 0.4, fill: { color: NAVY }, line: { type: "none" } });
    slide.addText("FragmentProcessor（22）", { x: c[1].x + 0.15, y: CONTENT_TOP, w: c[1].w - 0.3, h: 0.4, fontFace: FONT, fontSize: 11.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
    slide.addText(fpList.join("\n"), { x: c[1].x + 0.2, y: CONTENT_TOP + 0.5, w: c[1].w - 0.4, h: 4.35, fontFace: MONO, fontSize: 8.5, color: TEXT_DARK, margin: 0, lineSpacingMultiple: 1.35 });

    panel(slide, c[2].x, CONTENT_TOP, c[2].w, 1.5);
    slide.addShape("rect", { x: c[2].x, y: CONTENT_TOP, w: c[2].w, h: 0.4, fill: { color: NAVY }, line: { type: "none" } });
    slide.addText("XferProcessor（2）", { x: c[2].x + 0.15, y: CONTENT_TOP, w: c[2].w - 0.3, h: 0.4, fontFace: FONT, fontSize: 11.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
    slide.addText(xpList.join("\n"), { x: c[2].x + 0.2, y: CONTENT_TOP + 0.5, w: c[2].w - 0.4, h: 0.9, fontFace: MONO, fontSize: 9.5, color: TEXT_DARK, margin: 0, lineSpacingMultiple: 1.35 });

    panel(slide, c[2].x, CONTENT_TOP + 1.7, c[2].w, 3.25, { fill: BG_LIGHT });
    slide.addText("统计口径", { x: c[2].x + 0.22, y: CONTENT_TOP + 1.9, w: c[2].w - 0.44, h: 0.3, fontFace: FONT, fontSize: 11.5, bold: true, color: TEXT_DARK, margin: 0 });
    slide.addText(
      "src/gpu/processors/ 下 DEFINE_PROCESSOR_CLASS_ID 的实际数量，2026-07-02 统计；后续新增需同步更新本表。\n\n渐变家族与合成家族归类后，独立的“渐变 shader 组合逻辑”数量少于 22，具体归并方式在细化阶段确定。",
      { x: c[2].x + 0.22, y: CONTENT_TOP + 2.2, w: c[2].w - 0.44, h: 2.3, fontFace: FONT, fontSize: 9.8, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 }
    );

    footer(slide, page, SECTION);
  }

  // ================= Slide 18: 变体规模分析 TextureEffect =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "变体规模分析", "TextureEffect：8 个独立 bit 的组合空间");

    const rows = [
      [headCell("bit"), headCell("含义"), headCell("来源")],
      ["0", "HAS_YUV", "yuvTexture != nullptr"],
      ["1", "HAS_RGBAAA", "alphaStart == Point::Zero()（RGBAAA 布局标志）"],
      ["2", "ALPHA_ONLY", "textureProxy->isAlphaOnly()"],
      ["3", "YUV_FORMAT_NOT_I420", "yuvTexture->yuvFormat() != I420（仅 YUV 有效）"],
      ["4", "YUV_RANGE_FULL", "YUV color range 是否为 limited（仅 YUV 有效）"],
      ["5", "HAS_SUBSET", "needSubset()：是否需要子区域裁剪采样"],
      ["6", "STRICT_CONSTRAINT", "constraint == Strict：采样约束模式"],
      ["7", "HAS_PERSPECTIVE", "coordTransform.matrix.hasPerspective()"],
    ];
    table(slide, rows, MARGIN, CONTENT_TOP, CONTENT_W, 3.85, [0.7, 3.0, 7.63], { fontSize: 12.5 });

    callout(slide, MARGIN, 5.9, CONTENT_W, 0.65,
      "裸组合数：2⁸ = 256。排除“YUV 同时具有 alpha-only 或 RGBAAA”的结构性不可达组合后，有效变体数由构建工具在枚举阶段实际统计", { fontSize: 13, bold: true });
    footer(slide, page, SECTION);
  }

  // ================= Slide 19: 变体规模分析 TiledTextureEffect =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "变体规模分析", "TiledTextureEffect：环绕采样填充，648 种裸组合");

    const rows = [
      [headCell("维度"), headCell("取值数"), headCell("来源")],
      ["shaderModeX", "9", "X 轴 ShaderMode（None/Clamp/RepeatNearestNone/...）"],
      ["shaderModeY", "9", "Y 轴 ShaderMode（onComputeProcessorKey 中 <<4 与 X 轴打包）"],
      ["isAlphaOnly", "2", "textureProxy->isAlphaOnly()"],
      ["constraint == Strict", "2", "SrcRectConstraint"],
      ["hasPerspective", "2", "坐标变换是否含透视"],
    ];
    const c = cols(2, 0.4);
    table(slide, rows, c[0].x, CONTENT_TOP, c[0].w, 2.85, [1.8, 0.6, 2.9], { fontSize: 12 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 2.85, { fill: NAVY, line: NAVY });
    slide.addText("9 × 9 × 2 × 2 × 2", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.28, w: c[1].w - 0.56, h: 0.5, fontFace: MONO, fontSize: 20, bold: true, color: WHITE, margin: 0 });
    slide.addText("= 648 裸组合", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.75, w: c[1].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEAL, margin: 0 });
    slide.addText("ShaderMode::None 表示该轴完全由硬件采样器处理——是否需要为其设计更简单的变体，是构建期裁剪规则需要回答的问题", {
      x: c[1].x + 0.28, y: CONTENT_TOP + 1.2, w: c[1].w - 0.56, h: 1.5, fontFace: FONT, fontSize: 11.5, color: ICE, margin: 0, lineSpacingMultiple: 1.3,
    });

    note(slide,
      "与 TextureEffect 是两个职责不重叠的 Shader：TiledTextureEffect::getTextureView() 显式排除 YUV 纹理，因此本 FP 处理环绕采样模式，TextureEffect 处理格式/YUV/RGBAAA——两者不应共用同一个示例混合其维度",
      MARGIN, 5.95, CONTENT_W, 0.7, { fontSize: 12.5 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 20: PerlinNoise 待决议 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "待决维度", "PerlinNoiseFragmentProcessor：规模问题待决议");

    const pnRows = [
      [headCell("维度"), headCell("取值数")],
      ["noiseType", "2（FractalNoise / Turbulence）"],
      ["numOctaves", "当前上限 255（MAX_OCTAVES）"],
      ["stitchTiles", "2"],
    ];
    table(slide, pnRows, MARGIN, CONTENT_TOP, CONTENT_W, 1.6, [3.5, 7.83], { fontSize: 13.5 });

    bullets(slide, [
      "255×2×2=1020 个变体，仅此一个 FP 就与 C3 目标冲突，直接枚举不可行",
      "此前方案提出收紧上限至 8（8 阶以上振幅衰减 <0.4%，视觉不可分辨），本方案采纳该方向作为前提，但这是公开 API 变更，需单独评审确认",
      "已核对 onComputeProcessorKey() 源码：仅 numOctaves×noiseType×stitchTiles 三个独立维度，收紧后正确变体数为 8×2×2=32——此前的估算值均有误，现已修正",
    ], MARGIN, 3.75, CONTENT_W, 2.4, { fontSize: 14, spaceAfter: 10 });

    note(slide, "在收紧决议前，暂列为待决维度，不计入单 Pass 覆盖率统计", MARGIN, 6.25, CONTENT_W, 0.35, { fontSize: 13 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 21: 单/多 Pass 判定规则 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "判定规则", "单 Pass / 多 Pass 的判定原则");

    const jrows = [
      [headCell("场景"), headCell("方式")],
      ["GP + 单个 colorFP + 可选 coverageFP", { text: "单 Pass", options: { color: TEAL, bold: true } }],
      ["+ 类型有限的 colorFilter（ColorMatrix 等）", { text: "单 Pass", options: { color: TEAL, bold: true } }],
      ["+ GaussianBlur（maskFilter 场景）", { text: "多 Pass", options: { color: CORAL, bold: true } }],
      ["ColorFilter+MaskFilter+高级 BlendMode 同现", { text: "多 Pass", options: { color: CORAL, bold: true } }],
      ["Shader::MakeBlend() 递归嵌套子 Shader", { text: "多 Pass（逐层拆解）", options: { color: CORAL, bold: true } }],
    ];
    table(slide, jrows, MARGIN, CONTENT_TOP, CONTENT_W, 3.3, [7.9, 3.4], { fontSize: 14 });

    callout(slide, MARGIN, 5.4, CONTENT_W, 0.9,
      "判定原则：笛卡尔积裁剪后若不显著膨胀（参考阈值 ≤256 变体），优先归入该 Shader 的 Permutation；否则拆分为独立 Shader，多 Pass 串联",
      { fontSize: 13.5, bold: true });
    footer(slide, page, SECTION);
  }

  // ================= Slide 22: 声明系统 三种维度类型 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "声明系统详细设计", "三种维度类型，覆盖已观察到的全部 key 参数形态");

    const types = [
      { t: "PermutationBool", d: "布尔开关维度，映射为单个 #define NAME 0/1", ex: "HAS_YUV / ALPHA_ONLY" },
      { t: "PermutationEnum", d: "枚举维度，valueNames.size() 必须等于对应 C++ enum 值数量；宏被定义为 0-based 下标", ex: "ShaderMode（9 个值）" },
      { t: "PermutationInt", d: "有界整数维度，取值范围 [0, count)", ex: "mip level 数等" },
    ];
    const c = cols(3, 0.35);
    types.forEach((t, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 3.0);
      slide.addText(t.t, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.25, w: c[i].w - 0.56, h: 0.35, fontFace: MONO, fontSize: 13, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(t.d, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.68, w: c[i].w - 0.56, h: 1.15, fontFace: FONT, fontSize: 11.5, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 });
      slide.addShape("rect", { x: c[i].x + 0.28, y: CONTENT_TOP + 2.4, w: c[i].w - 0.56, h: 0.38, fill: { color: BG_LIGHT }, line: { type: "none" } });
      slide.addText(t.ex, { x: c[i].x + 0.28, y: CONTENT_TOP + 2.4, w: c[i].w - 0.56, h: 0.38, fontFace: MONO, fontSize: 10.5, color: NAVY, align: "center", valign: "middle", margin: 0 });
    });

    note(slide, "TGFX 的 Processor / Shader 类数量为 36 个同量级，不构成模板实例化膨胀的压力；运行时用具名下标而非模板参数位置保证类型安全（详见下一页）",
      MARGIN, 5.35, CONTENT_W, 0.6, { fontSize: 13 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 23: 为何不用模板参数包 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "声明系统详细设计", "为何不采用 UE 式模板参数包");

    bullets(slide, [
      "UE 的 TShaderPermutationDomain<Dim1, Dim2, ...> 通过模板参数包在编译期展开维度组合，代价是每种维度组合都会实例化一份新类型",
      "TGFX 的 Shader 类数量与 Processor 同量级（36 个），不构成模板实例化膨胀的压力，无需为此付出模板复杂度",
      "运行时改用 std::vector<std::variant<...>> 描述维度组合，类型安全通过具名下标访问保证，而非依赖模板参数位置",
      "对全 bool 维度场景（如 TextureFillShader），推荐用 TGFX_DEFINE_DIMS 宏一行声明，内部生成的仍是由 PermutationBool 组成的 PermutationDomain，不需要逐个手写维度类型",
    ], MARGIN, CONTENT_TOP + 0.2, CONTENT_W, 4.2, { fontSize: 16, spaceAfter: 14 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 24: PermutationDomain 编解码 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "声明系统详细设计", "PermutationDomain：mixed-radix 编解码公式");

    panel(slide, MARGIN, CONTENT_TOP, CONTENT_W, 1.55, { fill: NAVY, line: NAVY });
    slide.addText("index = Σ v[i] · stride[i]，i ∈ [0, N)", { x: MARGIN + 0.32, y: CONTENT_TOP + 0.2, w: CONTENT_W - 0.64, h: 0.5, fontFace: MONO, fontSize: 17, bold: true, color: TEAL, margin: 0 });
    slide.addText("stride[0] = 1，stride[i] = stride[i-1] · dimensions[i-1].valueCount()", { x: MARGIN + 0.32, y: CONTENT_TOP + 0.72, w: CONTENT_W - 0.64, h: 0.35, fontFace: MONO, fontSize: 12.5, color: WHITE, margin: 0 });
    slide.addText("第 0 个维度（声明顺序最先）权重最小；后续维度按前面全部维度 valueCount() 的累积乘积递增权重", { x: MARGIN + 0.32, y: CONTENT_TOP + 1.08, w: CONTENT_W - 0.64, h: 0.4, fontFace: FONT, fontSize: 11, color: ICE, margin: 0 });

    panel(slide, MARGIN, 3.65, CONTENT_W, 1.35, { fill: TINT_TEAL, line: CARD_BORDER });
    slide.addText("全 bool domain 退化为标准位打包（LSB-first）：", { x: MARGIN + 0.32, y: 3.8, w: CONTENT_W - 0.64, h: 0.32, fontFace: FONT, fontSize: 12.5, bold: true, color: TEAL, margin: 0 });
    slide.addText("index = Σ v[i] · 2ⁱ  ——  第 i 个维度恰好占第 i 个 bit", { x: MARGIN + 0.32, y: 4.15, w: CONTENT_W - 0.64, h: 0.45, fontFace: MONO, fontSize: 15, bold: true, color: TEAL, margin: 0 });
    slide.addText("这是直接使用 1u << D::DIM_NAME 位运算、且结果与 domain.encode({...}) 完全等价的前提", { x: MARGIN + 0.32, y: 4.62, w: CONTENT_W - 0.64, h: 0.3, fontFace: FONT, fontSize: 11, italic: true, color: TEAL, margin: 0 });

    slide.addText("关键约束", { x: MARGIN, y: 5.3, w: 4, h: 0.32, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "encode()/decode() 必须严格遵循该公式，不得采用 MSB-first 或其他打包顺序",
      "含 PermutationEnum/PermutationInt 维度的 Shader 不满足“每维度恰占 1 bit”前提，仍需走通用的 domain.encode({...}) 按位置传值路径",
    ], MARGIN, 5.68, CONTENT_W, 0.95, { fontSize: 12.5, spaceAfter: 6 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 25: 自注册机制 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "声明系统详细设计", "PrecompiledShader 基类与静态自注册");

    const c = cols(2, 0.4);
    codeBlock(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5, "PrecompiledShader.h", [
      { text: "struct PrecompiledShaderInfo {\n  std::string name, vertexFile, fragmentFile;\n  PermutationDomain domain;\n  ShouldCompileFn shouldCompile;\n};\n\n", options: { color: TEXT_DARK } },
      { text: "class PrecompiledShader {\n public:\n  virtual PrecompiledShaderInfo\n      info() const = 0;\n};\n\n", options: { color: TEXT_DARK } },
      { text: "// ShaderRegistry::Register() / All() 见右侧宏说明", options: { color: TEXT_MUTED } },
    ], { fontSize: 12 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5);
    slide.addText("两个宏，消除手写同步负担", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.22, w: c[1].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    slide.addText([
      { text: "TGFX_REGISTER_SHADER(ClassName)\n", options: { fontFace: MONO, bold: true, color: NAVY, fontSize: 12.5 } },
      { text: "静态初始化期把工厂函数注册进 ShaderRegistry；用 __COUNTER__ 生成唯一注册器名，不对 ClassName 做 token 粘贴（因其可能含 :: ，粘贴是未定义行为）\n\n", options: { color: TEXT_MUTED, fontSize: 11.5 } },
      { text: "TGFX_DEFINE_DIMS(A, B, C, ...)\n", options: { fontFace: MONO, bold: true, color: NAVY, fontSize: 12.5 } },
      { text: "把“维度声明”与“维度下标”合并为一次声明——生成 enum { A=0, B=1, C=2, COUNT } 与对应的 PermutationDomain::domain()，两者永不漂移", options: { color: TEXT_MUTED, fontSize: 11.5 } },
    ], { x: c[1].x + 0.28, y: CONTENT_TOP + 0.65, w: c[1].w - 0.56, h: 2.5, fontFace: FONT, margin: 0, lineSpacingMultiple: 1.32 });

    callout(slide, c[1].x, CONTENT_TOP + 3.25, c[1].w, 1.15,
      "COUNT 是编译期常量，用于 static_assert(D::COUNT == N)：增删维度后若忘记同步更新裁剪/运行时代码，编译直接失败，而不是留下静默的下标错位",
      { fontSize: 11.5, color: TEAL, bold: true });
    footer(slide, page, SECTION);
  }

  // ================= Slide 26: 生产环境完整声明 8 维度 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "生产环境完整声明", "TextureFillShader：8 维度声明与运行时位拼装");

    const c = cols(2, 0.4);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("8 个 Permutation 维度（均为 bool）", { x: c[0].x + 0.28, y: CONTENT_TOP + 0.22, w: c[0].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    slide.addText("HAS_YUV · YUV_FORMAT_NOT_I420\nYUV_RANGE_FULL · ALPHA_ONLY\nHAS_RGBAAA · HAS_SUBSET\nSTRICT_CONSTRAINT · HAS_PERSPECTIVE", {
      x: c[0].x + 0.28, y: CONTENT_TOP + 0.65, w: c[0].w - 0.56, h: 1.0, fontFace: MONO, fontSize: 12, bold: true, color: NAVY, margin: 0, lineSpacingMultiple: 1.2,
    });
    bullets(slide, [
      "均为独立 bool，互不嵌套，可一次 TGFX_DEFINE_DIMS 声明",
      "YUV_FORMAT_NOT_I420 / YUV_RANGE_FULL 仅在 HAS_YUV 时被消费，仍声明为平级独立 bool，避免维度间隐式耦合",
      "ShouldCompile 排除“YUV 同时具有 alpha-only 或 RGBAAA”的结构性不可达组合",
      "static_assert(D::COUNT == 8) 防止后续增删维度时裁剪逻辑与声明脱钩",
    ], c[0].x + 0.28, CONTENT_TOP + 1.75, c[0].w - 0.56, 2.6, { fontSize: 12, spaceAfter: 9 });

    codeBlock(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5, "运行时位拼装（等价于 encode）", [
      { text: "using D = TextureFillShader::Dims;\nstatic_assert(D::COUNT == 8, \"...\");\n\n", options: { color: TEXT_MUTED } },
      { text: "uint32_t idx = 0;\nif (hasYuv)    idx |= 1u << D::HAS_YUV;\nif (alphaOnly) idx |= 1u << D::ALPHA_ONLY;\nif (hasRgbaaa) idx |= 1u << D::HAS_RGBAAA;\n// ... 其余维度同理\n\n", options: { color: TEXT_DARK } },
      { text: "PipelineKey key{\"TextureFillShader\", idx,\n    rtFormat, sampleCount, blendState};", options: { color: TEXT_DARK } },
    ], { fontSize: 12.5 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 27: 构建工具链整体流程 + 后端转换 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "构建工具链", "shader_build_tool 整体流程与后端转换链路");

    const c = cols(2, 0.4);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("main() 逻辑", { x: c[0].x + 0.28, y: CONTENT_TOP + 0.22, w: c[0].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    numberedList(slide, [
      "遍历 ShaderRegistry::All() 取得每个已注册 Shader",
      "枚举 domain.totalCount() 个裸组合，逐一调用 shouldCompile 裁剪",
      "有效组合：defineListFor() 生成宏 → 走后端转换链路",
      "WriteBundles() 打包产物，WriteReportJson() 写构建报告",
      "report.hasErrors() 为真时非零退出，令 CMake build 直接失败",
    ], c[0].x + 0.28, CONTENT_TOP + 0.7, c[0].w - 0.56, 3.6, { fontSize: 11.8, rowH: 0.72 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5);
    slide.addText("后端转换链路", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.22, w: c[1].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    const rows = [
      [headCell("后端"), headCell("链路")],
      ["OpenGL", "宏展开后的 GLSL 直接作为产物（运行时仍需 glCompileShader）"],
      ["Vulkan", "GLSL → glslang → SPIR-V"],
      ["Metal", "GLSL → glslang → SPIR-V → spirv-cross → MSL"],
      ["WebGPU", "GLSL → glslang → SPIR-V → tint → WGSL"],
    ];
    table(slide, rows, c[1].x + 0.28, CONTENT_TOP + 0.65, c[1].w - 0.56, 1.9, [1.0, 3.9], { fontSize: 10.5 });
    slide.addText("OpenGL 需按 ShaderCaps profile（framebuffer fetch / ES/Desktop 差异）拆分为多个 Bundle；文件名含 profileTag，拼接规则运行时与构建期共享同一实现", {
      x: c[1].x + 0.28, y: CONTENT_TOP + 2.7, w: c[1].w - 0.56, h: 1.6, fontFace: FONT, fontSize: 11, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3,
    });
    footer(slide, page, SECTION);
  }

  // ================= Slide 28: CMake 依赖追踪 + Bundle 格式 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "构建工具链", "CMake 依赖追踪与 Bundle 二进制格式");

    const c = cols(2, 0.35);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("CMake 依赖追踪", { x: c[0].x + 0.28, y: CONTENT_TOP + 0.22, w: c[0].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "file(GLOB_RECURSE ... CONFIGURE_DEPENDS ...) 收集 shader 源文件，新增/删除文件也能正确触发重跑",
      "add_custom_command DEPENDS 包含 shader_build_tool 本身与全部 shader 源文件",
      "add_custom_target tgfx_shader_bundles 挂到 TGFXFullTest_OpenGL 等目标",
      "改一个 .frag 触发重跑，体验等价于修改一个 .cpp 后的正常增量编译（符合 C4）",
    ], c[0].x + 0.28, CONTENT_TOP + 0.68, c[0].w - 0.56, 3.6, { fontSize: 12, spaceAfter: 12 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5);
    slide.addText("Bundle 结构（每个 backend/profile 各一份）", { x: c[1].x + 0.25, y: CONTENT_TOP + 0.22, w: c[1].w - 0.5, h: 0.35, fontFace: FONT, fontSize: 13, bold: true, color: TEXT_DARK, margin: 0 });
    const parts = ["FileHeader", "IndexEntry[entryCount]", "Reflection Data Pool", "Shader Data Pool"];
    let psy = CONTENT_TOP + 0.65;
    parts.forEach((p) => {
      slide.addShape("rect", { x: c[1].x + 0.25, y: psy, w: c[1].w - 0.5, h: 0.48, fill: { color: NAVY }, line: { type: "none" } });
      slide.addText(p, { x: c[1].x + 0.45, y: psy, w: c[1].w - 0.9, h: 0.48, fontFace: MONO, fontSize: 11.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
      psy += 0.58;
    });
    bullets(slide, [
      "索引表按 (pipelineKeyHashHi, pipelineKeyHashLo) 字典序排列，构建期校验无重复，运行时二分查找",
      "全部 struct 用 #pragma pack(push,1) 保证跨平台无 padding 的可移植序列化",
      "OpenGL 无 SPIR-V 中间产物：只经 glslang buildReflection() 提取布局后保留原始 GLSL 文本",
    ], c[1].x + 0.25, psy + 0.15, c[1].w - 0.5, 1.4, { fontSize: 10.3, spaceAfter: 6 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 29: 运行时集成 GlobalCache + PrecompiledShaderCache =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "运行时集成", "与现有 GlobalCache 的关系，以及 PrecompiledShaderCache");

    const c = cols(2, 0.4);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("与 GlobalCache 的关系", { x: c[0].x + 0.28, y: CONTENT_TOP + 0.22, w: c[0].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "不替换缓存机制，只替换“未命中时如何生成 Program”这一步",
      "现状：未命中调用 ProgramBuilder::CreateProgram() 动态拼接",
      "改造后：未命中改为 precompiledShaderCache()->find(pipelineKey) 查表",
      "PipelineKey::toKey() 序列化为 BytesKey，直接复用现有 GlobalCache 基础设施；编码含固定前缀 tag 字节，与旧路径 key 空间区分",
    ], c[0].x + 0.28, CONTENT_TOP + 0.65, c[0].w - 0.56, 3.7, { fontSize: 12, spaceAfter: 11 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5);
    slide.addText("PrecompiledShaderCache", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.22, w: c[1].w - 0.56, h: 0.35, fontFace: MONO, fontSize: 13, bold: true, color: TEXT_DARK, margin: 0 });
    slide.addText("每个 Context 持有一个实例：loadBundle(path) / find(key) -> Blob*", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.6, w: c[1].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 10.8, color: TEXT_MUTED, margin: 0 });
    bullets(slide, [
      "实例方法而非静态：按 Context 粒度隔离，同进程内多个 backend 的 Context 各自持有并加载自己的 Bundle",
      "loadBundle() 由 GPU::init() 内部自动调用一次，无需用户手动触发",
      "loadBundle() 必须先于任何 find() 完成，完成后视为只读——find() 天然线程安全，不需要额外加锁",
      "find() 返回构建期产物（二进制/文本+Reflection），是否创建 Program 仍遵循既有分层",
    ], c[1].x + 0.28, CONTENT_TOP + 1.05, c[1].w - 0.56, 3.3, { fontSize: 12, spaceAfter: 10 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 30: Uniform 填充 + EffectDecomposer =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "运行时集成", "Uniform 填充机制、EffectDecomposer 递归分解");

    const c = cols(2, 0.4);
    panel(slide, c[0].x, CONTENT_TOP, c[0].w, 4.5);
    slide.addText("Uniform 填充机制", { x: c[0].x + 0.28, y: CONTENT_TOP + 0.22, w: c[0].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    note(slide, "Permutation 改造消除“生成 shader 代码”，不改变“填充 uniform 数值”", c[0].x + 0.28, CONTENT_TOP + 0.6, c[0].w - 0.56, 0.5, { fontSize: 11.5 });
    bullets(slide, [
      "Processor::setData()/onSetData() 写入数值路径不变",
      "需改造：uniform 名字 → buffer 偏移/binding 的映射，改为从 Bundle 的 ReflectionEntry/UniformDesc 静态读取",
      "UniformDesc.name 与 Processor 侧命名一致性无法构建期静态检查，只能靠命名约定 + 运行时防御检查暴露",
    ], c[0].x + 0.28, CONTENT_TOP + 1.15, c[0].w - 0.56, 3.1, { fontSize: 12, spaceAfter: 11 });

    panel(slide, c[1].x, CONTENT_TOP, c[1].w, 4.5);
    slide.addText("EffectDecomposer 递归分解", { x: c[1].x + 0.28, y: CONTENT_TOP + 0.22, w: c[1].w - 0.56, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "Decompose() 三步：识别匹配的已注册 Shader 类 → 调用各 Processor 的 onComputePermutationValues() → encode() 组装 PipelineKey",
      "容器节点递归下探：子结果均单 Pass 且父为 Compose 时并入同一 Shader；父为 Xfermode 或子结果已多 Pass 时新增 Pass 边界",
      "递归终止条件是叶子 FP——类型集合有限，任意深度的树都在有限步内终止",
      "RuntimeEffect 已用独立 CommandEncoder 路径，不受本方案影响，但仍需 shaderc 等作为运行时依赖，与瘦身构建互斥",
    ], c[1].x + 0.28, CONTENT_TOP + 0.65, c[1].w - 0.56, 3.7, { fontSize: 11.5, spaceAfter: 10 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 31: 完备性保证 · 判定表 =================
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

  // ================= Slide 32: 完备性保证 · 三层机制 =================
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
      slide.addText(m.t, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.35, w: c[i].w - 0.6, h: 0.5, fontFace: FONT, fontSize: 15.5, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(m.d, { x: c[i].x + 0.3, y: CONTENT_TOP + 0.95, w: c[i].w - 0.6, h: 2.4, fontFace: FONT, fontSize: 13, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 });
    });
    footer(slide, page, SECTION);
  }

  // ================= Slide 33: 变体规模评估与置信度声明 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "变体规模评估", "Permutation 变体规模评估结果");

    const stats = [
      { n: "256", l: "TextureEffect 裸组合数", d: "8 个独立 bool 维度，2⁸=256；裁剪结构性不可达组合后更少（待构建工具统计）" },
      { n: "648", l: "TiledTextureEffect 裸组合数", d: "X/Y 两轴各 9 种 ShaderMode + isAlphaOnly + constraint + hasPerspective" },
      { n: "32", l: "PerlinNoise（待决议）", d: "已核对源码：3 个独立维度（8×2×2=32）；上限收紧属公开 API 变更，需单独评审" },
    ];
    const c = cols(3, 0.4);
    stats.forEach((s, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 2.55, { fill: NAVY, line: NAVY });
      slide.addText(s.n, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.22, w: c[i].w - 0.56, h: 0.6, fontFace: MONO, fontSize: 26, bold: true, color: TEAL, margin: 0 });
      slide.addText(s.l, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.85, w: c[i].w - 0.56, h: 0.5, fontFace: FONT, fontSize: 12.5, bold: true, color: WHITE, margin: 0, lineSpacingMultiple: 1.15 });
      slide.addText(s.d, { x: c[i].x + 0.28, y: CONTENT_TOP + 1.4, w: c[i].w - 0.56, h: 1.05, fontFace: FONT, fontSize: 11, color: ICE, margin: 0, lineSpacingMultiple: 1.25 });
    });

    callout(slide, MARGIN, 4.65, CONTENT_W, 0.6, "判定阈值：单 Shader ≤ 256 个已编译变体（工程经验值），超过由构建工具告警，非自动阻断", { fontSize: 13, bold: true });
    note(slide, "当前置信度：TGFX 共 36 个 Processor 子类（12 GP + 22 FP + 2 XP），本方案仅对 3 个代表性 FP 完成逐 bit 分析；其余标注为「待细化」，不影响架构方向",
      MARGIN, 5.5, CONTENT_W, 0.9, { fontSize: 12.5 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 34: 错误检查体系 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "错误检查体系", "编译期 / 构建期 / 运行时三层，多数错误在构建期暴露");

    const layers = [
      { t: "编译期（C++ 编译阶段）", items: ["Shader 类未实现 info()：纯虚函数，直接编译报错", "TGFX_DEFINE_DIMS 维度数量变化后，配套 static_assert(D::COUNT==N) 强制开发者检视相关代码"] },
      { t: "构建期（shader_build_tool，9 项检查）", items: ["目录扫描 vs 注册表比对、维度声明自检、ShouldCompile 下标安全性", "宏展开后 GLSL 语法校验（报错含维度取值定位）、全零覆盖检查、变体数膨胀预警（>256）", "构建报告 JSON、Uniform 名长度校验（≤31 字节）、Processor 覆盖率检查"] },
      { t: "运行时（防御性检查）", items: ["PipelineKey 查不到：Debug 断言中止，Release 跳过绘制并记录日志", "Bundle 版本（sourceHash）不匹配：无编译器可回退，Debug 断言中止，Release 拒绝启动该 backend"] },
    ];
    let ly = CONTENT_TOP;
    layers.forEach((l, i) => {
      const h = i === 1 ? 2.15 : 1.25;
      panel(slide, MARGIN, ly, CONTENT_W, h);
      slide.addShape("rect", { x: MARGIN, y: ly, w: 0.06, h, fill: { color: TEAL }, line: { type: "none" } });
      slide.addText(l.t, { x: MARGIN + 0.32, y: ly + 0.12, w: CONTENT_W - 0.6, h: 0.32, fontFace: FONT, fontSize: 13.5, bold: true, color: TEXT_DARK, margin: 0 });
      bullets(slide, l.items, MARGIN + 0.32, ly + 0.48, CONTENT_W - 0.6, h - 0.55, { fontSize: 11.3, spaceAfter: 4 });
      ly += h + 0.15;
    });
    footer(slide, page, SECTION);
  }

  // ================= Slide 35: 与既有方案的决策差异 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "决策差异", "与既往方案的关键选型对比");

    const rows = [
      [headCell("维度"), headCell("既往方案"), headCell("本方案")],
      ["变种声明位置", "shader 文件 //! 注释", "C++ 类声明"],
      ["解析方式", "Python 正则解析 GLSL 注释", "C++ 静态自注册，构建工具直接调用 C++ API"],
      ["架构基线", "全部改为多 Pass，废弃单 Pass 拼接模型", "单 Pass 为主，多 Pass 仅作复杂组合兜底"],
      ["原子 Shader 数", "固定集合（估算 ~359）", "按已注册 Shader 类的维度动态确定，不预设总数"],
    ];
    table(slide, rows, MARGIN, CONTENT_TOP, CONTENT_W, 2.55, [2.0, 4.6, 4.73], { fontSize: 12.5 });

    slide.addText("选择理由", { x: MARGIN, y: 4.65, w: 4, h: 0.35, fontFace: FONT, fontSize: 15, bold: true, color: TEXT_DARK, margin: 0 });
    bullets(slide, [
      "C++ 类声明可获得编译期类型检查（info() 纯虚函数强制实现），//! 注释的正确性只能在构建期或运行时暴露",
      "全面多 Pass 改造意味着放弃全部单 Pass 场景的现有性能特征；消除运行时拼接、控制变体规模两个目标，可以同时通过“Shader 类粒度的 Permutation”达成，不必绑定“废弃单 Pass”这一决策",
    ], MARGIN, 5.1, CONTENT_W, 1.5, { fontSize: 13, spaceAfter: 10 });

    footer(slide, page, SECTION);
  }

  // ================= Slide 36: 实施路径 =================
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

  // ================= Slide 37: 风险与兜底 · 机制 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "风险与兜底", "过渡期风险控制机制");

    const risks = [
      { t: "过渡期兜底", d: "未覆盖组合保留 ProgramBuilder::CreateProgram() 动态拼接，不中断渲染" },
      { t: "9 项构建期检查", d: "语法校验、裁剪自检、变体膨胀预警、Processor 覆盖率核对等，任一失败即中止 CMake 构建" },
      { t: "RuntimeEffect 隔离", d: "用户自定义着色器走独立运行时编译路径，不受本方案影响（但仍依赖 shaderc 等，与瘦身构建互斥）" },
    ];
    const c = cols(3, 0.4);
    risks.forEach((r, i) => {
      panel(slide, c[i].x, CONTENT_TOP, c[i].w, 3.5);
      slide.addShape("rect", { x: c[i].x, y: CONTENT_TOP, w: c[i].w, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
      slide.addText(r.t, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.32, w: c[i].w - 0.56, h: 0.4, fontFace: FONT, fontSize: 14.5, bold: true, color: TEXT_DARK, margin: 0 });
      slide.addText(r.d, { x: c[i].x + 0.28, y: CONTENT_TOP + 0.85, w: c[i].w - 0.56, h: 2.5, fontFace: FONT, fontSize: 12.5, color: TEXT_MUTED, margin: 0, lineSpacingMultiple: 1.3 });
    });
    footer(slide, page, SECTION);
  }

  // ================= Slide 38: 关键风险：Bundle 版本不匹配 =================
  {
    page++;
    const slide = pres.addSlide();
    header(slide, "风险与兜底", "需要明确告知的关键风险：Bundle 版本不匹配");

    panel(slide, MARGIN, CONTENT_TOP, CONTENT_W, 3.0, { fill: TINT_CORAL, line: CARD_BORDER });
    slide.addShape("rect", { x: MARGIN, y: CONTENT_TOP, w: 0.07, h: 3.0, fill: { color: CORAL }, line: { type: "none" } });
    slide.addText(
      "FileHeader.sourceHash 与当前源码不一致，说明 Bundle 未随源码同步重新构建。此时没有可退到的运行时编译器（瘦身构建下 shaderc/spirv-cross/tint 均不在运行时二进制中）；因此加载时直接拒绝：Debug 断言中止并提示需重新构建，Release 拒绝启动对应 backend 并记录致命日志。",
      { x: MARGIN + 0.4, y: CONTENT_TOP + 0.25, w: CONTENT_W - 0.7, h: 1.5, fontFace: FONT, fontSize: 15, color: CORAL, bold: true, margin: 0, lineSpacingMultiple: 1.35 }
    );
    slide.addText(
      "保留运行时编译器作为回退不是本方案选项，因为这与消除运行时拼接的目标直接冲突。",
      { x: MARGIN + 0.4, y: CONTENT_TOP + 1.85, w: CONTENT_W - 0.7, h: 0.9, fontFace: FONT, fontSize: 13.5, color: CORAL, margin: 0, lineSpacingMultiple: 1.3 }
    );

    note(slide,
      "PipelineKey → 128-bit hash 由构建工具与运行时链接同一份 Hash128 实现，一致性由“单一实现”结构性保证；碰撞抗性受限于两个独立 64-bit hash 拼接（约 2⁻³² 量级），在万级变体规模下可忽略",
      MARGIN, 5.25, CONTENT_W, 0.8, { fontSize: 12.5 });
    footer(slide, page, SECTION);
  }

  // ================= Slide 39: 收益总结 + Q&A =================
  {
    page++;
    const slide = darkSlide(pres);

    slide.addShape("rect", { x: MARGIN, y: 0.65, w: 0.5, h: 0.06, fill: { color: TEAL }, line: { type: "none" } });
    slide.addText("收益总结", { x: MARGIN, y: 0.85, w: 6, h: 0.4, fontFace: FONT, fontSize: 13, color: TEAL, bold: true, charSpacing: 3, margin: 0 });
    slide.addText("把不确定的运行时开销，换成确定的构建期投入", {
      x: MARGIN, y: 1.3, w: 11, h: 0.65, fontFace: FONT, fontSize: 25, bold: true, color: WHITE, margin: 0,
    });

    const benefits = [
      { n: "15–70 ms → 0", l: "首帧延迟", d: "查表替代拼接与翻译" },
      { n: "-9~14 MB", l: "运行时二进制体积", d: "瘦身构建下移除三套编译器" },
      { n: "2 个文件", l: "新增 Shader 的改动量", d: ".h 声明维度 + .frag/.vert 逻辑" },
    ];
    const c = cols(3, 0.4);
    benefits.forEach((b, i) => {
      slide.addShape("rect", { x: c[i].x, y: 2.2, w: c[i].w, h: 1.6, fill: { color: NAVY }, line: { type: "none" } });
      slide.addText(b.n, { x: c[i].x + 0.26, y: 2.4, w: c[i].w - 0.52, h: 0.5, fontFace: MONO, fontSize: 18, bold: true, color: TEAL, margin: 0 });
      slide.addText(b.l, { x: c[i].x + 0.26, y: 2.9, w: c[i].w - 0.52, h: 0.3, fontFace: FONT, fontSize: 12.5, bold: true, color: WHITE, margin: 0 });
      slide.addText(b.d, { x: c[i].x + 0.26, y: 3.2, w: c[i].w - 0.52, h: 0.5, fontFace: FONT, fontSize: 11, color: ICE, margin: 0, lineSpacingMultiple: 1.2 });
    });

    slide.addShape("line", { x: MARGIN, y: 4.15, w: CONTENT_W, h: 0, line: { color: "3E4F94", width: 1 } });

    slide.addText("Q & A · 待确认事项", { x: MARGIN, y: 4.4, w: 6, h: 0.45, fontFace: FONT, fontSize: 18, bold: true, color: TEAL, margin: 0 });
    bullets(slide, [
      "变体规模评估目前仅覆盖 3 个代表性 Processor（TextureEffect/TiledTextureEffect/PerlinNoise），其余 33 个待补齐同等深度分析",
      "PerlinNoise 阶数收紧（255→8）属于公开 API 变更，需单独评审确认，且需完整覆盖 1–8 全部取值",
      "GaussianBlur 作为 coverageFP 时单/多 Pass 下的时序差异，继承既往方案中的待办事项，需专项设计",
      "运行时编译器下线时间表尚未确定，作为独立里程碑单独决策",
    ], MARGIN, 4.9, CONTENT_W, 1.85, { color: ICE, fontSize: 12.5, spaceAfter: 6 });

    footer(slide, page, SECTION, { color: "8291D6", lineColor: "3E4F94" });
  }

  const outPath = __dirname + "/TGFX-Shader-Permutation-技术评审.pptx";
  await pres.writeFile({ fileName: outPath });
  console.log("Written:", outPath);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
