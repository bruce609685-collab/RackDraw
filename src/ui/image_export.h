#pragma once

// 布局图导出（F-09）：离屏重绘 + WIC 编码。
// 画布布局常量与 HTML 对照版 buildRackCanvas 一致（padX 30 / padY 24 / capH 44 / headH 26 /
// labelW 34 / railW 18 / footH 34），2 倍高清，超 4000 万像素自动降倍。

#include <windows.h>
#include <gdiplus.h>

#include <string>

#include "core/types.h"

namespace ui {

struct CanvasBox {
    float padX = 30.0f;
    float padY = 24.0f;
    float capH = 44.0f;
    float headH = 26.0f;
    float labelW = 34.0f;
    float railW = 18.0f;
    float footH = 34.0f;
    float totalW = 0.0f;
    float totalH = 0.0f;
    float scale = 2.0f;   // 实际使用的倍率（可能因像素上限而降倍）
};

CanvasBox ComputeCanvasBox(const rack::State& s);

// 渲染整张布局图（调用方负责 delete）
Gdiplus::Bitmap* RenderLayoutBitmap(const rack::State& s);

bool SaveBitmapAsPng(const std::wstring& path, Gdiplus::Bitmap* bitmap);
bool SaveBitmapAsJpeg(const std::wstring& path, Gdiplus::Bitmap* bitmap, float quality);

}  // namespace ui
