#pragma once

#include <string>

namespace util {

// UTF-8 <-> UTF-16 转换（存档与布设文件都是 UTF-8，Win32 API 用 UTF-16）
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

// 从文件的 UTF-8 原始字节读入（自动跳过 BOM）
std::wstring DecodeFileBytes(const std::string& bytes);

}  // namespace util
