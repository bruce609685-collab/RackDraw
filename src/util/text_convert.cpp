#include "util/text_convert.h"

#include <windows.h>

namespace util {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        &out[0], need);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        &out[0], need, nullptr, nullptr);
    return out;
}

std::wstring DecodeFileBytes(const std::string& bytes) {
    // 跳过 UTF-8 BOM
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return Utf8ToWide(bytes.substr(3));
    }
    return Utf8ToWide(bytes);
}

}  // namespace util
