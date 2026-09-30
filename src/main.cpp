// 程序入口：
//   DPI 声明 → 命令行分支（自检 / 导出 / 演示） → COM + GDI+ 初始化 → 单实例检测 →
//   创建主窗口 → 消息循环 → 退出清理

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "app/app_info.h"
#include "app/dpi_aware.h"
#include "app/single_instance.h"
#include "core/selftest.h"
#include "util/text_convert.h"
#include "ui/main_window.h"

namespace {

std::vector<std::wstring> Arguments() {
    std::vector<std::wstring> out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return out;
    for (int i = 0; i < argc; ++i) out.push_back(argv[i]);
    LocalFree(argv);
    return out;
}

// 窗口子系统程序从控制台启动时默认拿不到 stdout，自检模式需要挂到父控制台。
// 设 RACKDRAW_NO_CONSOLE=1 可跳过挂接，便于自动化脚本重定向抓取输出。
void AttachConsoleForOutput() {
    wchar_t flag[8] = {0};
    if (GetEnvironmentVariableW(L"RACKDRAW_NO_CONSOLE", flag, 8) > 0) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole()) return;
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    app::EnableDpiAwareness();

    const std::vector<std::wstring> args = Arguments();

    // ---- 命令行模式（不建窗口、不占用单实例） ----
    if (args.size() >= 2) {
        const std::wstring& mode = args[1];
        const bool wantsGdiplus = (mode == L"--selftest") || (mode == L"--export-png") ||
                                  (mode == L"--export-md") || (mode == L"--store-info");
        if (wantsGdiplus) {
            AttachConsoleForOutput();
            Gdiplus::GdiplusStartupInput input;
            ULONG_PTR token = 0;
            Gdiplus::GdiplusStartup(&token, &input, nullptr);
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            int exitCode = 0;
            if (mode == L"--selftest") {
                exitCode = rack::RunSelfTest();
            } else if (mode == L"--export-png" && args.size() >= 3) {
                exitCode = rack::ExportDemoPng(args[2]) ? 0 : 1;
            } else if (mode == L"--export-md" && args.size() >= 3) {
                exitCode = rack::ExportDemoMarkdown(args[2]) ? 0 : 1;
            } else if (mode == L"--store-info") {
                exitCode = rack::PrintStoreInfo();
            } else {
                // 注意：控制台输出统一写 UTF-8 字节（fputs），不要用 fputws——
                // 本工具链的 stdout 处于字节模式，宽字符版本会静默失败
                std::fputs(util::WideToUtf8(L"用法：--selftest | --export-png <路径> | "
                                            L"--export-md <路径> | --store-info\n").c_str(),
                           stdout);
                exitCode = 2;
            }

            CoUninitialize();
            Gdiplus::GdiplusShutdown(token);
            return exitCode;
        }

        // 未知的 -- 参数：给出用法并退出，不要静默启动图形界面
        // （--demo 是已知的图形界面参数，必须排除在外）
        if (mode != L"--demo" && mode.rfind(L"--", 0) == 0) {
            AttachConsoleForOutput();
            std::fputs(util::WideToUtf8(L"用法：--selftest | --export-png <路径> | "
                                        L"--export-md <路径> | --store-info | --demo\n").c_str(),
                       stdout);
            return 2;
        }
    }

    // ---- 图形界面 ----
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    if (!app::ClaimSingleInstance(ui::MainWindowClassName())) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        return 0;
    }

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&commonControls);

    rack::State demoState;
    const rack::State* initialState = nullptr;
    if (args.size() >= 2 && args[1] == L"--demo") {
        demoState = rack::MakeDemoState();
        initialState = &demoState;
    }

    if (!ui::CreateMainWindow(instance, showCommand, initialState)) {
        MessageBoxW(nullptr, L"程序启动失败：主窗口创建失败。", APP_NAME_CN, MB_OK | MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        return 1;
    }

    const int exitCode = ui::RunMessageLoop();

    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return exitCode;
}
