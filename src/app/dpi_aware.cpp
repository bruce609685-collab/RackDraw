#include "app/dpi_aware.h"

#include <windows.h>

namespace app {
namespace {

int g_dpi = 96;

}  // namespace

void EnableDpiAwareness() {
    SetProcessDPIAware();

    // 取系统 DPI 作为全局缩放基准（Win7 上可用；失败则保持 96）
    HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
        const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        if (dpi >= 96 && dpi <= 480) g_dpi = dpi;
        ReleaseDC(nullptr, screen);
    }
}

int WindowDpi() { return g_dpi; }

}  // namespace app
