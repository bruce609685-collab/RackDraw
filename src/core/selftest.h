#pragma once

// 命令行自检（不建窗口、不依赖界面）：
//   RackDraw.exe --selftest            跑核心逻辑断言，输出 PASS/FAIL 行，全部通过返回 0
//   RackDraw.exe --export-png <路径>   用示例场景渲染布局图（核对绘制与编码链路）
//   RackDraw.exe --export-md <路径>    用示例场景导出布设文件
//   RackDraw.exe --store-info          打印当前存档路径与是否便携
//   RackDraw.exe --demo                以示例场景启动图形界面（供截图/演示）

#include <string>

#include "core/types.h"

namespace rack {

// 示例场景：每个类别各一台 + 混排 + 多 U（与验证脚本的场景一致）
State MakeDemoState();

int RunSelfTest();
bool ExportDemoPng(const std::wstring& path);
bool ExportDemoMarkdown(const std::wstring& path);
int PrintStoreInfo();

}  // namespace rack
