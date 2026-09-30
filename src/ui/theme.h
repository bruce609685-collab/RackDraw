#pragma once

// 配色与自绘工具。
// 配色全部取自 HTML 对照版 style.css，是界面与导出图的唯一来源（对应 §2.3 单一来源约定）：
// 禁止在绘制代码里另写颜色字面量。

// 需要 gdiplus 的扩展接口（Bitmap::Lock 等）
#ifndef GDIPVER
#  define GDIPVER 0x0110
#endif

#include <windows.h>
#include <gdiplus.h>

#include <cstdint>
#include <string>

namespace ui {

/* ---------------- 配色（0xRRGGBB） ---------------- */
constexpr uint32_t kPageBg      = 0xe9edf2;
constexpr uint32_t kTopbarBg    = 0xffffff;
constexpr uint32_t kTextMain    = 0x2d3748;
constexpr uint32_t kTextDim     = 0x718096;
constexpr uint32_t kTextCtl     = 0x4a5568;
constexpr uint32_t kTextFaint   = 0xa0aec0;
constexpr uint32_t kBorder      = 0xcbd5e0;
constexpr uint32_t kBorderSoft  = 0xe2e8f0;
constexpr uint32_t kPanelSoft   = 0xf7fafc;
constexpr uint32_t kPrimary     = 0x2b6cb0;
constexpr uint32_t kPrimaryDark = 0x2c5282;
constexpr uint32_t kDangerText  = 0xc53030;
constexpr uint32_t kSideBorder  = 0xdde3ea;
constexpr uint32_t kScrollTrack = 0xeef1f5;   // 侧栏滚动条轨道
constexpr uint32_t kScrollThumb = 0x5a6a7d;   // 侧栏滚动条滑块（深色）
constexpr uint32_t kScrollThumbHot = 0x4a5568;
constexpr uint32_t kHoverBlue   = 0xebf4ff;
constexpr uint32_t kHoverGray   = 0xf1f5f9;
constexpr uint32_t kSelGold     = 0xf6c344;
constexpr uint32_t kOkGreen     = 0x48bb78;
constexpr uint32_t kBadRed      = 0xf56565;
constexpr uint32_t kToastBg     = 0xc6f6d5;
constexpr uint32_t kToastText   = 0x22543d;
constexpr uint32_t kHintBg      = 0x2d3748;
constexpr uint32_t kStageTop    = 0xdfe5ec;
constexpr uint32_t kStageMid    = 0xeef1f5;
constexpr uint32_t kStageBottom = 0xe2e7ee;
constexpr uint32_t kRackEdge    = 0xc3cbd6;
constexpr uint32_t kRackEdge2   = 0xeef2f7;
constexpr uint32_t kRackMid     = 0xf7f9fc;
constexpr uint32_t kCapTopA     = 0xf2f5f9;
constexpr uint32_t kCapTopB     = 0xcdd5df;
constexpr uint32_t kCapBotB     = 0xb3bdc9;
constexpr uint32_t kInnerA      = 0xe6eaf0;
constexpr uint32_t kInnerB      = 0xeef1f5;
constexpr uint32_t kInnerC      = 0xe4e8ee;
constexpr uint32_t kRailA       = 0xd7dde5;
constexpr uint32_t kScrew       = 0x9aa5b1;
constexpr uint32_t kLabelText   = 0x5a6a7d;
constexpr uint32_t kBandDark    = 0x788eaa;   // 偶数 U 条纹（透明度 .17）
constexpr uint32_t kBandLight   = 0xffffff;   // 奇数 U 条纹（透明度 .30）
constexpr uint32_t kBandPad     = 0x96a8be;   // 上下留白（透明度 .13）
constexpr uint32_t kGridRef     = 0x50647d;   // 网格线基色（强 .28 / 弱 .13）
constexpr uint32_t kExportPage  = 0xeef1f6;
constexpr uint32_t kExportTitle = 0x1a2434;
constexpr uint32_t kSubText     = 0x718096;
constexpr uint32_t kFooterText  = 0x8a97a6;
constexpr uint32_t kShadow      = 0x141e2d;
constexpr uint32_t kLedGreen    = 0x68d391;
constexpr uint32_t kLedAmber    = 0xf6ad55;
constexpr uint32_t kPortDark    = 0x141c26;
constexpr uint32_t kBayDark     = 0x2b3a4d;   // 监控录像机的硬盘位
constexpr uint32_t kPortGold    = 0xd69e2e;
constexpr uint32_t kScWhite     = 0xedf2f7;
constexpr uint32_t kScRing      = 0x4a5568;
constexpr uint32_t kRingWhite   = 0xf0f3f7;
constexpr uint32_t kDeviceEdge  = 0x000000;

/* ---------------- 颜色构造 ---------------- */
inline Gdiplus::Color Col(uint32_t hex, int alpha = 255) {
    return Gdiplus::Color(static_cast<BYTE>(alpha),
                          static_cast<BYTE>((hex >> 16) & 0xff),
                          static_cast<BYTE>((hex >> 8) & 0xff),
                          static_cast<BYTE>(hex & 0xff));
}
inline Gdiplus::Color Shade(uint32_t hex, float factor, int alpha = 255) {
    const int r = static_cast<int>(((hex >> 16) & 0xff) * factor);
    const int g = static_cast<int>(((hex >> 8) & 0xff) * factor);
    const int b = static_cast<int>((hex & 0xff) * factor);
    return Gdiplus::Color(static_cast<BYTE>(alpha),
                          static_cast<BYTE>(r > 255 ? 255 : r),
                          static_cast<BYTE>(g > 255 ? 255 : g),
                          static_cast<BYTE>(b > 255 ? 255 : b));
}
inline Gdiplus::Color Mix(uint32_t a, uint32_t b, float percentA, int alpha = 255) {
    const float pa = percentA / 100.0f, pb = 1.0f - pa;
    auto ch = [&](int shift) {
        return static_cast<BYTE>(((a >> shift) & 0xff) * pa + ((b >> shift) & 0xff) * pb);
    };
    return Gdiplus::Color(static_cast<BYTE>(alpha), ch(16), ch(8), ch(0));
}

/* ---------------- 字体 ---------------- */
// 按像素字号取字体（同一字号复用；粗体用于标题/强调）
Gdiplus::Font* Font(float pixelSize, bool bold = false);

/* ---------------- 基础绘制 ---------------- */
void AddRoundedRect(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& r, float radius);
void FillSolid(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint32_t color, int alpha = 255);
void FillRounded(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, uint32_t color,
                 int alpha = 255);
// 纵向渐变（midStop < 0 时只用两段）
void FillVertical(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint32_t top, uint32_t bottom,
                  float midStop = -1.0f, uint32_t midColor = 0);
void FillRoundedVertical(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                         uint32_t top, uint32_t bottom, float midStop = -1.0f, uint32_t midColor = 0);
// 横向渐变（多段，用于机柜金属质感）
void FillHorizontalStops(Gdiplus::Graphics& g, const Gdiplus::RectF& r,
                         const float* stops, const uint32_t* colors, int count);
void StrokeRounded(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, uint32_t color,
                   float width = 1.0f, int alpha = 255);
void StrokeLine(Gdiplus::Graphics& g, float x1, float y1, float x2, float y2, uint32_t color,
                float width = 1.0f, int alpha = 255);

/* ---------------- 文字 ---------------- */
// 省略号截断到 maxWidth（maxWidth <= 0 表示不截断）
std::wstring Ellipsize(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                       float maxWidth);
void TextLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
              const Gdiplus::RectF& box, uint32_t color, int alpha = 255);
void TextCenter(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                const Gdiplus::RectF& box, uint32_t color, int alpha = 255);
void TextRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
               const Gdiplus::RectF& box, uint32_t color, int alpha = 255);
// 以「垂直中线」为基准绘制（对应 Canvas 的 textBaseline = middle 与 CSS 的行盒居中）
void TextMiddleLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                    float x, float centerY, uint32_t color, float maxWidth = 0, int alpha = 255);
void TextMiddleCenter(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                      float centerX, float centerY, uint32_t color, float maxWidth = 0,
                      int alpha = 255);
// 以「垂直中线 + 右边界」为基准绘制
void TextMiddleRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                     float rightX, float centerY, uint32_t color, float maxWidth = 0,
                     int alpha = 255);
float TextWidth(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font);

/* ---------------- 变换下的清晰文字 ---------------- */
enum class CrispAlign { Left, Center, Right };

// 在「缩放 + 平移」变换下绘制文字时，GDI+ 会失去栅格贴合（屏幕上看就是发虚、笔画缺像素）。
// TextCrisp 的做法：先读当前变换，临时还原成单位变换，把锚点与字号换算到设备像素后再绘制，
// 于是任何缩放下文字都是按最终像素栅格渲染的。传入的是逻辑字号与逻辑坐标（与直接从前的调用一致）。
void TextCrisp(Gdiplus::Graphics& g, const std::wstring& text, float logicalSize, bool bold,
               CrispAlign align, float anchorX, float centerY, uint32_t color,
               float maxWidthLogical = 0.0f, int alpha = 255);

/* ---------------- 高 DPI 换算 ---------------- */
// 界面按 96 DPI 设计，绘制/命中测试统一走这里换算（导出图不走，保持逻辑单位）
int Scale(int value);
float ScaleF(float value);

}  // namespace ui
