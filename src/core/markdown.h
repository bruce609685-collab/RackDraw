#pragma once

// 布设文件（F-11 导出 / F-12 导入）。
// 与 HTML 对照版 v0.2.1 的 buildLayoutMarkdown() / importLayout() 保持同一格式：
// 两个版本导出的 .md 可以互相导入（含「顶边孔位」列）。

#include <string>
#include <vector>

#include "core/types.h"

namespace rack {

enum class ReportKind { Ok, Warn, Unknown, Error, Indent };

struct ReportRow {
    ReportKind kind = ReportKind::Ok;
    std::wstring text;
};

struct ImportResult {
    bool ok = false;             // 解析是否通过（失败时不修改当前状态）
    std::wstring error;          // 失败原因
    State next;                  // 成功后的新状态（原子切换用）
    std::vector<ReportRow> rows; // 报告行
    int okCount = 0;
    int adjCount = 0;
    int unkCount = 0;
    int skipCount = 0;
    bool canUndo = false;
};

std::wstring BuildLayoutMarkdown(const State& s);
std::wstring BuildAsciiMap(const State& s);
ImportResult ImportLayout(const std::wstring& text, const State& current);

std::wstring FileStamp();    // 文件名时间戳 YYYYMMDD_HHMM
std::wstring LocalStamp();   // 界面文字用时间戳
std::wstring MarkdownFileName(const State& s);

/* ---------------- 显示宽度（ASCII 对照图排版用） ---------------- */
// 中文/全角记 2 列，与终端等宽字体一致；HTML 对照版同名函数口径相同
int DispWidth(const std::wstring& s);
int CharWidth(wchar_t ch);
std::wstring FitLabel(const std::wstring& name, int span);

/* ---------------- Markdown 转义 ---------------- */
std::wstring MdEscape(const std::wstring& s);
std::wstring MdUnescape(const std::wstring& s);

}  // namespace rack
