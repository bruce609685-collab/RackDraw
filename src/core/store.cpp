#include "core/store.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>

#include "util/text_convert.h"

namespace rack {
namespace {

/* ---------------- 极简 JSON：只处理本存档的固定结构，但对垃圾数据要宽容 ---------------- */

bool ExtractString(const std::wstring& obj, const wchar_t* key, std::wstring* out) {
    const std::wstring needle = std::wstring(L"\"") + key + L"\"";
    size_t at = obj.find(needle);
    if (at == std::wstring::npos) return false;
    at = obj.find(L':', at + needle.size());
    if (at == std::wstring::npos) return false;
    ++at;
    while (at < obj.size() && (obj[at] == L' ' || obj[at] == L'\t')) ++at;
    if (at >= obj.size() || obj[at] != L'"') return false;
    ++at;
    std::wstring value;
    while (at < obj.size()) {
        const wchar_t ch = obj[at];
        if (ch == L'\\' && at + 1 < obj.size()) {
            const wchar_t next = obj[at + 1];
            switch (next) {
                case L'n': value.push_back(L'\n'); break;
                case L't': value.push_back(L'\t'); break;
                case L'r': value.push_back(L'\r'); break;
                case L'"': value.push_back(L'"'); break;
                case L'\\': value.push_back(L'\\'); break;
                case L'/': value.push_back(L'/'); break;
                case L'b': value.push_back(L'\b'); break;
                case L'f': value.push_back(L'\f'); break;
                case L'u': {
                    if (at + 5 < obj.size()) {
                        const std::wstring hex = obj.substr(at + 2, 4);
                        const long code = std::wcstol(hex.c_str(), nullptr, 16);
                        value.push_back(static_cast<wchar_t>(code));
                        at += 4;
                    }
                    break;
                }
                default: value.push_back(next); break;
            }
            at += 2;
            continue;
        }
        if (ch == L'"') break;
        value.push_back(ch);
        ++at;
    }
    *out = value;
    return true;
}

bool ExtractInt(const std::wstring& obj, const wchar_t* key, int* out) {
    const std::wstring needle = std::wstring(L"\"") + key + L"\"";
    size_t at = obj.find(needle);
    if (at == std::wstring::npos) return false;
    at = obj.find(L':', at + needle.size());
    if (at == std::wstring::npos) return false;
    ++at;
    while (at < obj.size() && (obj[at] == L' ' || obj[at] == L'\t' || obj[at] == L'"')) ++at;
    const size_t begin = at;
    while (at < obj.size() && ((obj[at] >= L'0' && obj[at] <= L'9') || obj[at] == L'-')) ++at;
    if (at == begin) return false;
    *out = std::wcstol(obj.substr(begin, at - begin).c_str(), nullptr, 10);
    return true;
}

// 取出 "devices" 数组里的顶层对象（按大括号配对，跳过字符串里的括号）
std::vector<std::wstring> ExtractObjects(const std::wstring& text) {
    std::vector<std::wstring> objects;
    size_t at = text.find(L"\"devices\"");
    if (at == std::wstring::npos) return objects;
    at = text.find(L'[', at);
    if (at == std::wstring::npos) return objects;

    int depth = 0;
    bool inString = false;
    size_t objBegin = std::wstring::npos;
    for (size_t i = at; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (inString) {
            if (ch == L'\\') { ++i; continue; }
            if (ch == L'"') inString = false;
            continue;
        }
        if (ch == L'"') { inString = true; continue; }
        if (ch == L'{') {
            if (depth == 0) objBegin = i;
            ++depth;
        } else if (ch == L'}') {
            --depth;
            if (depth == 0 && objBegin != std::wstring::npos) {
                objects.push_back(text.substr(objBegin, i - objBegin + 1));
                objBegin = std::wstring::npos;
            }
        } else if (ch == L']' && depth == 0) {
            break;
        }
    }
    return objects;
}

std::wstring JsonEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t ch : s) {
        switch (ch) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (ch < 0x20) {
                    wchar_t buf[8] = {0};
                    std::swprintf(buf, 8, L"\\u%04x", static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

std::wstring AppDataDir() {
    wchar_t path[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::wstring();
    }
    std::wstring dir = path;
    dir += L"\\RackDraw";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// 程序所在目录（exe 同级）。全程宽字符，中文路径无需转换
std::wstring ExecutableDir() {
    wchar_t path[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();
    std::wstring dir(path, length);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    dir.resize(slash);
    return dir;
}

// 目标目录是否可写：建一个临时文件再删掉（比看属性可靠，能识别只读/受保护目录）
bool DirectoryWritable(const std::wstring& dir) {
    if (dir.empty()) return false;
    const std::wstring probe = dir + L"\\.rackdraw-write-probe.tmp";
    HANDLE handle = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
    DeleteFileW(probe.c_str());
    return true;
}

bool PathExists(const std::wstring& path) {
    return !path.empty() &&
           GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 存档目录：优先程序目录（便携）；不可写时退回 %APPDATA%\RackDraw（只探测一次）
std::wstring ResolveStoreDir(bool* portable) {
    static std::wstring cached;
    static bool cachedPortable = false;
    if (!cached.empty()) {
        if (portable != nullptr) *portable = cachedPortable;
        return cached;
    }
    const std::wstring exeDir = ExecutableDir();
    if (DirectoryWritable(exeDir)) {
        cached = exeDir;
        cachedPortable = true;
    } else {
        cached = AppDataDir();
        cachedPortable = false;
        if (cached.empty()) cached = exeDir;   // 极端情况：连用户目录都拿不到
    }
    if (portable != nullptr) *portable = cachedPortable;
    return cached;
}

std::vector<Archived> ReadArchiveFile(const std::wstring& path) {
    std::vector<Archived> list;
    if (path.empty()) return list;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return list;

    std::string bytes;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        bytes.append(buffer, read);
    }
    CloseHandle(file);

    const std::wstring text = util::DecodeFileBytes(bytes);
    const std::vector<std::wstring> objects = ExtractObjects(text);
    int index = 0;
    for (const std::wstring& obj : objects) {
        Archived a;
        bool hasName = ExtractString(obj, L"name", &a.name);
        int value = 0;
        if (ExtractInt(obj, L"w", &value)) a.w = value;
        if (ExtractInt(obj, L"hU", &value)) a.hU = value;
        ExtractString(obj, L"id", &a.id);
        ExtractString(obj, L"createdAt", &a.createdAt);
        if (!hasName && a.id.empty()) continue;   // 完全无意义的条目直接丢弃
        list.push_back(SanitizeArchived(a, index++));
    }
    return list;
}

}  // namespace

std::wstring StoreDir() { return ResolveStoreDir(nullptr); }

bool StoreIsPortable() {
    bool portable = false;
    ResolveStoreDir(&portable);
    return portable;
}

std::wstring StorePath() {
    const std::wstring dir = StoreDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\custom-devices.json";
}

std::wstring IsoNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32] = {0};
    std::swprintf(buf, 32, L"%04d-%02d-%02dT%02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring MakeArchivedId() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[48] = {0};
    std::swprintf(buf, 48, L"custom-%04d%02d%02d%02d%02d%02d-%u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  static_cast<unsigned>(GetTickCount() & 0xffff));
    return buf;
}

Archived SanitizeArchived(const Archived& in, int index) {
    Archived out = in;
    if (out.id.empty()) {
        out.id = MakeArchivedId() + L"-" + std::to_wstring(index);
    }
    out.name = TrimName(out.name, L"自定义设备");
    out.w = ClampW(out.w);
    out.hU = ClampH(out.hU);
    if (out.createdAt.empty()) out.createdAt = L"";
    return out;
}

std::vector<Archived> LoadArchive() {
    const std::wstring path = StorePath();
    const bool existed = PathExists(path);
    std::vector<Archived> list = ReadArchiveFile(path);
    if (list.empty() && !existed) {
        // 存档文件不存在时落一个空存档，让「存档在哪儿」一目了然。
        // 程序只认这一份：不做任何迁移，删掉存档再启动就是空的
        SaveArchive(list);
    }
    return list;
}

bool SaveArchive(const std::vector<Archived>& list) {
    const std::wstring path = StorePath();
    if (path.empty()) return false;

    std::wstring json = L"{\n  \"version\": 1,\n  \"updatedAt\": \"";
    json += JsonEscape(IsoNow());
    json += L"\",\n  \"devices\": [";
    for (size_t i = 0; i < list.size(); ++i) {
        const Archived& a = list[i];
        json += (i == 0 ? L"\n" : L",\n");
        json += L"    { \"id\": \"" + JsonEscape(a.id) + L"\", \"name\": \"" + JsonEscape(a.name) +
                L"\", \"w\": " + std::to_wstring(a.w) +
                L", \"hU\": " + std::to_wstring(a.hU) +
                L", \"createdAt\": \"" + JsonEscape(a.createdAt) +
                L"\", \"note\": \"\" }";
    }
    if (!list.empty()) json += L"\n  ";
    json += L"]\n}\n";

    const std::string bytes = util::WideToUtf8(json);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                              &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

bool UpsertArchive(std::vector<Archived>* list, const Archived& item) {
    for (Archived& a : *list) {
        if (a.name == item.name) {   // 同名：更新尺寸，不新增（B-20）
            a.w = item.w;
            a.hU = item.hU;
            return false;
        }
    }
    list->push_back(item);
    return true;
}

std::wstring CopyNameOf(const std::vector<Archived>& list, const std::wstring& name) {
    std::wstring stem = name;
    if (stem.size() > 8) stem.resize(8);
    for (int i = 1; i <= 99; ++i) {
        std::wstring cand = (i == 1) ? (stem + L" 副本")
                                     : (stem + L" 副本" + std::to_wstring(i));
        if (cand.size() > static_cast<size_t>(kNameMax)) cand.resize(kNameMax);
        bool taken = false;
        for (const Archived& a : list) {
            if (a.name == cand) { taken = true; break; }
        }
        if (!taken) return cand;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring fallback = stem + std::to_wstring(st.wMinute) + std::to_wstring(st.wSecond);
    if (fallback.size() > static_cast<size_t>(kNameMax)) fallback.resize(kNameMax);
    return fallback;
}

}  // namespace rack
