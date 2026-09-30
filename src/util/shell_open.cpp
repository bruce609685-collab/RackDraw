#include "util/shell_open.h"

#include <windows.h>
#include <shellapi.h>

namespace util {

bool OpenUrl(const std::wstring& url) {
    if (url.empty()) return false;
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    // 返回值 <= 32 表示失败（见 ShellExecute 文档）
    return reinterpret_cast<INT_PTR>(result) > 32;
}

}  // namespace util
