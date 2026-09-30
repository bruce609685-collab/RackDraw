#pragma once

// 设备目录与类别口径（对应 HTML 对照版 app.js 的 CATALOG / CAT_COLORS / CAT_LEDS / portCount）。
// 这些是「单一来源」：界面绘制与导出图必须都从这里取，禁止在绘制代码里另写一套。

#include <cstdint>
#include <string>
#include <vector>

#include "core/types.h"

namespace rack {

/* ---------------- 类别主题色 ---------------- */
// c0/c1/c2 = 面板纵向渐变（顶/中/底），chip = 目录色块单色；值取自 HTML 对照版 CAT_COLORS
struct CatTheme {
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t chip;
};
CatTheme CatThemeOf(Category cat);

const wchar_t* CatLabel(Category cat);

/* ---------------- 面板元素口径 ---------------- */
// LED 颜色序列：'g' = 绿，'a' = 琥珀（监控设备按名称区分：录像机 3 灯 / 交换机 2 灯）
const wchar_t* CatLeds(const Device& d);
int PortCount(const Device& d);      // 网口 / RJ45 / 散热孔 / 理线环 / 配线口 / 硬盘位数量
bool IsPoE(const Device& d);
bool IsHdmi(const Device& d);
bool IsCableMgr(const Device& d);
bool IsPatchPanel(const Device& d);
bool IsBlankPanel(const Device& d);  // 机柜配件里既非理线也非配线 → 空位挡板
bool IsRecorder(const Device& d);    // 监控录像机（画硬盘位；否则按网口画）

/* ---------------- 目录树 ---------------- */
struct CatalogNode {
    std::wstring label;
    bool isLeaf = false;
    CatalogLeaf leaf{};
    std::vector<CatalogNode> children;
};

const std::vector<CatalogNode>& Catalog();

/* ---------------- 名称匹配（导入用） ---------------- */
// ① 精确匹配内置目录 ② 精确匹配存档 ③ 模糊匹配内置 ④ 模糊匹配存档
struct NameMatch {
    Category cat = Category::Custom;
    std::wstring name;
    bool unknown = false;
};
NameMatch MatchDeviceName(const std::wstring& name,
                          const std::vector<Archived>& archive);

std::wstring NormalizeName(const std::wstring& s);  // 去空白/·/・ + 转小写

}  // namespace rack
