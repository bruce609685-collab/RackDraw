#pragma once

// 自定义设备存档（F-10）。
// 位置：**程序所在目录**的 custom-devices.json（绿色便携式，可随 exe 一起拷走）；
//      若程序目录不可写（例如装在 Program Files），自动退回 %APPDATA%\RackDraw\ 并提示。
// 路径全程走宽字符 Win32 API（GetModuleFileNameW / CreateFileW），中文目录无需特殊处理。
// 与 HTML 对照版 localStorage['rackdraw.customDevices.v1'] 同一份 schema，字段可手工互转。

#include <string>
#include <vector>

#include "core/types.h"

namespace rack {

std::wstring StorePath();          // 当前生效的存档文件完整路径
bool StoreIsPortable();            // true = 存档在程序目录（便携模式）
std::wstring StoreDir();           // 当前生效的存档目录

std::wstring MakeArchivedId();
std::wstring IsoNow();

// 读取并逐条规整；文件不存在/损坏时返回空列表（不阻塞启动）
// 文件不存在时会落一个空存档；程序**只认自己目录下的这一份**，不做任何迁移
std::vector<Archived> LoadArchive();
bool SaveArchive(const std::vector<Archived>& list);

// 单条规整：名称裁到 12 字符、宽度归一到 {6,3,2}、高度归一到 1~24、补全 id
Archived SanitizeArchived(const Archived& in, int index);

// 同名去重：存在同名则更新尺寸，返回 false 表示是「更新」而非「新增」
bool UpsertArchive(std::vector<Archived>* list, const Archived& item);

// 生成不与现有存档重名的副本名（原名可能已达 12 字上限）
std::wstring CopyNameOf(const std::vector<Archived>& list, const std::wstring& name);

}  // namespace rack
