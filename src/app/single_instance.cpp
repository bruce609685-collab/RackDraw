#include "app/single_instance.h"

#include <windows.h>

namespace app {
namespace {

// 句柄保留到进程退出，由系统自动释放（崩溃时同样释放，不会残留死锁）
HANDLE g_mutex = nullptr;

void ActivateExistingWindow(const wchar_t* windowClassName) {
    const HWND existing = FindWindowW(windowClassName, nullptr);
    if (existing == nullptr) return;

    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
}

}  // namespace

bool ClaimSingleInstance(const wchar_t* windowClassName) {
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\RackDraw.SingleInstance");
    if (g_mutex == nullptr) {
        return true;  // 互斥体不可用时不阻止启动，避免把程序卡死
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingWindow(windowClassName);
        return false;
    }
    return true;
}

}  // namespace app
