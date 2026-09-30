#include "ui/rack_view.h"

#include <algorithm>
#include <cmath>

#include "core/catalog.h"
#include "ui/theme.h"

namespace ui {
namespace {

using rack::Device;
using rack::State;

float SlotH() { return rack::SlotHExact(); }
float ColW() { return rack::ColW(); }

const Gdiplus::RectF Bar(float x, float y, float w, float h) {
    return Gdiplus::RectF(x, y, w, h);
}

/* 内凹阴影：沿内框四边由外向内画几条半透明描边，近似 CSS 的 inset box-shadow */
void InnerShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& r) {
    for (int i = 0; i < 3; ++i) {
        const int alpha = 30 - i * 9;
        Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(alpha), 110, 125, 145), 1.0f);
        Gdiplus::RectF box(r.X + i * 0.5f, r.Y + i * 0.5f, r.Width - i, r.Height - i);
        g.DrawRectangle(&pen, box);
    }
}

/* 机柜落影（近似 CSS 的 0 10px 24px rgba(20,30,45,.22)） */
void FloorShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& frame) {
    for (int i = 6; i >= 1; --i) {
        const int alpha = 6 + (6 - i) * 3;
        Gdiplus::RectF box(frame.X + frame.Width * 0.02f - i * 2.0f,
                           frame.Y + 6.0f + i * 1.5f,
                           frame.Width * 0.96f + i * 4.0f,
                           frame.Height + i * 2.0f);
        Gdiplus::GraphicsPath path;
        AddRoundedRect(&path, box, 12.0f);
        Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(alpha), 20, 30, 45));
        g.FillPath(&brush, &path);
    }
}

/* 设备面板上的重复小元素（网口/RJ45/散热孔/理线环/配线口） */
void DrawRepeatPorts(Gdiplus::Graphics& g, const Device& d, const Gdiplus::RectF& box) {
    const int count = rack::PortCount(d);
    if (count <= 0) return;
    const float inner = box.Width - 8.0f;
    const float step = inner / static_cast<float>(count);
    const float midY = box.Y + 13.0f;
    for (int i = 0; i < count; ++i) {
        const float cx = box.X + 4.0f + step * static_cast<float>(i) + (step - 8.0f) / 2.0f;

        if (rack::IsCableMgr(d)) {                       // 理线环：白色圆环
            Gdiplus::Pen pen(Col(kRingWhite), 1.8f);
            g.DrawEllipse(&pen, cx + 4.0f - 4.0f, box.Y + box.Height / 2.0f - 3.0f - 4.0f, 8.0f, 8.0f);
            continue;
        }
        if (rack::IsPatchPanel(d)) {                     // 配线口：深色小方块
            Gdiplus::RectF cell(cx, box.Y + box.Height / 2.0f - 6.0f, 8.0f, 6.0f);
            FillRounded(g, cell, 1.0f, ui::kPortDark);
            StrokeRounded(g, cell, 1.0f, 0xffffff, 1.0f, 77);
            continue;
        }
        if (rack::IsRecorder(d)) {                       // 监控录像机：硬盘位
            Gdiplus::RectF bay(cx, midY - 3.0f, 8.0f, 6.0f);
            FillRounded(g, bay, 1.0f, kBayDark);
            StrokeRounded(g, bay, 1.0f, 0xffffff, 1.0f, 89);
            continue;
        }
        if (d.cat == rack::Category::Switch ||
            d.cat == rack::Category::Monitor) {          // RJ45：上金下黑（交换机 / 监控交换机）
            const float pw = 7.0f, ph = 5.5f;
            Gdiplus::RectF gold(cx, midY - ph / 2.0f, pw, ph * 0.35f);
            Gdiplus::RectF dark(cx, midY - ph / 2.0f + ph * 0.35f, pw, ph * 0.65f);
            FillSolid(g, gold, kPortGold);
            FillSolid(g, dark, kPortDark);
            Gdiplus::RectF outline(cx + 0.5f, midY - ph / 2.0f + 0.5f, pw - 1.0f, ph - 1.0f);
            StrokeRounded(g, outline, 0.0f, 0x000000, 1.0f, 128);
            continue;
        }
        if (d.cat == rack::Category::Fw) {               // 散热孔：细长条
            FillSolid(g, Bar(cx, midY - 1.0f, 8.0f, 2.0f), 0x000000, 115);
            continue;
        }
        Gdiplus::RectF jack(cx, midY - 2.5f, 9.0f, 5.0f);  // 通用网口
        FillRounded(g, jack, 1.0f, kPortDark);
        StrokeRounded(g, jack, 1.0f, 0xffffff, 1.0f, 56);
    }
}

void DrawLeds(Gdiplus::Graphics& g, const Device& d, float x0, float centerY) {
    const wchar_t* leds = rack::CatLeds(d);
    if (leds == nullptr) return;
    for (int i = 0; leds[i] != 0; ++i) {
        const bool amber = (leds[i] == L'a');
        Gdiplus::SolidBrush brush(Col(amber ? kLedAmber : kLedGreen));
        const float cx = x0 + static_cast<float>(i) * 5.0f;
        g.FillEllipse(&brush, cx - 1.6f, centerY - 1.6f, 3.2f, 3.2f);
    }
}

void DrawCategoryArt(Gdiplus::Graphics& g, const Device& d, const Gdiplus::RectF& box) {
    const float topY = box.Y + 6.0f;

    switch (d.cat) {
        case rack::Category::Router:
            DrawLeds(g, d, box.X + 6.0f, topY);
            DrawRepeatPorts(g, d, box);
            break;
        case rack::Category::Switch:
            DrawLeds(g, d, box.X + 6.0f, topY);
            DrawRepeatPorts(g, d, box);
            break;
        case rack::Category::Fw:
            DrawLeds(g, d, box.X + 6.0f, topY);
            DrawRepeatPorts(g, d, box);
            break;
        case rack::Category::Monitor:   // 监控录像机画硬盘位、监控交换机画网口
            DrawLeds(g, d, box.X + 6.0f, topY);
            DrawRepeatPorts(g, d, box);
            break;
        case rack::Category::Wireless: {
            for (int i = 0; i < 2; ++i) {                 // 两根天线（向上的三角）
                const float ax = box.X + 7.0f + static_cast<float>(i) * 9.0f;
                Gdiplus::PointF points[3] = {
                    Gdiplus::PointF(ax, topY - 3.0f),
                    Gdiplus::PointF(ax - 3.0f, topY + 3.0f),
                    Gdiplus::PointF(ax + 3.0f, topY + 3.0f),
                };
                Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(217), 255, 255, 255));
                g.FillPolygon(&brush, points, 3);
            }
            DrawLeds(g, d, box.GetRight() - 16.0f, topY);
            DrawRepeatPorts(g, d, box);
            break;
        }
        case rack::Category::Small: {
            const float cy = box.Y + box.Height / 2.0f - 3.0f;
            if (rack::IsHdmi(d)) {
                Gdiplus::RectF jack(box.X + box.Width / 2.0f - 5.0f, cy - 2.5f, 10.0f, 5.0f);
                FillRounded(g, jack, 1.0f, kPortDark);
                StrokeRounded(g, jack, 1.0f, 0xffffff, 1.0f, 115);
            } else {                                       // SC 光口 ×2
                for (int i = 0; i < 2; ++i) {
                    const float cx = box.X + box.Width / 2.0f - 5.0f + static_cast<float>(i) * 10.0f;
                    Gdiplus::SolidBrush fill(Col(kScWhite));
                    g.FillEllipse(&fill, cx - 3.2f, cy - 3.2f, 6.4f, 6.4f);
                    Gdiplus::Pen ring(Col(kScRing), 1.6f);
                    g.DrawEllipse(&ring, cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
                }
            }
            break;
        }
        case rack::Category::Accessory: {
            if (rack::IsCableMgr(d) || rack::IsPatchPanel(d)) {
                DrawRepeatPorts(g, d, box);
            } else {                                       // 空位挡板：中央螺丝孔 + 淡名
                Gdiplus::RectF hole(box.X + box.Width / 2.0f - 8.0f, box.Y + box.Height / 2.0f - 1.5f,
                                    16.0f, 3.0f);
                FillRounded(g, hole, 1.5f, 0x000000, 64);
                TextCrisp(g, L"空位挡板", 8.0f, false, CrispAlign::Center, box.X + box.Width / 2.0f,
                          box.Y + box.Height / 2.0f - 9.0f, 0xffffff, 0.0f, 191);
            }
            break;
        }
        default: {
            // 自定义设备：右上角高度徽标 + 居中名称（名称由 DrawDevicePanel 统一绘制）
            Gdiplus::RectF badge(box.GetRight() - 24.0f, box.Y + 3.0f, 21.0f, 11.0f);
            FillRounded(g, badge, 2.0f, 0x000000, 102);
            TextCrisp(g, std::to_wstring(d.hU) + L"U", 8.0f, false, CrispAlign::Center,
                      badge.X + badge.Width / 2.0f, badge.Y + badge.Height / 2.0f, 0xffffff);
            break;
        }
    }
}

}  // namespace

float NameSizeForExport(float height) {
    return std::min(9.5f, std::max(7.0f, height * 0.34f));
}

RackMetrics ScreenMetrics(const State& s) {
    RackMetrics m;
    m.capH = static_cast<float>(rack::kCapH);
    m.labelW = static_cast<float>(rack::kLabelW);
    m.railW = static_cast<float>(rack::kRailW);
    m.innerW = static_cast<float>(rack::kInnerW);
    m.bodyH = static_cast<float>(rack::BodyH(s.rack));
    m.innerH = m.bodyH + rack::kPadTop + rack::kPadBottom;
    m.frameW = rack::kFramePadX * 2 + m.labelW * 2 + m.railW * 2 + m.innerW;
    m.frameH = m.capH * 2 + m.innerH;
    m.exportStyle = false;
    return m;
}

RackMetrics ExportMetrics(const State& s) {
    RackMetrics m;
    m.labelW = 34.0f;
    m.railW = 18.0f;
    m.innerW = static_cast<float>(rack::kInnerW);
    m.bodyH = static_cast<float>(rack::BodyH(s.rack));
    m.innerH = m.bodyH + rack::kPadTop + rack::kPadBottom;
    m.headH = 26.0f;
    m.plaqueH = 22.0f;
    m.frameW = m.labelW * 2 + m.railW * 2 + m.innerW;
    m.frameH = m.headH + m.innerH;
    m.exportStyle = true;
    return m;
}

Gdiplus::RectF FrameRectOf(const RackMetrics& m, const Gdiplus::PointF& origin, float scale) {
    return Gdiplus::RectF(origin.X, origin.Y, m.frameW * scale, m.frameH * scale);
}

rack::InnerRect InnerRectOf(const State&, const RackMetrics& m, const Gdiplus::PointF& origin,
                            float scale) {
    rack::InnerRect r;
    const float dx = m.exportStyle ? (m.labelW + m.railW) : (rack::kFramePadX + m.labelW + m.railW);
    const float dy = m.exportStyle ? m.headH : m.capH;
    r.left = origin.X + dx * scale;
    r.top = origin.Y + dy * scale;
    r.width = m.innerW * scale;
    r.height = m.innerH * scale;
    r.scale = scale;
    return r;
}

Gdiplus::PointF InnerOrigin(const RackMetrics& m) {
    const float x = m.exportStyle ? (m.labelW + m.railW) : (rack::kFramePadX + m.labelW + m.railW);
    const float y = m.exportStyle ? m.headH : m.capH;
    return Gdiplus::PointF(x, y);
}

Gdiplus::RectF DeviceRect(const Device& d) {
    return Gdiplus::RectF(static_cast<float>(rack::kPadX) + d.col * ColW(),
                          static_cast<float>(rack::kPadTop) + d.hole * SlotH(),
                          d.w * ColW(), d.hU * rack::kUPx);
}

Gdiplus::RectF DeviceRectIn(const RackMetrics& m, const Device& d) {
    const Gdiplus::PointF origin = InnerOrigin(m);
    const Gdiplus::RectF local = DeviceRect(d);
    return Gdiplus::RectF(origin.X + local.X, origin.Y + local.Y, local.Width, local.Height);
}

void DrawDevicePanel(Gdiplus::Graphics& g, const Device& d, const Gdiplus::RectF& box,
                     float nameSize) {
    const rack::CatTheme theme = rack::CatThemeOf(d.cat);

    // 面板本体：纵向三色渐变 + 圆角 + 高光/暗边
    FillRoundedVertical(g, box, 2.0f, theme.c0, theme.c2, 0.55f, theme.c1);
    StrokeLine(g, box.X + 1.0f, box.Y + 0.5f, box.GetRight() - 1.0f, box.Y + 0.5f, 0xffffff,
               1.0f, 82);
    StrokeLine(g, box.X + 1.0f, box.GetBottom() - 0.5f, box.GetRight() - 1.0f,
               box.GetBottom() - 0.5f, 0x000000, 1.0f, 115);
    StrokeRounded(g, Gdiplus::RectF(box.X + 0.5f, box.Y + 0.5f, box.Width - 1.0f, box.Height - 1.0f),
                  2.0f, kDeviceEdge, 1.0f, 102);

    // 面板内的元素与文字都裁剪在圆角内
    Gdiplus::GraphicsPath clip;
    AddRoundedRect(&clip, box, 2.0f);
    const Gdiplus::GraphicsState state = g.Save();
    g.SetClip(&clip);

    DrawCategoryArt(g, d, box);

    // 名称：自定义设备居中（粗体），其余贴底
    const bool blank = rack::IsBlankPanel(d);
    if (!blank) {
        if (d.cat == rack::Category::Custom) {
            const float size = nameSize > 0.0f ? std::min(12.0f, nameSize + 0.5f)
                                               : std::min(12.0f, std::max(7.0f, box.Height * 0.24f));
            TextCrisp(g, d.name, size, true, CrispAlign::Center, box.X + box.Width / 2.0f,
                      box.Y + box.Height / 2.0f, 0xffffff, box.Width - 30.0f, 242);
        } else {
            const float size = nameSize > 0.0f ? nameSize : NameSizeForExport(box.Height);
            const std::wstring label = Ellipsize(g, d.name, Font(size), box.Width - 10.0f);
            // 文字阴影（HTML 对照版为 text-shadow 0 1px 1px rgba(0,0,0,.7)）
            TextCrisp(g, label, size, false, CrispAlign::Left, box.X + 5.0f,
                      box.Y + box.Height - 5.5f + 1.0f, 0x000000, 0.0f, 178);
            TextCrisp(g, label, size, false, CrispAlign::Left, box.X + 5.0f,
                      box.Y + box.Height - 5.5f, 0xffffff, 0.0f, 242);
        }
    }

    // 多 U 设备：每 U 画一条分隔线
    if (d.hU > 1) {
        for (int i = 1; i < d.hU; ++i) {
            const float y = std::floor(box.Y + i * rack::kUPx) + 0.5f;
            StrokeLine(g, box.X, y, box.GetRight(), y, 0x000000, 1.0f, 71);
        }
    }

    g.Restore(state);
}

Gdiplus::RectF CloseButtonRect(const Gdiplus::RectF& deviceBox) {
    return Gdiplus::RectF(deviceBox.GetRight() - 15.0f, deviceBox.Y + 1.0f, 14.0f, 14.0f);
}

void DrawCloseButton(Gdiplus::Graphics& g, const Gdiplus::RectF& deviceBox) {
    const Gdiplus::RectF button = CloseButtonRect(deviceBox);
    Gdiplus::SolidBrush brush(Col(0xe53e3e));
    g.FillEllipse(&brush, button);
    Gdiplus::Pen pen(Col(0xffffff), 1.4f);
    const float pad = 4.5f;
    g.DrawLine(&pen, button.X + pad, button.Y + pad, button.GetRight() - pad, button.GetBottom() - pad);
    g.DrawLine(&pen, button.GetRight() - pad, button.Y + pad, button.X + pad, button.GetBottom() - pad);
}

namespace {

/* 以指定不透明度绘制一台设备（拖动中的「抬起」态与示意外形都用它） */
void DrawDeviceWithAlpha(Gdiplus::Graphics& g, const Device& d, const Gdiplus::RectF& box,
                         float nameSize, float alpha) {
    if (alpha >= 1.0f) {
        DrawDevicePanel(g, d, box, nameSize);
        return;
    }
    const int pad = 3;
    const int width = std::max(1, static_cast<int>(box.Width) + pad * 2);
    const int height = std::max(1, static_cast<int>(box.Height) + pad * 2);
    Gdiplus::Bitmap layer(width, height, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics lg(&layer);
        lg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        lg.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        lg.TranslateTransform(static_cast<float>(pad) - box.X, static_cast<float>(pad) - box.Y);
        DrawDevicePanel(lg, d, box, nameSize);
    }
    Gdiplus::ImageAttributes attributes;
    Gdiplus::ColorMatrix matrix = {};
    matrix.m[0][0] = 1.0f;
    matrix.m[1][1] = 1.0f;
    matrix.m[2][2] = 1.0f;
    matrix.m[3][3] = alpha;
    matrix.m[4][4] = 1.0f;
    attributes.SetColorMatrix(&matrix);
    g.DrawImage(&layer,
                Gdiplus::RectF(box.X - pad, box.Y - pad, box.Width + pad * 2.0f,
                               box.Height + pad * 2.0f),
                0.0f, 0.0f, static_cast<Gdiplus::REAL>(width), static_cast<Gdiplus::REAL>(height),
                Gdiplus::UnitPixel, &attributes);
}

}  // namespace

void DrawRack(Gdiplus::Graphics& g, const State& s, const RackMetrics& m, int selectedId,
              float nameSizeForScreen, int dimId) {
    const int n = rack::RackU(s.rack);
    const int holes = rack::TotalHoles(s.rack);
    const Gdiplus::RectF frame(0.0f, 0.0f, m.frameW, m.frameH);

    if (!m.exportStyle) FloorShadow(g, frame);

    // 金属外框
    const float frameStops[5] = {0.0f, 0.08f, 0.5f, 0.92f, 1.0f};
    const uint32_t frameColors[5] = {kRackEdge, kRackEdge2, kRackMid, kRackEdge2, kRackEdge};
    if (m.exportStyle) {
        Gdiplus::GraphicsPath path;
        AddRoundedRect(&path, frame, 10.0f);
        Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(frame.X, frame.Y),
                                           Gdiplus::PointF(frame.GetRight(), frame.Y),
                                           Col(kRackEdge), Col(kRackEdge));
        Gdiplus::Color colors[5];
        Gdiplus::REAL positions[5];
        for (int i = 0; i < 5; ++i) {
            colors[i] = Col(frameColors[i]);
            positions[i] = frameStops[i];
        }
        brush.SetInterpolationColors(colors, positions, 5);
        g.FillPath(&brush, &path);
        StrokeRounded(g, Gdiplus::RectF(0.5f, 0.5f, m.frameW - 1.0f, m.frameH - 1.0f), 10.0f,
                      0xffffff, 1.0f, 230);
        // 头部
        FillRounded(g, Gdiplus::RectF(1.0f, 1.0f, m.frameW - 2.0f, m.headH - 2.0f), 9.0f, 0xdfe5ec);
    } else {
        // 上盖
        FillRoundedVertical(g, Gdiplus::RectF(0.0f, 0.0f, m.frameW, m.capH), 10.0f, kCapTopA, kCapTopB);
        // 机身（金属渐变，方形侧面）
        FillSolid(g, Gdiplus::RectF(0.0f, m.capH, m.frameW, m.innerH), kRackMid);
        {
            Gdiplus::RectF body(0.0f, m.capH, m.frameW, m.innerH);
            FillHorizontalStops(g, body, frameStops, frameColors, 5);
        }
        StrokeRounded(g, Gdiplus::RectF(0.5f, 0.5f, m.frameW - 1.0f, m.frameH - 1.0f), 10.0f,
                      0xffffff, 1.0f, 153);
        // 下盖 + 铭牌
        FillRoundedVertical(g, Gdiplus::RectF(0.0f, m.capH + m.innerH, m.frameW, m.capH), 10.0f,
                            kCapTopB, kCapBotB);
        TextCrisp(g, std::wstring(L"19\" 标准机柜 · ") + rack::RackText(s.rack), 10.0f, false,
                  CrispAlign::Center, m.frameW / 2.0f, m.capH + m.innerH + m.capH / 2.0f, kTextCtl);
    }

    if (m.exportStyle) {
        // 底部铭牌带（画在外框下沿之外，与 HTML 对照版导出图一致）
        const Gdiplus::RectF plaque(1.0f, m.frameH + 1.0f, m.frameW - 2.0f, m.plaqueH);
        FillRounded(g, plaque, 8.0f, kCapTopB);
        TextCrisp(g, std::wstring(L"19\" 标准机柜 · ") + rack::RackText(s.rack), 11.0f, false,
                  CrispAlign::Center, m.frameW / 2.0f, plaque.Y + plaque.Height / 2.0f, kTextCtl);
    }

    const float innerX = m.exportStyle ? (m.labelW + m.railW) : (rack::kFramePadX + m.labelW + m.railW);
    const float innerY = m.exportStyle ? m.headH : m.capH;
    const Gdiplus::RectF inner(innerX, innerY, m.innerW, m.innerH);

    // 内框底
    FillVertical(g, inner, kInnerA, kInnerC, 0.5f, kInnerB);

    // 条纹（含上下留白），与网格、设备共用 PAD_TOP 基准
    for (int u = n; u >= 1; --u) {
        const float y = inner.Y + rack::kPadTop + (n - u) * static_cast<float>(rack::kUPx);
        const bool dark = (u % 2) == 0;
        Gdiplus::RectF band(inner.X, y, inner.Width, static_cast<float>(rack::kUPx));
        if (dark) {
            FillSolid(g, band, kBandDark, 43);      // .17
        } else {
            FillSolid(g, band, kBandLight, 77);     // .30
        }
    }
    FillSolid(g, Gdiplus::RectF(inner.X, inner.Y, inner.Width, rack::kPadTop), kBandPad, 33);
    FillSolid(g, Gdiplus::RectF(inner.X, inner.Y + rack::kPadTop + m.bodyH, inner.Width,
                                rack::kPadBottom), kBandPad, 33);

    // 内凹阴影与描边
    InnerShadow(g, inner);
    StrokeRounded(g, Gdiplus::RectF(inner.X + 0.5f, inner.Y + 0.5f, inner.Width - 1.0f,
                                    inner.Height - 1.0f), 0.0f, 0x96a5b9, 1.0f, 128);

    // 网格线
    for (int i = 0; i <= holes; ++i) {
        const bool strong = (i % rack::kHolesPerU) == 0;
        if (s.mode == rack::SnapMode::U && !strong) continue;
        const float y = std::floor(inner.Y + rack::kPadTop + i * SlotH()) + 0.5f;
        StrokeLine(g, inner.X, y, inner.GetRight(), y, kGridRef, 1.0f, strong ? 71 : 33);
    }

    // 立柱（含螺丝孔）
    auto drawRail = [&](float x) {
        const Gdiplus::RectF rail(x, inner.Y + rack::kPadTop, m.railW, m.bodyH);
        const float railStops[3] = {0.0f, 0.5f, 1.0f};
        const uint32_t railColors[3] = {kRailA, 0xffffff, kRailA};
        FillHorizontalStops(g, rail, railStops, railColors, 3);
        Gdiplus::SolidBrush screw(Col(kScrew));
        for (int hh = 0; hh < holes; ++hh) {
            const float cy = rail.Y + hh * SlotH() + SlotH() / 2.0f;
            g.FillEllipse(&screw, x + m.railW / 2.0f - 2.0f, cy - 2.0f, 4.0f, 4.0f);
        }
    };
    drawRail(inner.X - m.railW);
    drawRail(inner.GetRight());

    // U 编号（左列右对齐、右列左对齐，竖直居中于每条 U 带）
    for (int u = n; u >= 1; --u) {
        const float centerY = inner.Y + rack::kPadTop + (n - u) * static_cast<float>(rack::kUPx) +
                              rack::kUPx / 2.0f;
        const std::wstring text = std::to_wstring(u);
        const float size = m.exportStyle ? 9.0f : 10.0f;
        const uint32_t color = m.exportStyle ? kLabelText : kTextDim;
        TextCrisp(g, text, size, false, CrispAlign::Right,
                  inner.X - m.railW - 2.0f, centerY, color);
        TextCrisp(g, text, size, false, CrispAlign::Left,
                  inner.GetRight() + m.railW + 4.0f, centerY, color);
    }

    // 设备（选中态最后画，保证描边在最上层；拖动中的设备画成抬起态）
    const Device* selected = nullptr;
    for (const Device& d : s.devices) {
        if (d.id == selectedId) {
            selected = &d;
            continue;
        }
        const Gdiplus::RectF box = DeviceRectIn(m, d);
        const float nameSize = m.exportStyle ? 0.0f : nameSizeForScreen;
        if (dimId != 0 && d.id == dimId) {
            DrawDeviceWithAlpha(g, d, box, nameSize, 0.25f);
        } else {
            DrawDevicePanel(g, d, box, nameSize);
        }
    }
    if (selected != nullptr) {
        const Gdiplus::RectF box = DeviceRectIn(m, *selected);
        DrawDevicePanel(g, *selected, box, m.exportStyle ? 0.0f : nameSizeForScreen);
        StrokeRounded(g, Gdiplus::RectF(box.X - 1.0f, box.Y - 1.0f, box.Width + 2.0f, box.Height + 2.0f),
                      2.0f, kSelGold, 2.0f);
        if (!m.exportStyle) DrawCloseButton(g, box);
    }
}

int HitDevice(const State& s, const RackMetrics& m, const Gdiplus::PointF& frameOrigin, float scale,
              const Gdiplus::PointF& pt) {
    for (auto it = s.devices.rbegin(); it != s.devices.rend(); ++it) {
        const Gdiplus::RectF box = DeviceRectIn(m, *it);
        const Gdiplus::RectF screen(frameOrigin.X + box.X * scale, frameOrigin.Y + box.Y * scale,
                                    box.Width * scale, box.Height * scale);
        if (pt.X >= screen.X && pt.X <= screen.GetRight() && pt.Y >= screen.Y &&
            pt.Y <= screen.GetBottom()) {
            return it->id;
        }
    }
    return 0;
}

bool HitCloseButton(const State& s, const RackMetrics& m, const Gdiplus::PointF& frameOrigin,
                    float scale, const Gdiplus::PointF& pt) {
    if (s.selectedId == 0) return false;
    for (const Device& d : s.devices) {
        if (d.id != s.selectedId) continue;
        const Gdiplus::RectF box = DeviceRectIn(m, d);
        const Gdiplus::RectF button = CloseButtonRect(box);
        const Gdiplus::RectF screen(frameOrigin.X + button.X * scale,
                                    frameOrigin.Y + button.Y * scale, button.Width * scale,
                                    button.Height * scale);
        const float slack = 2.0f * scale;
        return pt.X >= screen.X - slack && pt.X <= screen.GetRight() + slack &&
               pt.Y >= screen.Y - slack && pt.Y <= screen.GetBottom() + slack;
    }
    return false;
}

}  // namespace ui
