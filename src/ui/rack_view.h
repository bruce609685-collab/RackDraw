#pragma once

// 机柜与设备的自绘（GDI+）。
// 界面与导出图共用这里的画法：DrawRack 在「机柜外框左上角为原点」的局部坐标系里作画，
// 调用方用 Graphics 的 Translate/Scale 变换决定位置与缩放（对应 HTML 对照版的 CSS transform
// 与 Canvas 的 ctx.scale + 偏移）。

#include <windows.h>
#include <gdiplus.h>

#include "core/types.h"

namespace ui {

struct RackMetrics {
    float capH = 20.0f;       // 上/下盖高（界面 20；导出图为头部 26 + 铭牌带 22）
    float labelW = 26.0f;     // U 编号栏宽（界面 26；导出图 34）
    float railW = 16.0f;      // 立柱宽（界面 16；导出图 18）
    float innerW = 312.0f;
    float innerH = 0.0f;
    float bodyH = 0.0f;
    float frameW = 0.0f;
    float frameH = 0.0f;
    float headH = 0.0f;       // 导出图专用：头部高度
    float plaqueH = 0.0f;     // 导出图专用：底部铭牌带高度
    bool exportStyle = false;
};

RackMetrics ScreenMetrics(const rack::State& s);
RackMetrics ExportMetrics(const rack::State& s);

// 机柜外框在屏幕上的矩形（frameOrigin + scale 决定）
Gdiplus::RectF FrameRectOf(const RackMetrics& m, const Gdiplus::PointF& frameOrigin, float scale);
// 机柜内框在屏幕上的位置与缩放（吸附计算用，对应 HTML 对照版 getBoundingClientRect）
rack::InnerRect InnerRectOf(const rack::State& s, const RackMetrics& m,
                            const Gdiplus::PointF& frameOrigin, float scale);

// 绘制整座机柜（含 U 编号、立柱、内框、条纹、网格、全部设备）
// nameSizeForScreen：界面上的设备名字号（逻辑单位，通常 9.5）；导出图传 9.5 以外的自适应值可传 0
// dimId：拖动中的设备 id（画成半透明「抬起」态）；0 表示没有
void DrawRack(Gdiplus::Graphics& g, const rack::State& s, const RackMetrics& m, int selectedId,
              float nameSizeForScreen, int dimId = 0);

// 设备在内框里的矩形（局部单位，相对内框左上角）
Gdiplus::RectF DeviceRect(const rack::Device& d);

// 设备在机柜外框坐标系里的矩形（= 内框原点偏移 + DeviceRect），绘制与命中测试都用它
Gdiplus::RectF DeviceRectIn(const RackMetrics& m, const rack::Device& d);
// 内框左上角在外框坐标系里的位置
Gdiplus::PointF InnerOrigin(const RackMetrics& m);

// 单台设备面板（局部单位；nameSize <= 0 时用导出图的自适应字号）
void DrawDevicePanel(Gdiplus::Graphics& g, const rack::Device& d, const Gdiplus::RectF& box,
                     float nameSize);

// 选中设备的删除按钮（界面专用；局部单位）
Gdiplus::RectF CloseButtonRect(const Gdiplus::RectF& deviceBox);
void DrawCloseButton(Gdiplus::Graphics& g, const Gdiplus::RectF& deviceBox);

// 命中测试（frameOrigin/scale 与绘制时一致）
int HitDevice(const rack::State& s, const RackMetrics& m, const Gdiplus::PointF& frameOrigin,
              float scale, const Gdiplus::PointF& pt);
bool HitCloseButton(const rack::State& s, const RackMetrics& m, const Gdiplus::PointF& frameOrigin,
                    float scale, const Gdiplus::PointF& pt);

}  // namespace ui
