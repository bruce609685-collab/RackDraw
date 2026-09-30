#pragma once

#include <string>

namespace util {

// 用系统默认浏览器打开网址（F-15 检查更新）。
// 程序自身不发起任何网络请求，只把地址交给系统。
bool OpenUrl(const std::wstring& url);

}  // namespace util
