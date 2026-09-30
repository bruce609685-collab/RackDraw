// Bitmap::Lock/Unlock（取像素做 WIC 编码）需要 GDIPVER >= 0x0110，
// 必须在任何 gdiplus.h 之前定义
#define GDIPVER 0x0110

#include "ui/image_export.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "app/app_info.h"
#include "core/markdown.h"
#include "ui/rack_view.h"
#include "ui/theme.h"

namespace ui {
namespace {

// 像素上限降倍：与画布尺寸同源，改动其一不会让判断与实际脱节
void ApplyPixelLimit(CanvasBox* box) {
    const double px = static_cast<double>(box->totalW) * box->scale *
                      static_cast<double>(box->totalH) * box->scale;
    if (px > static_cast<double>(rack::kMaxCanvasPx)) {
        box->scale = static_cast<float>(
            box->scale * std::sqrt(static_cast<double>(rack::kMaxCanvasPx) / px));
    }
}

/* ---------------- WIC 编码 ---------------- */

bool EncodeWithWic(Gdiplus::Bitmap* bitmap, const std::wstring& path, const GUID& container,
                   bool useQuality, float quality) {
    if (bitmap == nullptr) return false;

    Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap->GetWidth()),
                       static_cast<INT>(bitmap->GetHeight()));
    Gdiplus::BitmapData data;
    // 注意：MinGW 的 gdiplus 头只提供 LockBits/UnlockBits（没有 Lock/Unlock 便捷方法）
    if (bitmap->LockBits(&rect, static_cast<UINT>(Gdiplus::ImageLockModeRead),
                         PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    const int width = rect.Width;
    const int height = rect.Height;
    const int outChannels = (container == GUID_ContainerFormatJpeg) ? 3 : 4;

    // 统一按自上而下的顺序打包（GDI+ 的 Stride 可能为负，表示自下而上）
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * outChannels);
    const BYTE* base = static_cast<const BYTE*>(data.Scan0);
    for (int y = 0; y < height; ++y) {
        const BYTE* row = base + static_cast<ptrdiff_t>(y) * data.Stride;
        BYTE* dst = pixels.data() + static_cast<size_t>(y) * width * outChannels;
        for (int x = 0; x < width; ++x) {
            const BYTE* src = row + x * 4;
            dst[x * outChannels + 0] = src[0];
            dst[x * outChannels + 1] = src[1];
            dst[x * outChannels + 2] = src[2];
            if (outChannels == 4) dst[x * outChannels + 3] = src[3];
        }
    }
    bitmap->UnlockBits(&data);

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }

    bool ok = false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;

    do {
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(container, nullptr, &encoder))) break;
        if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(encoder->CreateNewFrame(&frame, &props))) break;

        if (useQuality && props != nullptr) {
            PROPBAG2 option = {};
            option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_R4;
            value.fltVal = quality;
            props->Write(1, &option, &value);
        }
        if (FAILED(frame->Initialize(props))) break;
        if (FAILED(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) break;

        WICPixelFormatGUID format = (outChannels == 4) ? GUID_WICPixelFormat32bppBGRA
                                                       : GUID_WICPixelFormat24bppBGR;
        if (FAILED(frame->SetPixelFormat(&format))) break;
        const bool formatOk = (outChannels == 4)
                                  ? IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)
                                  : IsEqualGUID(format, GUID_WICPixelFormat24bppBGR);
        if (!formatOk) break;

        const UINT stride = static_cast<UINT>(width * outChannels);
        const UINT size = static_cast<UINT>(pixels.size());
        if (FAILED(frame->WritePixels(static_cast<UINT>(height), stride, size, pixels.data()))) {
            break;
        }
        if (FAILED(frame->Commit())) break;
        if (FAILED(encoder->Commit())) break;
        ok = true;
    } while (false);

    if (props != nullptr) props->Release();
    if (frame != nullptr) frame->Release();
    if (encoder != nullptr) encoder->Release();
    if (stream != nullptr) stream->Release();
    factory->Release();
    return ok;
}

}  // namespace

CanvasBox ComputeCanvasBox(const rack::State& s) {
    CanvasBox box;
    box.totalW = box.padX * 2 + box.labelW * 2 + box.railW * 2 + rack::kInnerW;
    box.totalH = box.padY * 2 + box.capH + box.headH + rack::InnerH(s.rack) + box.footH;
    box.scale = static_cast<float>(rack::kSaveScale);
    ApplyPixelLimit(&box);
    return box;
}

Gdiplus::Bitmap* RenderLayoutBitmap(const rack::State& s) {
    const CanvasBox box = ComputeCanvasBox(s);
    const int width = std::max(1, static_cast<int>(std::lround(box.totalW * box.scale)));
    const int height = std::max(1, static_cast<int>(std::lround(box.totalH * box.scale)));

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }

    Gdiplus::Graphics g(bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.ScaleTransform(box.scale, box.scale);

    // 页面底
    FillSolid(g, Gdiplus::RectF(0.0f, 0.0f, box.totalW, box.totalH), kExportPage);

    // 标题与副标题
    const float titleX = box.padX;
    const float maxTitleW = box.totalW - box.padX * 2;
    TextMiddleLeft(g, L"弱电机柜设备上架布局图", Font(19.0f, true), titleX, box.padY + 12.0f,
                   kExportTitle, maxTitleW);

    const int used = rack::UsedU(s);
    std::wstring subtitle = std::wstring(rack::RackText(s.rack)) + L" 标准机柜 · " +
                            (s.mode == rack::SnapMode::U ? L"标准 U 数版式" : L"螺丝孔位版式") +
                            L" · 已放置 " + std::to_wstring(s.devices.size()) + L" 台 · 高度合计 " +
                            std::to_wstring(used) + L"U / " + std::to_wstring(rack::RackU(s.rack)) +
                            L"U · " + rack::LocalStamp();
    float subSize = 12.0f;
    while (subSize > 8.0f && TextWidth(g, subtitle, Font(subSize)) > maxTitleW) {
        subSize -= 0.5f;
    }
    TextMiddleLeft(g, subtitle, Font(subSize), titleX, box.padY + 33.0f, kSubText, maxTitleW);

    // 机柜本体（导出图口径）
    const RackMetrics metrics = ExportMetrics(s);
    const Gdiplus::GraphicsState state = g.Save();
    g.TranslateTransform(box.padX, box.padY + box.capH);
    DrawRack(g, s, metrics, 0, 0.0f);
    g.Restore(state);

    // 页脚
    const std::wstring footer = std::wstring(L"由「") + APP_NAME_CN + L" " + APP_VERSION_TAG +
                                L"」生成 · 网格 1/6 标准宽 · 支持全宽 / 1/2 宽 / 1/3 宽混排";
    TextMiddleCenter(g, footer, Font(11.0f), box.totalW / 2.0f, box.totalH - box.padY - 6.0f,
                     kFooterText);
    return bitmap;
}

bool SaveBitmapAsPng(const std::wstring& path, Gdiplus::Bitmap* bitmap) {
    return EncodeWithWic(bitmap, path, GUID_ContainerFormatPng, false, 0.0f);
}

bool SaveBitmapAsJpeg(const std::wstring& path, Gdiplus::Bitmap* bitmap, float quality) {
    return EncodeWithWic(bitmap, path, GUID_ContainerFormatJpeg, true, quality);
}

}  // namespace ui
