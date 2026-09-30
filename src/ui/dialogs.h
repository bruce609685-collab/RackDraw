#pragma once

// 原生对话框：文件选择（IFileSaveDialog / IFileOpenDialog）、自定义设备、导入报告、文本输入。
// 说明：主界面完全自绘（与 HTML 对照版一致），但**弹窗内的输入控件用系统原生控件**——
// 中文输入法（IME）与系统无障碍支持只有原生 EDIT/COMBOBOX 才可靠，自绘输入框会失去这些能力。

#include <windows.h>

#include <string>
#include <vector>

#include "core/markdown.h"
#include "core/types.h"

namespace ui {

/* ---------------- 文件选择 ---------------- */
bool PickSavePath(HWND owner, const std::wstring& defaultName, const wchar_t* filterLabel,
                  const wchar_t* filterSpec, const wchar_t* defaultExt, std::wstring* out);
bool PickOpenPath(HWND owner, const wchar_t* filterLabel, const wchar_t* filterSpec,
                  const wchar_t* defaultExt, std::wstring* out);

/* ---------------- 自定义设备（F-07） ---------------- */
struct CustomDeviceInput {
    std::wstring name;
    int w = 6;
    int hU = 1;
    bool saveToArchive = false;
};
bool ShowCustomDeviceDialog(HWND owner, CustomDeviceInput* inOut);

/* ---------------- 导入报告（F-12） ---------------- */
enum class ReportAction { None, Undo };
ReportAction ShowImportReportDialog(HWND owner, const std::wstring& title,
                                    const std::vector<rack::ReportRow>& rows, bool canUndo);

/* ---------------- 文本输入 / 消息 ---------------- */
bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label,
                const std::wstring& initial, int maxLength, std::wstring* out);
void ShowInfo(HWND owner, const std::wstring& text);
void ShowError(HWND owner, const std::wstring& text);
bool Confirm(HWND owner, const std::wstring& text, const std::wstring& title);

}  // namespace ui
