#pragma once

namespace app {

// 声明进程 DPI 感知：v1 API（SetProcessDPIAware）在 Win7 上必然可用，
// 不使用 Windows 8.1 及以上才有的 per-monitor API。
// 界面自行按 96 DPI 设计尺寸换算，见 ui/layout 里的 Scale()。
void EnableDpiAwareness();

// 当前窗口 DPI（96 起步），供绘制与命中测试统一换算。
int WindowDpi();

}  // namespace app
