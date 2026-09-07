// TGFX Shader Permutation 方案走读 PPT — 贯穿式案例版
// 以 "TextureEffect 处理一张 alpha-only 纹理" 的一次 draw 作为线索，
// 从旧路径完整走一遍，到新路径逐环节替代，每页追踪同一组数据的形态变化。
// Run: node generate-walkthrough.js
const pptxgen = require("pptxgenjs");

// ── Tokens ──
const NAVY = "10173A", NAVY_DARK = "0D1230", TEAL = "00B8A9", CORAL = "E94F4F";
const WHITE = "FFFFFF", BG = "F7F8FC", DARK = "1A2452", MUTED = "6B7494";
const BORDER = "E3E6F2", ICE = "C7CEEF", TINT_TEAL = "E6F7F5", TINT_CORAL = "FCEBEB";
const PURPLE = "6B5B95", AMBER = "B8860B";
const F = "PingFang SC", M = "SF Mono";

// ── Layout ──
const PW = 13.333, MG = 0.8, CW = PW - MG * 2; // 11.733"
const CT = 1.70, CB = 6.90;
const col2W = (CW - 0.40) / 2;

// ── Helpers ──
function hdr(s, kicker, title, opts = {}) {
  s.background = { color: BG };
  s.addShape("rect", { x: 0, y: 0, w: PW, h: 0.04, fill: { color: NAVY }, line: { type: "none" } });
  const kc = opts.kc || TEAL;
  s.addText(kicker.toUpperCase(), { x: MG, y: 0.40, w: 8, h: 0.24, fontFace: F, fontSize: 10, color: kc, bold: true, charSpacing: 2, margin: 0 });
  // Data state badge (top-right)
  if (opts.badge) {
    s.addShape("rect", { x: PW - MG - 3.2, y: 0.38, w: 3.2, h: 0.26, fill: { color: opts.bb || "FBF3E0" }, line: { color: opts.bl || AMBER, width: 0.75 } });
    s.addText(opts.badge, { x: PW - MG - 3.2, y: 0.38, w: 3.2, h: 0.26, fontFace: M, fontSize: 8, bold: true, color: opts.bl || AMBER, align: "center", valign: "middle", margin: 0 });
  }
  s.addText(title, { x: MG, y: 0.72, w: CW, h: 0.65, fontFace: F, fontSize: 22, color: DARK, bold: true, margin: 0 });
  s.addShape("line", { x: MG, y: 1.45, w: CW, h: 0, line: { color: BORDER, width: 1 } });
}

function ftr(s, n, label) {
  s.addShape("line", { x: MG, y: CB, w: CW, h: 0, line: { color: BORDER, width: 0.75 } });
  const t = "TGFX · Shader Permutation" + (label ? "  \u25b8 " + label : "");
  s.addText(t, { x: MG, y: CB + 0.06, w: 9, h: 0.22, fontFace: F, fontSize: 9, color: MUTED, margin: 0 });
  s.addText(String(n), { x: PW - MG - 0.4, y: CB + 0.06, w: 0.4, h: 0.22, fontFace: F, fontSize: 9, color: MUTED, align: "right", margin: 0 });
}

function codeBox(s, x, y, w, h, title, text, opts = {}) {
  s.addShape("rect", { x, y, w, h, fill: { color: "F4F5FA" }, line: { color: BORDER, width: 0.75 } });
  const bc = opts.proposed ? "3A3360" : NAVY;
  s.addShape("rect", { x, y, w, h: 0.32, fill: { color: bc }, line: { type: "none" } });
  s.addText(title, { x: x + 0.14, y, w: w - 0.28, h: 0.32, fontFace: M, fontSize: 8.5, bold: true, color: WHITE, valign: "middle", margin: 0 });
  s.addText(text, { x: x + 0.18, y: y + 0.38, w: w - 0.36, h: h - 0.46, fontFace: M, fontSize: opts.fs || 9, color: DARK, margin: 0, valign: "top" });
}

function tip(s, x, y, w, h, text, opts = {}) {
  const tint = opts.tone === "coral" ? TINT_CORAL : TINT_TEAL;
  const acc = opts.tone === "coral" ? CORAL : TEAL;
  s.addShape("rect", { x, y, w, h, fill: { color: tint }, line: { type: "none" } });
  s.addShape("rect", { x, y, w: 0.05, h, fill: { color: acc }, line: { type: "none" } });
  s.addText(text, { x: x + 0.28, y, w: w - 0.42, h, fontFace: F, fontSize: opts.fs || 11, bold: true, color: opts.color || DARK, valign: "middle", margin: 0 });
}

function bul(s, items, x, y, w, h, opts = {}) {
  s.addText(items.map((t, i) => ({
    text: t,
    options: { bullet: true, breakLine: i < items.length - 1, fontSize: opts.fs || 12, color: opts.color || DARK }
  })), { x, y, w, h, fontFace: F, valign: "top", margin: [0, 0, 0, 16], paraSpaceAfter: 4 });
}

function tbl(s, rows, x, y, w, colW, opts = {}) {
  s.addTable(rows, { x, y, w, fontFace: F, fontSize: opts.fs || 10, color: DARK, border: { pt: 0.5, color: BORDER }, fill: { color: WHITE }, colW, valign: "middle", autoPage: false });
}
function thd(t) { return { text: t, options: { fill: { color: NAVY }, color: WHITE, bold: true, fontSize: 9.5 } }; }

function dark(p) { const s = p.addSlide(); s.background = { color: NAVY_DARK }; return s; }

// ══════════════════════════════════════════
async function main() {
  const pres = new pptxgen();
  pres.layout = "LAYOUT_WIDE";
  pres.author = "TGFX Graphics Team";
  pres.title = "TGFX Shader Permutation \u00b7 \u65b9\u6848\u8d70\u8bfb";

  let pg = 0;
  const P = { kc: PURPLE, bb: "F0ECF7", bl: PURPLE };

  // ════════════════════════════════════════════════════════════════════
  // Page 1: Cover — 设定场景
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = dark(pres);
    s.addText("SHADER PERMUTATION \u65b9\u6848\u8d70\u8bfb", { x: MG, y: 1.8, w: 8, h: 0.3, fontFace: F, fontSize: 11, color: TEAL, bold: true, charSpacing: 3, margin: 0 });
    s.addText("\u4e00\u6b21 alpha-only \u7eb9\u7406 draw \u7684\u5168\u94fe\u8def\u8ffd\u8e2a", { x: MG, y: 2.2, w: 11, h: 0.8, fontFace: F, fontSize: 30, color: WHITE, bold: true, margin: 0 });
    s.addText("\u4ee5 TextureEffect \u5904\u7406\u4e00\u5f20 isAlphaOnly()=true \u7684\u7eb9\u7406\u4e3a\u4f8b\uff0c\u5b8c\u6574\u8d70\u5b8c\u65e7/\u65b0\u4e24\u6761\u8def\u5f84", { x: MG, y: 3.1, w: 10, h: 0.4, fontFace: F, fontSize: 14, color: ICE, margin: 0 });

    // Key numbers
    s.addShape("rect", { x: MG, y: 3.9, w: CW, h: 1.2, fill: { color: NAVY }, line: { type: "none" } });
    const nums = [
      { n: "15\u201370 ms", l: "\u9996\u5e27\u7d2f\u79ef\u5ef6\u8fdf \u2192 <1 ms" },
      { n: "~14 MB", l: "\u7f16\u8bd1\u5668\u4f53\u79ef\u53ef\u6d88\u9664" },
      { n: "36", l: "Processor \u9010\u6279\u63a5\u5165" },
    ];
    const nw = CW / 3;
    nums.forEach((st, i) => {
      s.addText(st.n, { x: MG + i * nw, y: 4.05, w: nw, h: 0.40, fontFace: M, fontSize: 18, bold: true, color: TEAL, align: "center", margin: 0 });
      s.addText(st.l, { x: MG + i * nw, y: 4.48, w: nw, h: 0.30, fontFace: F, fontSize: 10, color: ICE, align: "center", margin: 0 });
    });

    // Scenario description
    s.addShape("rect", { x: MG, y: 5.4, w: CW, h: 0.90, fill: { color: NAVY }, line: { color: "3E4F94", width: 0.75 } });
    s.addText("\u672c\u4f8b\u573a\u666f", { x: MG + 0.2, y: 5.48, w: 2, h: 0.22, fontFace: F, fontSize: 9, bold: true, color: TEAL, margin: 0 });
    s.addText("App \u7ed8\u5236\u4e00\u5f20 alpha-only \u7eb9\u7406\uff08\u5982\u5b57\u4f53\u56fe\u96c6\u3001\u5355\u901a\u9053 mask\uff09\n\u2022 textureProxy->isAlphaOnly() = true\n\u2022 \u65e0 YUV\u3001\u65e0 RGBAAA\u3001\u65e0 Subset\u3001\u65e0 Perspective", { x: MG + 0.2, y: 5.72, w: CW - 0.4, h: 0.55, fontFace: M, fontSize: 9.5, color: ICE, margin: 0 });
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 2: 旧路径完整 Trace
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u65e7\u8def\u5f84\u5b8c\u6574 TRACE", "\u672c\u4f8b\u5728\u73b0\u6709\u7cfb\u7edf\u4e2d\u7684\u6267\u884c\u5168\u8fc7\u7a0b",
      { badge: "flags=0b00000100 (bit2=alphaOnly)", bb: TINT_CORAL, bl: CORAL });

    // Left: call flow
    const lx = MG, lw = 6.5;
    const steps = [
      { t: "1. DrawOp::execute()", sub: "  \u2514\u2500 ProgramInfo::getProgram()", hot: false },
      { t: "2. computeProcessorKey()", sub: "  \u2514\u2500 TextureEffect: flags |= isAlphaOnly() ? 2 : 0\n  \u2514\u2500 \u4ea7\u51fa BytesKey\uff08\u4e0d\u900f\u660e 7-bit\uff09", hot: false },
      { t: "3. GlobalCache::findProgram(key)", sub: "  \u2514\u2500 MISS\uff08\u9996\u6b21\u89c1\u5230\u6b64 flags \u7ec4\u5408\uff09", hot: false },
      { t: "4. ProgramBuilder::CreateProgram()", sub: "  \u2514\u2500 GLSLTextureEffect::emitCode()\n  \u2514\u2500 C++ \u62fc\u63a5: color = vec4(0,0,0,color.a)", hot: true },
      { t: "5. shaderc (glslang \u2192 SPIR-V)", sub: "  \u2514\u2500 ~2\u20135 ms per shader variant", hot: true },
      { t: "6. spirv-cross \u2192 MSL / tint \u2192 WGSL", sub: "  \u2514\u2500 ~1\u20133 ms \u8de8\u8bed\u8a00\u7ffb\u8bd1", hot: true },
      { t: "7. GPU::createProgram()", sub: "  \u2514\u2500 \u5199\u56de GlobalCache", hot: false },
    ];
    steps.forEach((item, i) => {
      const iy = CT + i * 0.72;
      const bg = item.hot ? TINT_CORAL : "F8F9FC";
      s.addShape("rect", { x: lx, y: iy, w: lw, h: 0.66, fill: { color: bg }, line: { color: item.hot ? CORAL : BORDER, width: 0.5 } });
      s.addText(item.t, { x: lx + 0.12, y: iy + 0.02, w: lw - 0.24, h: 0.26, fontFace: M, fontSize: 9.5, bold: true, color: item.hot ? CORAL : DARK, valign: "middle", margin: 0 });
      s.addText(item.sub, { x: lx + 0.12, y: iy + 0.28, w: lw - 0.24, h: 0.34, fontFace: M, fontSize: 8.5, color: item.hot ? CORAL : MUTED, valign: "top", margin: 0 });
    });

    // Right: key encoding detail
    const rx = lx + lw + 0.30, rw = CW - lw - 0.30;
    s.addText("TextureEffect\nonComputeProcessorKey()", { x: rx, y: CT, w: rw, h: 0.36, fontFace: M, fontSize: 9, bold: true, color: DARK, margin: 0 });
    codeBox(s, rx, CT + 0.42, rw, 2.60, "\u73b0\u6709 7-bit flags \u7f16\u7801",
`uint32_t flags = 0;
// bit0: alphaStart==Zero \u2192 \u201c\u65e0RGBAAA\u201d\u53cd\u903b\u8f91
flags |= (alphaStart == Zero()) ? 1 : 0;
// bit1: reserved(unused)
// bit2: isAlphaOnly()     \u2190 \u672c\u4f8b=1
flags |= isAlphaOnly() ? 4 : 0;
// bit3: yuvFormat!=I420 (YUV only)
// bit4: yuv limited range (YUV only)
// bit5: needSubset()
// bit6: constraint==Strict

// \u672c\u4f8b\u4ea7\u51fa: flags = 0b0000100 = 4
// \u7b49\u4ef7\u4e8e: "alphaOnly=true, \u5176\u4f59\u5168false"`, { fs: 8 });
    tip(s, rx, CT + 3.15, rw, 0.60, "\u95ee\u9898\uff1a\u7b2c 4\u20136 \u6b65\u5728\u6e32\u67d3\u63d0\u4ea4\u5173\u952e\u8def\u5f84\u4e0a\n\u4f46\u5b83\u4eec\u7684\u8f93\u51fa\u5b8c\u5168\u7531 flags=4 \u51b3\u5b9a\u2014\u2014\u786e\u5b9a\u6027\u8ba1\u7b97", { tone: "coral", fs: 10 });
    ftr(s, pg, "\u65e7\u8def\u5f84");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 3: 核心洞察 + PermutationDomain 编码
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u6838\u5fc3\u6d1e\u5bdf", "emitCode() \u7684\u8f93\u51fa\u662f flags \u7684\u51fd\u6570\u2014\u2014\u53ef\u6784\u5efa\u671f\u7a77\u4e3e",
      { badge: "flags \u2208 [0, 2\u2078) = 256 \u88f8\u7ec4\u5408", ...P });

    // Insight box
    tip(s, MG, CT, CW, 0.55, "\u76f8\u540c flags \u5fc5\u7136\u4ea7\u51fa\u76f8\u540c GLSL \u2192 \u76f8\u540c SPIR-V \u2192 \u76f8\u540c MSL/WGSL\n\u7ed3\u8bba\uff1a\u6784\u5efa\u671f\u7a77\u4e3e\u6240\u6709\u6709\u6548 flags\uff0c\u9884\u7f16\u8bd1\u6253\u5305\uff0c\u8fd0\u884c\u65f6\u67e5\u8868\u5373\u53ef");

    // Domain encoding formula
    s.addText("PermutationDomain \u7f16\u7801\u516c\u5f0f\uff08mixed-radix LSB-first\uff09", { x: MG, y: CT + 0.70, w: CW, h: 0.24, fontFace: F, fontSize: 11, bold: true, color: DARK, margin: 0 });
    codeBox(s, MG, CT + 1.0, CW, 1.30, "\u7f16\u7801\u516c\u5f0f\u4e0e\u5168 bool \u7279\u4f8b",
`// \u901a\u7528\u516c\u5f0f
index = \u03a3_i v[i] * stride[i],   stride[0]=1, stride[i]=stride[i-1]*dim[i-1].valueCount()

// \u5168 bool \u7ef4\u5ea6\u65f6\u9000\u5316\u4e3a\u6807\u51c6\u4f4d\u6253\u5305\uff08\u56e0\u4e3a\u6bcf\u4e2a dim valueCount=2\uff09\uff1a
index = \u03a3_i v[i] * 2^i = \u4f4d\u62fc\u63a5

// \u672c\u4f8b\uff1a4 \u7ef4\u5ea6 bool, ALPHA_ONLY \u662f\u7b2c 1 \u4e2a\u7ef4\u5ea6\uff08\u6309\u58f0\u660e\u987a\u5e8f\uff09
//   values = [HAS_YUV=0, ALPHA_ONLY=1, HAS_RGBAAA=0, HAS_SUBSET=0]
//   index  = 0*1 + 1*2 + 0*4 + 0*8 = 2  (0b0010)`, { fs: 9 });

    // Table: dimension values for this example
    tbl(s, [
      [thd("\u7ef4\u5ea6"), thd("\u4f4d\u7f6e"), thd("\u672c\u4f8b\u53d6\u503c"), thd("\u523b\u5ea6")],
      ["HAS_YUV", "D::0", "0", "1"],
      ["ALPHA_ONLY", "D::1", "1  \u2190", "2"],
      ["HAS_RGBAAA", "D::2", "0", "4"],
      ["HAS_SUBSET", "D::3", "0", "8"],
    ], MG, CT + 2.50, 5.5, [2.0, 0.8, 1.2, 1.0], { fs: 10 });

    s.addText("\u672c\u4f8b\u8d2f\u7a7f\u6570\u636e", { x: MG + 6, y: CT + 2.50, w: 5, h: 0.24, fontFace: F, fontSize: 11, bold: true, color: TEAL, margin: 0 });
    s.addText("permutationIndex = 2 (0b0010)\n\u540e\u7eed\u6bcf\u9875\u8ffd\u8e2a\u8fd9\u4e2a\u6570\u636e\u5728\u5404\u9636\u6bb5\u7684\u5f62\u6001\u53d8\u5316", { x: MG + 6, y: CT + 2.80, w: 5, h: 0.50, fontFace: F, fontSize: 10, color: DARK, margin: 0 });

    // Scope note
    bul(s, [
      "\u751f\u4ea7\u7248 TextureFillShader \u5b9e\u9645\u6709 8 \u4e2a\u7ef4\u5ea6\uff08+YUV_FORMAT/YUV_RANGE/STRICT/PERSPECTIVE\uff09",
      "\u672c\u8d70\u8bfb\u7528 4 \u7ef4\u5ea6\u7b80\u5316\u7248\u8bb2\u89e3\uff0c\u673a\u5236\u5b8c\u5168\u76f8\u540c",
      "4 \u7ef4 bool \u2192 2\u2074=16 \u88f8\u7ec4\u5408 \u2192 ShouldCompile \u6392\u9664 6 \u2192 10 \u6709\u6548\u53d8\u4f53",
    ], MG + 6, CT + 3.40, 5, 1.40, { fs: 10 });
    ftr(s, pg, "\u8bbe\u8ba1\u6d1e\u5bdf");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 4: Step① 声明维度
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u65b0\u8def\u5f84 STEP \u2460", "\u58f0\u660e\u7ef4\u5ea6\uff1aTextureFillShader.h",
      { badge: "ALPHA_ONLY = D::1", ...P });

    codeBox(s, MG, CT, CW, 3.60, "src/gpu/shaders/level1/TextureFillShader.h  (\u5b8c\u6574\u4ee3\u7801)",
`namespace tgfx {

class TextureFillShader : public PrecompiledShader {
 public:
  // \u4e00\u884c\u5b8f\u58f0\u660e\u6240\u6709 bool \u7ef4\u5ea6\uff0c\u5c55\u5f00\u4e3a:
  //   struct Dims { enum : uint32_t { HAS_YUV=0, ALPHA_ONLY=1, ..., COUNT=4 };
  //                 static PermutationDomain domain(); };
  TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET);
  using D = Dims;
  static_assert(D::COUNT == 4, "Update ShouldCompile when dims change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureFillShader",
            "level1/texture_fill.vert",
            "level1/texture_fill.frag",
            D::domain(), ShouldCompile};
  }

 private:
  // YUV \u7eb9\u7406\u4e0d\u4f1a\u540c\u65f6\u662f alpha-only \u6216\u6709\u72ec\u7acb alpha \u5e73\u9762
  static bool ShouldCompile(const std::vector<int>& v) {
    bool hasYuv = v[D::HAS_YUV] != 0;
    bool alphaOnly = v[D::ALPHA_ONLY] != 0;
    bool hasRgbaaa = v[D::HAS_RGBAAA] != 0;
    return !(hasYuv && (alphaOnly || hasRgbaaa));  // \u6392\u9664\u7ed3\u6784\u6027\u4e0d\u53ef\u8fbe\u7ec4\u5408
  }
};

}  // namespace tgfx
TGFX_REGISTER_SHADER(tgfx::TextureFillShader)  // \u6784\u5efa\u5de5\u5177\u81ea\u52a8\u53d1\u73b0`, { fs: 8, proposed: true });

    // Right annotations
    const notes = [
      { y: CT + 0.72, t: "\u2190 TGFX_DEFINE_DIMS \u5b8f\u5c55\u5f00\u4e3a enum + domain()" },
      { y: CT + 1.12, t: "\u2190 \u7ef4\u5ea6\u6570\u53d8\u4e86\u8fd9\u91cc\u5c31\u7f16\u8bd1\u62a5\u9519" },
      { y: CT + 1.55, t: "\u2190 \u8fd4\u56de shader \u5143\u4fe1\u606f\uff08\u540d\u5b57 + \u6e90\u6587\u4ef6 + domain + \u88c1\u526a\u51fd\u6570\uff09" },
      { y: CT + 2.75, t: "\u2190 \u6784\u5efa\u5de5\u5177\u7528\u8fd9\u4e2a\u51fd\u6570\u8fc7\u6ee4\u6b7b\u53d8\u4f53" },
      { y: CT + 3.30, t: "\u2190 \u9759\u6001\u81ea\u6ce8\u518c\uff0c\u6784\u5efa\u5de5\u5177\u65e0\u9700\u89e3\u6790 C++ \u6e90\u7801" },
    ];
    notes.forEach(n => {
      s.addText(n.t, { x: MG + CW - 4.2, y: n.y, w: 4.2, h: 0.22, fontFace: F, fontSize: 9, italic: true, color: PURPLE, align: "right", margin: 0 });
    });

    // Macro expansion note
    tip(s, MG, CT + 3.75, CW, 0.40, "TGFX_DEFINE_DIMS \u5c55\u5f00\u4e3a\uff1aenum \u201c\u5177\u540d\u4e0b\u6807\u201d + FromBoolNames(#__VA_ARGS__) \u89e3\u6790\u4e3a PermutationDomain", { fs: 10 });
    bul(s, [
      "\u7ef4\u5ea6\u540d\u5373\u4e3a\u6784\u5efa\u671f\u5b8f\u540d\uff1a#define ALPHA_ONLY 1",
      "ShouldCompile \u901a\u8fc7\u5177\u540d\u4e0b\u6807 D::HAS_YUV / D::ALPHA_ONLY \u8bbf\u95ee\u2014\u2014\u4e0d\u4f1a\u9519\u4f4d",
      "\u672c\u4f8b: ShouldCompile([0,1,0,0]) = !(0 && (1||0)) = true \u2192 \u8be5\u7ec4\u5408\u4f1a\u88ab\u7f16\u8bd1",
    ], MG, CT + 4.25, CW, 1.10, { fs: 10 });
    ftr(s, pg, "\u7ef4\u5ea6\u58f0\u660e");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 5: Step② Shader 源码
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u65b0\u8def\u5f84 STEP \u2461", "\u5199 Shader \u6e90\u7801\uff1atexture_fill.frag",
      { badge: "#if ALPHA_ONLY \u2192 \u672c\u4f8b\u8d70\u6b64\u5206\u652f", ...P });

    codeBox(s, MG, CT, 6.8, 4.30, "src/gpu/shaders/level1/texture_fill.frag  (\u7eaf GLSL + #if \u5b8f\u6d88\u8d39)",
`#version 450
layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

#if HAS_YUV                             // \u672c\u4f8b=0, \u8df3\u8fc7
layout(set=0, binding=0) uniform sampler2D u_texY;
layout(set=0, binding=1) uniform sampler2D u_texU;
layout(set=0, binding=2) uniform sampler2D u_texV;
#else                                   // \u2190 \u672c\u4f8b\u8fdb\u5165\u6b64\u5206\u652f
layout(set=0, binding=0) uniform sampler2D u_sampler;
  #if HAS_RGBAAA                        // \u672c\u4f8b=0, \u8df3\u8fc7
  layout(set=0, binding=1) uniform sampler2D u_alphaSampler;
  #endif
#endif

#if HAS_SUBSET                          // \u672c\u4f8b=0, \u8df3\u8fc7
layout(set=0, binding=3) uniform SubsetBlock { vec4 u_subset; };
#endif

void main() {
  vec4 color = texture(u_sampler, v_texCoord);
  #if ALPHA_ONLY                        // \u2190\u2190\u2190 \u672c\u4f8b=1, \u547d\u4e2d!
    color = vec4(0.0, 0.0, 0.0, color.a);
  #elif HAS_RGBAAA
    color.a = texture(u_alphaSampler, v_texCoord).r;
  #endif
  fragColor = color;
}`, { fs: 8, proposed: true });

    // Side notes
    const rx = MG + 7.1, rw = CW - 7.1 + MG;
    s.addText("\u4e0e\u73b0\u6709 emitCode() \u7684\u5bf9\u5e94", { x: rx, y: CT, w: rw, h: 0.24, fontFace: F, fontSize: 10, bold: true, color: DARK, margin: 0 });
    codeBox(s, rx, CT + 0.30, rw, 1.60, "\u73b0\u6709 GLSLTextureEffect::emitCode() \u7b49\u4ef7\u903b\u8f91",
`// C++ \u62fc\u63a5\uff08\u73b0\u72b6\uff09
if (isAlphaOnly()) {
  fragBuilder->codeAppendf(
    "%s = vec4(0,0,0,%s.a);",
    args.outputColor, sample);
}
// \u201c\u7ffb\u8bd1\u201d\u4e3a #if ALPHA_ONLY \u5206\u652f\u5373\u53ef`, { fs: 8 });
    bul(s, [
      "bool \u7ef4\u5ea6\u7528 #if DIMNAME",
      "enum \u7ef4\u5ea6\u7528 #if DIMNAME == N",
      "\u6bcf\u4e2a\u7ef4\u5ea6\u65e0\u8bba 0/1 \u90fd\u4f1a\u751f\u6210 #define",
      "\u5171\u4eab\u4ee3\u7801\u653e src/gpu/shaders/common/*.glsl",
      "\u7b49\u4ef7\u4e8e\u628a emitCode() \u201c\u7ffb\u8bd1\u201d\u4e3a\u7eaf GLSL",
    ], rx, CT + 2.10, rw, 2.50, { fs: 10 });
    ftr(s, pg, "Shader \u6e90\u7801");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 6: Step③ Processor 接入
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u65b0\u8def\u5f84 STEP \u2462", "Processor \u63a5\u5165\uff1aonComputePermutationValues()",
      { badge: "values=[0,1,0,0] \u2192 index=2", ...P });

    // Left: old method
    codeBox(s, MG, CT, col2W, 2.80, "\u73b0\u6709\uff1aonComputeProcessorKey()  [7-bit \u53cd\u903b\u8f91]",
`// src/gpu/processors/TextureEffect.cpp
uint32_t flags = 0;
// bit0: \u201c\u65e0RGBAAA\u201d\u53cd\u903b\u8f91!
flags |= (alphaStart == Point::Zero()) ? 1 : 0;
// bit1: reserved
// bit2: isAlphaOnly()
flags |= isAlphaOnly() ? 4 : 0;
// bit3: yuvFormat!=I420 (only if YUV)
// bit4: yuvRange limited (only if YUV)
// bit5: needSubset()
// bit6: constraint==Strict
BytesKey key;
key.write(flags);  // \u4e0d\u900f\u660e, \u4e0d\u53ef\u5bfb\u5740
// \u672c\u4f8b\u4ea7\u51fa: flags=4`, { fs: 8 });

    // Right: new method
    codeBox(s, MG + col2W + 0.40, CT, col2W, 2.80, "\u65b0\u589e\uff1aonComputePermutationValues()  [\u6b63\u903b\u8f91, \u5177\u540d\u7d22\u5f15]",
`// src/gpu/processors/TextureEffect.cpp (NEW)
std::vector<int>
TextureEffect::onComputePermutationValues() const {
  using D = TextureFillShader::Dims;
  std::vector<int> values(D::COUNT, 0);
  values[D::HAS_YUV]    = (yuvTexture != nullptr) ? 1 : 0;
  values[D::ALPHA_ONLY] = textureProxy->isAlphaOnly() ? 1 : 0;
  values[D::HAS_RGBAAA] = (alphaStart != Point::Zero()) ? 1 : 0;
  values[D::HAS_SUBSET] = needSubset() ? 1 : 0;
  return values;
  // \u672c\u4f8b\u4ea7\u51fa: [0, 1, 0, 0]
  // encode() \u2192 index = 0*1 + 1*2 + 0*4 + 0*8 = 2
}`, { fs: 8, proposed: true });

    // Comparison note
    tip(s, MG, CT + 2.95, CW, 0.50, "\u5de6\u53f3\u5bf9\u6bd4\uff1a\u540c\u6837\u7684\u72b6\u6001\u6620\u5c04\uff0c\u4ece\u201c\u4e0d\u900f\u660e bit \u53cd\u903b\u8f91\u62fc\u63a5\u201d\u53d8\u4e3a\u201c\u5177\u540d\u6b63\u903b\u8f91\u7ef4\u5ea6\u503c\u201d\n\u4e24\u4e2a\u65b9\u6cd5\u5171\u5b58\u2014\u2014\u4e0d\u5220\u65e7\u65b9\u6cd5\uff0c\u4e0d\u5f71\u54cd\u73b0\u6709\u884c\u4e3a", { fs: 10 });

    // Key differences table
    tbl(s, [
      [thd("\u5bf9\u6bd4\u9879"), thd("\u65e7 onComputeProcessorKey()"), thd("\u65b0 onComputePermutationValues()")],
      ["\u8f93\u51fa\u7c7b\u578b", "\u4e0d\u900f\u660e BytesKey", "\u53ef\u5bfb\u5740 vector<int>"],
      ["\u903b\u8f91\u65b9\u5411", "bit0\u201c\u65e0RGBAAA\u201d\u53cd\u903b\u8f91", "D::HAS_RGBAAA \u6b63\u903b\u8f91"],
      ["\u7ef4\u5ea6\u6570", "7 bit (YUV\u6027\u8d28\u9690\u542b)", "4/8 \u663e\u5f0f\u7ef4\u5ea6"],
      ["\u53ef\u6784\u5efa\u671f\u5bfb\u5740", "\u5426", "\u662f\uff1adomain.encode(values) \u2192 index"],
    ], MG, CT + 3.60, CW, [1.8, 3.5, 4.0], { fs: 9.5 });

    bul(s, [
      "\u8fc1\u79fb\u65b9\u5f0f\uff1a\u628a\u73b0\u6709 flags \u7684\u53cd\u903b\u8f91\u7ea0\u6b63\u4e3a\u6b63\u903b\u8f91",
      "\u6bcf\u4e2a Processor \u72ec\u7acb\u63a5\u5165\u3001\u72ec\u7acb\u6d4b\u8bd5\uff0c\u7ea6 10\u201330 \u884c\u4ee3\u7801",
    ], MG, CT + 5.00, CW, 0.60, { fs: 10 });
    ftr(s, pg, "Processor \u63a5\u5165");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 7: 构建期 trace
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u6784\u5efa\u671f TRACE", "shader_build_tool \u7528\u672c\u4f8b\u7684 index=2 \u8dd1\u4e00\u904d",
      { badge: "decode(2)\u2192[0,1,0,0]\u2192#define ALPHA_ONLY 1", ...P });

    // Horizontal flow
    const flowY = CT;
    const flowSteps = [
      "\u2460 \u626b\u63cf\u6ce8\u518c\u8868\nShaderRegistry::All()",
      "\u2461 \u7a77\u4e3e\u7ec4\u5408\ndomain.decode(2)",
      "\u2462 ShouldCompile\n\u88c1\u526a\u68c0\u67e5",
      "\u2463 \u5b8f\u5c55\u5f00\ndefineListFor(2)",
      "\u2464 \u540e\u7aef\u7f16\u8bd1\nGLSL\u2192SPIR-V\u2192MSL",
      "\u2465 \u5199\u5165 Bundle\nIndexEntry{hash,blob}",
    ];
    const fsw = (CW - 0.16 * 5) / 6;
    flowSteps.forEach((t, i) => {
      const fx = MG + i * (fsw + 0.16);
      const highlight = (i === 3 || i === 4);
      s.addShape("rect", { x: fx, y: flowY, w: fsw, h: 1.05, fill: { color: highlight ? TINT_TEAL : "F0F1F8" }, line: { color: highlight ? TEAL : BORDER, width: 0.75 } });
      s.addText(t, { x: fx + 0.06, y: flowY + 0.06, w: fsw - 0.12, h: 0.93, fontFace: F, fontSize: 8.5, color: DARK, align: "center", valign: "middle", margin: 0 });
      if (i < flowSteps.length - 1) {
        s.addText("\u2192", { x: fx + fsw, y: flowY + 0.35, w: 0.16, h: 0.30, fontFace: F, fontSize: 12, color: MUTED, align: "center", valign: "middle", margin: 0 });
      }
    });

    // Detailed trace for this example
    codeBox(s, MG, CT + 1.20, CW, 1.80, "\u672c\u4f8b\u8be6\u7ec6\u8ffd\u8e2a\uff1aindex=2 (TextureFillShader)",
`// \u2461 decode(2) \u2192 values = [0, 1, 0, 0]  (HAS_YUV=0, ALPHA_ONLY=1, HAS_RGBAAA=0, HAS_SUBSET=0)
// \u2462 ShouldCompile([0,1,0,0]) = !(0 && (1||0)) = true  \u2192 \u7f16\u8bd1

// \u2463 defineListFor(2) \u4ea7\u51fa\u4ee5\u4e0b 4 \u884c\u5b8f\uff0c\u524d\u7f6e\u5230 .frag \u6e90\u7801\u9876\u90e8:
#define HAS_YUV 0
#define ALPHA_ONLY 1       // \u2190 \u672c\u4f8b\u6838\u5fc3\u5206\u652f
#define HAS_RGBAAA 0
#define HAS_SUBSET 0

// \u2464 \u5b8f\u5c55\u5f00\u540e shader \u51c0\u4ee3\u7801\uff08\u6b7b\u5206\u652f\u5df2\u88ab\u9884\u5904\u7406\u5668\u5265\u79bb\uff09:
//   color = vec4(0.0, 0.0, 0.0, color.a);  // ALPHA_ONLY \u5206\u652f\u547d\u4e2d
// \u2192 glslang \u2192 SPIR-V blob (Vulkan)
// \u2192 spirv-cross \u2192 MSL text (Metal)
// \u2192 tint \u2192 WGSL text (WebGPU)
// \u2192 \u4fdd\u7559 GLSL text (OpenGL)`, { fs: 8.5 });

    // Bundle format summary
    s.addText("Bundle \u6587\u4ef6\u683c\u5f0f\uff08\u6bcf\u4e2a backend \u5404\u4e00\u4efd\uff09", { x: MG, y: CT + 3.15, w: CW, h: 0.24, fontFace: F, fontSize: 10, bold: true, color: DARK, margin: 0 });
    tbl(s, [
      [thd("\u533a\u6bb5"), thd("\u5185\u5bb9"), thd("\u672c\u4f8b\u5bf9\u5e94")],
      ["FileHeader", "magic(TGSH) + version + backend + entryCount", "entryCount = 10 (\u6709\u6548\u53d8\u4f53\u6570)"],
      ["IndexEntry[]", "\u6309 Hash128(PipelineKey) \u5b57\u5178\u5e8f\u6392\u5217\u7684\u7d22\u5f15", "index=2 \u7684 entry: {hashHi, hashLo, blobOffset, blobSize, reflOffset}"],
      ["Reflection Pool", "UniformDesc[]/SamplerDesc[]/AttributeDesc[]", "u_sampler: binding=0, type=sampler2D"],
      ["Shader Data Pool", "VS/FS \u4e8c\u8fdb\u5236\u6216\u6587\u672c", "\u672c\u4f8b\u7684 SPIR-V blob / MSL text / ..."],
    ], MG, CT + 3.45, CW, [1.8, 4.5, 4.5], { fs: 9 });

    tip(s, MG, CT + 4.95, CW, 0.40, "\u5f00\u53d1\u4f53\u9a8c\uff1a\u6539 .frag \u2192 Ninja \u81ea\u52a8\u91cd\u8dd1 shader_build_tool \u2192 \u4ec5\u91cd\u7f16\u53d7\u5f71\u54cd\u53d8\u4f53\uff0c\u7b49\u540c\u6539 .cpp", { fs: 10 });
    ftr(s, pg, "\u6784\u5efa\u671f");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 8: 运行时 trace
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u8fd0\u884c\u65f6 TRACE", "\u672c\u4f8b\u7684\u65b0\u8def\u5f84\u5b8c\u6574\u6267\u884c\u6d41\u7a0b",
      { badge: "Hash128(index=2) \u2192 Bundle \u4e8c\u5206\u67e5\u627e\u547d\u4e2d", ...P });

    // Vertical flow with detail
    const steps = [
      { t: "1. DrawOp::execute()", sub: "\u5165\u53e3\u2014\u2014\u4e0e\u65e7\u8def\u5f84\u76f8\u540c", hit: false },
      { t: "2. EffectDecomposer::Decompose(gp, [TextureEffect], xp, blendMode)", sub: "\u8bc6\u522b Processor \u7ec4\u5408 \u2192 \u547d\u4e2d TextureFillShader \u2192 \u5355 Pass", hit: false },
      { t: "3. TextureEffect::onComputePermutationValues()", sub: "\u4ea7\u51fa [0,1,0,0] \u2192 encode() \u2192 permutationIndex = 2", hit: false },
      { t: "4. PipelineKey{\"TextureFillShader\", index=2, rtFormat, sampleCount, blendState}", sub: "\u2192 Hash128() \u2192 128-bit key \u2192 GlobalCache::findProgram() \u2192 MISS", hit: false },
      { t: "5. PrecompiledShaderCache::find(key)", sub: "\u4e8c\u5206\u67e5\u627e IndexEntry[] \u2192 \u547d\u4e2d! \u8fd4\u56de SPIR-V blob + ReflectionEntry", hit: true },
      { t: "6. CreateProgramFromBlob(context, blob)", sub: "\u7528\u9884\u7f16\u8bd1\u4ea7\u7269\u521b\u5efa Program \u2192 \u5199\u56de GlobalCache", hit: false },
      { t: "7. setData() + bindPipelineAndDraw()", sub: "\u586b\u5145 Uniform\u3001\u63d0\u4ea4 GPU \u547d\u4ee4\u2014\u2014\u4e0e\u65e7\u8def\u5f84\u76f8\u540c", hit: false },
    ];
    steps.forEach((st, i) => {
      const sy = CT + i * 0.70;
      s.addShape("rect", { x: MG, y: sy, w: CW, h: 0.62, fill: { color: st.hit ? TINT_TEAL : "F8F9FC" }, line: { color: st.hit ? TEAL : BORDER, width: 0.75 } });
      s.addText(st.t, { x: MG + 0.14, y: sy + 0.02, w: CW - 0.28, h: 0.28, fontFace: M, fontSize: 9, bold: true, color: st.hit ? TEAL : DARK, valign: "middle", margin: 0 });
      s.addText(st.sub, { x: MG + 0.14, y: sy + 0.32, w: CW - 0.28, h: 0.26, fontFace: F, fontSize: 9, color: st.hit ? TEAL : MUTED, valign: "middle", margin: 0 });
      if (i < steps.length - 1) {
        s.addText("\u2193", { x: MG + 0.2, y: sy + 0.62, w: 0.20, h: 0.08, fontFace: F, fontSize: 8, color: MUTED, align: "center", margin: 0 });
      }
    });

    // Fallback note
    tip(s, MG, CT + 5.10, CW, 0.50, "\u5146\u5e95\uff1a\u82e5 PrecompiledShaderCache \u672a\u547d\u4e2d \u2192 \u56de\u9000\u5230 ProgramBuilder::CreateProgram()\n\u8fc7\u6e21\u671f\u4e24\u6761\u8def\u5f84\u5171\u5b58\uff0c\u4e0d\u4e2d\u65ad\u6e32\u67d3\uff1b100% \u8986\u76d6\u540e\u624d\u4e0b\u7ebf\u65e7\u8def\u5f84", { fs: 10 });
    ftr(s, pg, "\u8fd0\u884c\u65f6");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 9: Before/After 全链路对照
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = pres.addSlide();
    hdr(s, "\u5bf9\u6bd4\u603b\u7ed3", "\u540c\u4e00\u6b21 alpha-only draw\uff1a\u65e7\u8def\u5f84 7 \u6b65 vs \u65b0\u8def\u5f84 7 \u6b65");

    const lx = MG, lw = col2W, rx = MG + col2W + 0.40, rw = col2W;

    // Before column
    s.addShape("rect", { x: lx, y: CT, w: lw, h: 4.30, fill: { color: TINT_CORAL }, line: { color: BORDER, width: 0.75 } });
    s.addText("BEFORE", { x: lx + 0.16, y: CT + 0.06, w: 2, h: 0.22, fontFace: F, fontSize: 9, bold: true, color: CORAL, charSpacing: 2, margin: 0 });
    const bi = [
      { t: "DrawOp::execute()", h: false },
      { t: "computeProcessorKey() \u2192 flags=4", h: false },
      { t: "GlobalCache miss", h: false },
      { t: "emitCode() C++ \u62fc\u63a5 GLSL", h: true },
      { t: "shaderc \u2192 SPIR-V  (~3ms)", h: true },
      { t: "spirv-cross \u2192 MSL  (~2ms)", h: true },
      { t: "createProgram() + setData()", h: false },
    ];
    bi.forEach((item, i) => {
      const iy = CT + 0.36 + i * 0.55;
      s.addShape("ellipse", { x: lx + 0.14, y: iy + 0.06, w: 0.22, h: 0.22, fill: { color: item.h ? CORAL : MUTED }, line: { type: "none" } });
      s.addText(String(i + 1), { x: lx + 0.14, y: iy + 0.06, w: 0.22, h: 0.22, fontFace: F, fontSize: 8, bold: true, color: WHITE, align: "center", valign: "middle", margin: 0 });
      s.addText(item.t, { x: lx + 0.46, y: iy, w: lw - 0.62, h: 0.42, fontFace: F, fontSize: 10, color: item.h ? CORAL : DARK, valign: "middle", margin: 0 });
    });

    // After column
    s.addShape("rect", { x: rx, y: CT, w: rw, h: 4.30, fill: { color: TINT_TEAL }, line: { color: BORDER, width: 0.75 } });
    s.addText("AFTER", { x: rx + 0.16, y: CT + 0.06, w: 2, h: 0.22, fontFace: F, fontSize: 9, bold: true, color: TEAL, charSpacing: 2, margin: 0 });
    const ai = [
      { t: "DrawOp::execute()", h: false },
      { t: "EffectDecomposer \u2192 TextureFillShader", h: false },
      { t: "onComputePermutationValues() \u2192 index=2", h: false },
      { t: "PipelineKey \u2192 Hash128 \u2192 GlobalCache miss", h: false },
      { t: "PrecompiledShaderCache::find() \u547d\u4e2d", h: true },
      { t: "CreateProgramFromBlob() (<0.1ms)", h: true },
      { t: "setData() + bindPipelineAndDraw()", h: false },
    ];
    ai.forEach((item, i) => {
      const iy = CT + 0.36 + i * 0.55;
      s.addShape("ellipse", { x: rx + 0.14, y: iy + 0.06, w: 0.22, h: 0.22, fill: { color: item.h ? TEAL : MUTED }, line: { type: "none" } });
      s.addText(String(i + 1), { x: rx + 0.14, y: iy + 0.06, w: 0.22, h: 0.22, fontFace: F, fontSize: 8, bold: true, color: WHITE, align: "center", valign: "middle", margin: 0 });
      s.addText(item.t, { x: rx + 0.46, y: iy, w: rw - 0.62, h: 0.42, fontFace: F, fontSize: 10, color: item.h ? TEAL : DARK, valign: "middle", margin: 0 });
    });

    // Bottom summary
    tip(s, MG, CT + 4.45, CW, 0.55, "\u6d88\u9664\u7b2c 4\u20136 \u6b65\uff1a\u7d2f\u79ef\u5ef6\u8fdf 15\u201370 ms \u2192 <1 ms\n\u4f53\u79ef\uff1ashaderc + spirv-cross + tint \u5408\u8ba1 9\u201314 MB \u4e0d\u518d\u9700\u8981\u643a\u5e26\uff08\u7f16\u8bd1\u5668\u4ec5\u6784\u5efa\u671f\u4f7f\u7528\uff09", { fs: 10 });
    ftr(s, pg, "\u5bf9\u6bd4");
  }

  // ════════════════════════════════════════════════════════════════════
  // Page 10: 容错 + 迁移 + Q&A
  // ════════════════════════════════════════════════════════════════════
  { pg++;
    const s = dark(pres);
    s.addText("\u5bb9\u9519\u4f53\u7cfb\u3001\u8fc1\u79fb\u7b56\u7565\u4e0e\u5de5\u7a0b\u89c4\u6a21", { x: MG, y: 0.40, w: 10, h: 0.36, fontFace: F, fontSize: 17, bold: true, color: WHITE, margin: 0 });

    // Three-layer fault tolerance
    const c3w = (CW - 0.40) / 3;
    const layers = [
      { t: "\u7f16\u8bd1\u671f", d: "static_assert(D::COUNT==N)\ninfo() \u7eaf\u865a\u51fd\u6570\u7ea6\u675f\n\u7ef4\u5ea6\u589e\u5220\u5fc5\u7136\u7f16\u8bd1\u5931\u8d25" },
      { t: "\u6784\u5efa\u671f (9 \u9879\u68c0\u67e5)", d: "GLSL \u8bed\u6cd5\u6821\u9a8c (\u62a5\u7ef4\u5ea6\u53d6\u503c)\n\u6ce8\u518c\u8868 vs \u6587\u4ef6\u6bd4\u5bf9\nShouldCompile \u5168\u6392\u9664\u62a5\u9519\nUniform \u540d\u957f\u5ea6\u6821\u9a8c\nProcessor \u8986\u76d6\u7387\u68c0\u67e5" },
      { t: "\u8fd0\u884c\u65f6", d: "find()=null \u2192 fallback \u65e7\u8def\u5f84\nBundle hash \u4e0d\u5339\u914d \u2192 \u62d2\u7edd\u52a0\u8f7d\nDebug \u65ad\u8a00 / Release \u8bb0\u65e5\u5fd7" },
    ];
    layers.forEach((l, i) => {
      const lx2 = MG + i * (c3w + 0.20);
      s.addShape("rect", { x: lx2, y: 0.90, w: c3w, h: 1.80, fill: { color: NAVY }, line: { color: "3E4F94", width: 0.75 } });
      s.addText(l.t, { x: lx2 + 0.14, y: 0.96, w: c3w - 0.28, h: 0.24, fontFace: F, fontSize: 11, bold: true, color: TEAL, margin: 0 });
      s.addText(l.d, { x: lx2 + 0.14, y: 1.26, w: c3w - 0.28, h: 1.38, fontFace: F, fontSize: 9.5, color: ICE, margin: 0 });
    });

    // Migration strategy
    s.addShape("line", { x: MG, y: 2.90, w: CW, h: 0, line: { color: "3E4F94", width: 0.75 } });
    s.addText("\u8fc1\u79fb\u7b56\u7565", { x: MG, y: 3.05, w: 4, h: 0.26, fontFace: F, fontSize: 12, bold: true, color: WHITE, margin: 0 });
    const migItems = [
      "\u9010 Processor \u72ec\u7acb\u63a5\u5165\uff0c\u4e92\u4e0d\u5f71\u54cd\uff1b\u6bcf\u63a5\u4e00\u4e2a = \u5199 .h + .frag + \u91cd\u5199\u4e00\u4e2a\u65b9\u6cd5\uff0c\u8dd1\u622a\u56fe\u56de\u5f52",
      "RuntimeEffect \u4fdd\u6301\u72ec\u7acb\u7f16\u8bd1\u8def\u5f84\uff0c\u4e0d\u53d7\u5f71\u54cd",
      "\u65e7\u8def\u5f84 (ProgramBuilder) \u4e0e\u65b0\u8def\u5f84\u957f\u671f\u5171\u5b58\uff0c\u76f4\u5230 100% \u8986\u76d6\u540e\u624d\u4e0b\u7ebf",
      "\u4e0b\u7ebf\u662f\u72ec\u7acb\u91cc\u7a0b\u7891\u51b3\u7b56\uff0c\u9700\u53e6\u884c\u8bc4\u5ba1\u786e\u8ba4",
    ];
    migItems.forEach((t, i) => {
      s.addText("\u2022 " + t, { x: MG + 0.12, y: 3.35 + i * 0.30, w: CW - 0.24, h: 0.28, fontFace: F, fontSize: 10, color: ICE, margin: 0 });
    });

    // Stats
    s.addShape("line", { x: MG, y: 4.60, w: CW, h: 0, line: { color: "3E4F94", width: 0.75 } });
    const stats = [
      { n: "36", l: "Processor" },
      { n: "4\u21928", l: "\u7ef4\u5ea6/Shader" },
      { n: "~10", l: "\u6709\u6548\u53d8\u4f53/Shader" },
      { n: "<1 ms", l: "\u8fd0\u884c\u65f6\u67e5\u8868" },
      { n: "5 \u9636\u6bb5", l: "\u5206\u6b65\u843d\u5730" },
    ];
    const statW = CW / 5;
    stats.forEach((st, i) => {
      s.addText(st.n, { x: MG + i * statW, y: 4.75, w: statW, h: 0.34, fontFace: M, fontSize: 15, bold: true, color: TEAL, align: "center", margin: 0 });
      s.addText(st.l, { x: MG + i * statW, y: 5.10, w: statW, h: 0.22, fontFace: F, fontSize: 9.5, color: ICE, align: "center", margin: 0 });
    });

    // Q&A
    s.addShape("line", { x: MG, y: 5.50, w: CW, h: 0, line: { color: "3E4F94", width: 0.75 } });
    s.addText("Q & A", { x: MG, y: 5.65, w: 3, h: 0.35, fontFace: F, fontSize: 16, bold: true, color: TEAL, margin: 0 });
    s.addText("\u8bbe\u8ba1\u6587\u6863\uff1adocs/shader-permutation-system-design.md\n\u5b9e\u65bd\u8ba1\u5212\uff1adocs/shader-permutation-implementation-plan.md", { x: MG, y: 6.05, w: CW, h: 0.40, fontFace: M, fontSize: 9, color: MUTED, margin: 0 });
  }

  const out = __dirname + "/TGFX-Shader-Permutation-\u65b9\u6848\u8d70\u8bfb.pptx";
  await pres.writeFile({ fileName: out });
  console.log("Written:", out);
}

main().catch(e => { console.error(e); process.exit(1); });
