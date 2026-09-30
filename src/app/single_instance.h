#pragma once

namespace app {

// 单实例运行（F-14）：命名互斥体 + 激活已有窗口。
// 返回 false 表示已有实例在运行（此时本进程应直接退出）。
bool ClaimSingleInstance(const wchar_t* windowClassName);

}  // namespace app
