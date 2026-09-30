#include "ui/theme.h"

#include <map>

#include "app/dpi_aware.h"

namespace ui {
namespace {

Gdiplus::FontFamily* Family() {
    static Gdiplus::FontFamily* family = nullptr;
    if (family == nullptr) {
        // 微软雅黑（Win7 起随系统提供简体中文版），失败则退回默认宋体族
        family = new Gdiplus::FontFamily(L"Microsoft YaHei");
        if (!family->IsAvailable()) {
            delete family;
            family = new Gdiplus::FontFamily(L"SimSun");
        }
    }
    return family;
}

}  // namespace

Gdiplus::Font* Font(float pixelSize, bool bold) {
    static std::map<int, Gdiplus::Font*> cache;
    const int key = static_cast<int>(pixelSize * 4.0f) * 2 + (bold ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    Gdiplus::Font* font = new Gdiplus::Font(
        Family(), pixelSize,
        bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);
    cache[key] = font;
    return font;
}

int Scale(int value) {
    return static_cast<int>(value) * app::WindowDpi() / 96;
}

float ScaleF(float value) {
    return value * app::WindowDpi() / 96.0f;
}

void AddRoundedRect(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& r, float radius) {
    const float rr = radius < r.Width / 2 ? (radius < r.Height / 2 ? radius : r.Height / 2)
                                          : r.Width / 2;
    path->Reset();
    if (rr <= 0.01f) {
        path->AddRectangle(r);
        return;
    }
    const float d = rr * 2;
    path->AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path->AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path->AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path->AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
}

void FillSolid(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint32_t color, int alpha) {
    Gdiplus::SolidBrush brush(Col(color, alpha));
    g.FillRectangle(&brush, r);
}

void FillRounded(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, uint32_t color,
                 int alpha) {
    Gdiplus::GraphicsPath path;
    AddRoundedRect(&path, r, radius);
    Gdiplus::SolidBrush brush(Col(color, alpha));
    g.FillPath(&brush, &path);
}

void FillVertical(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint32_t top, uint32_t bottom,
                  float midStop, uint32_t midColor) {
    if (r.Height <= 0.0f) return;
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::PointF(r.X, r.Y), Gdiplus::PointF(r.X, r.GetBottom()),
        Col(top), Col(bottom));
    if (midStop >= 0.0f) {
        Gdiplus::Color stops[3] = {Col(top), (midColor == 0) ? Col((top + bottom) / 2)
                                                            : Col(midColor),
                                   Col(bottom)};
        Gdiplus::REAL positions[3] = {0.0f, midStop, 1.0f};
        brush.SetInterpolationColors(stops, positions, 3);
    }
    g.FillRectangle(&brush, r);
}

void FillRoundedVertical(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                         uint32_t top, uint32_t bottom, float midStop, uint32_t midColor) {
    Gdiplus::GraphicsPath path;
    AddRoundedRect(&path, r, radius);
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::PointF(r.X, r.Y), Gdiplus::PointF(r.X, r.GetBottom()),
        Col(top), Col(bottom));
    if (midStop >= 0.0f) {
        Gdiplus::Color stops[3] = {Col(top), (midColor == 0) ? Col((top + bottom) / 2)
                                                             : Col(midColor),
                                   Col(bottom)};
        Gdiplus::REAL positions[3] = {0.0f, midStop, 1.0f};
        brush.SetInterpolationColors(stops, positions, 3);
    }
    g.FillPath(&brush, &path);
}

void FillHorizontalStops(Gdiplus::Graphics& g, const Gdiplus::RectF& r,
                         const float* stops, const uint32_t* colors, int count) {
    if (count < 2) return;
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::PointF(r.X, r.Y), Gdiplus::PointF(r.GetRight(), r.Y),
        Col(colors[0]), Col(colors[count - 1]));
    Gdiplus::Color* cols = new Gdiplus::Color[static_cast<size_t>(count)];
    Gdiplus::REAL* pos = new Gdiplus::REAL[static_cast<size_t>(count)];
    for (int i = 0; i < count; ++i) {
        cols[i] = Col(colors[i]);
        pos[i] = stops[i];
    }
    brush.SetInterpolationColors(cols, pos, count);
    g.FillRectangle(&brush, r);
    delete[] cols;
    delete[] pos;
}

void StrokeRounded(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, uint32_t color,
                   float width, int alpha) {
    Gdiplus::GraphicsPath path;
    AddRoundedRect(&path, r, radius);
    Gdiplus::Pen pen(Col(color, alpha), width);
    g.DrawPath(&pen, &path);
}

void StrokeLine(Gdiplus::Graphics& g, float x1, float y1, float x2, float y2, uint32_t color,
                float width, int alpha) {
    Gdiplus::Pen pen(Col(color, alpha), width);
    g.DrawLine(&pen, x1, y1, x2, y2);
}

float TextWidth(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font) {
    if (text.empty()) return 0.0f;
    Gdiplus::RectF bounds(0.0f, 0.0f, 4000.0f, 200.0f);
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                          Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    g.MeasureString(text.c_str(), static_cast<INT>(text.size()), font, bounds, &format, &bounds);
    return bounds.Width;
}

std::wstring Ellipsize(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                       float maxWidth) {
    if (maxWidth <= 0.0f || text.empty()) return text;
    if (TextWidth(g, text, font) <= maxWidth) return text;
    std::wstring out = text;
    while (!out.empty()) {
        out.pop_back();
        const std::wstring attempt = out + L"…";
        if (TextWidth(g, attempt, font) <= maxWidth) return attempt;
    }
    return L"…";
}

namespace {

void TextAligned(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                 const Gdiplus::RectF& box, uint32_t color, int alpha,
                 Gdiplus::StringAlignment align) {
    if (text.empty()) return;
    Gdiplus::SolidBrush brush(Col(color, alpha));
    Gdiplus::StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                          Gdiplus::StringFormatFlagsNoClip);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), font, box, &format, &brush);
}

}  // namespace

void TextLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
              const Gdiplus::RectF& box, uint32_t color, int alpha) {
    TextAligned(g, text, font, box, color, alpha, Gdiplus::StringAlignmentNear);
}

void TextCenter(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                const Gdiplus::RectF& box, uint32_t color, int alpha) {
    TextAligned(g, text, font, box, color, alpha, Gdiplus::StringAlignmentCenter);
}

void TextRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
               const Gdiplus::RectF& box, uint32_t color, int alpha) {
    TextAligned(g, text, font, box, color, alpha, Gdiplus::StringAlignmentFar);
}

void TextMiddleLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                    float x, float centerY, uint32_t color, float maxWidth, int alpha) {
    const float height = font->GetHeight(&g);
    const float width = maxWidth > 0.0f ? maxWidth : 4000.0f;
    Gdiplus::RectF box(x, centerY - height / 2.0f, width, height);
    TextAligned(g, maxWidth > 0.0f ? Ellipsize(g, text, font, maxWidth) : text,
                font, box, color, alpha, Gdiplus::StringAlignmentNear);
}

void TextMiddleCenter(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                      float centerX, float centerY, uint32_t color, float maxWidth, int alpha) {
    const float height = font->GetHeight(&g);
    const float width = maxWidth > 0.0f ? maxWidth : 4000.0f;
    Gdiplus::RectF box(centerX - width / 2.0f, centerY - height / 2.0f, width, height);
    TextAligned(g, maxWidth > 0.0f ? Ellipsize(g, text, font, maxWidth) : text,
                font, box, color, alpha, Gdiplus::StringAlignmentCenter);
}

void TextMiddleRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font* font,
                     float rightX, float centerY, uint32_t color, float maxWidth, int alpha) {
    const float height = font->GetHeight(&g);
    const float width = maxWidth > 0.0f ? maxWidth : 4000.0f;
    Gdiplus::RectF box(rightX - width, centerY - height / 2.0f, width, height);
    TextAligned(g, maxWidth > 0.0f ? Ellipsize(g, text, font, maxWidth) : text,
                font, box, color, alpha, Gdiplus::StringAlignmentFar);
}

void TextCrisp(Gdiplus::Graphics& g, const std::wstring& text, float logicalSize, bool bold,
               CrispAlign align, float anchorX, float centerY, uint32_t color,
               float maxWidthLogical, int alpha) {
    Gdiplus::Matrix transform;
    if (g.GetTransform(&transform) != Gdiplus::Ok) {
        TextMiddleLeft(g, text, Font(logicalSize, bold), anchorX, centerY, color, maxWidthLogical,
                       alpha);
        return;
    }
    Gdiplus::REAL e[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    transform.GetElements(e);
    const float scaleX = e[0];
    const float scaleY = e[3];
    if (scaleX <= 0.0001f || scaleY <= 0.0001f) return;

    const float deviceX = anchorX * scaleX + e[4];
    const float deviceY = centerY * scaleY + e[5];
    const float deviceMax = maxWidthLogical > 0.0f ? maxWidthLogical * scaleX : 0.0f;
    const float deviceSize = logicalSize * scaleX;

    const Gdiplus::GraphicsState state = g.Save();
    g.ResetTransform();
    Gdiplus::Font* font = Font(deviceSize, bold);
    switch (align) {
        case CrispAlign::Left:
            TextMiddleLeft(g, text, font, deviceX, deviceY, color, deviceMax, alpha);
            break;
        case CrispAlign::Center:
            TextMiddleCenter(g, text, font, deviceX, deviceY, color, deviceMax, alpha);
            break;
        case CrispAlign::Right:
            TextMiddleRight(g, text, font, deviceX, deviceY, color, deviceMax, alpha);
            break;
    }
    g.Restore(state);
}

}  // namespace ui
