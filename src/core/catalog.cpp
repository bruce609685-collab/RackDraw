#include "core/catalog.h"

#include <cwctype>

namespace rack {
namespace {

/* 目录树（F-03：7 个一级项，与 HTML 对照版 CATALOG 完全一致） */
const CatalogLeaf kRouter[] = {
    {Category::Router, L"标准", L"路由器 · 标准", 6, 1},
    {Category::Router, L"小型（1/2宽）", L"路由器 · 小型", 3, 1},
    {Category::Router, L"mini（1/3宽）", L"路由器 · mini", 2, 1},
};
const CatalogLeaf kSwitch[] = {
    {Category::Switch, L"标准", L"交换机 · 标准", 6, 1},
    {Category::Switch, L"小型（1/2宽）", L"交换机 · 小型", 3, 1},
    {Category::Switch, L"mini（1/3宽）", L"交换机 · mini", 2, 1},
};
const CatalogLeaf kFirewall[] = {
    {Category::Fw, L"标准", L"防火墙 · 标准", 6, 1},
    {Category::Fw, L"小型（1/2宽）", L"防火墙 · 小型", 3, 1},
    {Category::Fw, L"mini（1/3宽）", L"防火墙 · mini", 2, 1},
};
const CatalogLeaf kApController[] = {
    {Category::Wireless, L"标准", L"AC控制器 · 标准", 6, 1},
    {Category::Wireless, L"小型（1/2宽）", L"AC控制器 · 小型", 3, 1},
    {Category::Wireless, L"mini（1/3宽）", L"AC控制器 · mini", 2, 1},
};
const CatalogLeaf kPoeSwitch[] = {
    {Category::Wireless, L"标准", L"PoE交换机 · 标准", 6, 1},
    {Category::Wireless, L"小型（1/2宽）", L"PoE交换机 · 小型", 3, 1},
    {Category::Wireless, L"mini（1/3宽）", L"PoE交换机 · mini", 2, 1},
};
const CatalogLeaf kAcRouter[] = {
    {Category::Wireless, L"标准", L"AC路由一体机 · 标准", 6, 1},
    {Category::Wireless, L"小型（1/2宽）", L"AC路由一体机 · 小型", 3, 1},
    {Category::Wireless, L"mini（1/3宽）", L"AC路由一体机 · mini", 2, 1},
};
const CatalogLeaf kSmall[] = {
    {Category::Small, L"光电转换器", L"光电转换器", 2, 1},
    {Category::Small, L"光猫", L"光猫", 2, 1},
    {Category::Small, L"HDMI延长器", L"HDMI延长器", 2, 1},
};
const CatalogLeaf kMonitor[] = {
    {Category::Monitor, L"监控录像机", L"监控录像机", 6, 3},
    {Category::Monitor, L"监控交换机", L"监控交换机", 6, 1},
};
const CatalogLeaf kAccessory[] = {
    {Category::Accessory, L"理线架", L"理线架", 6, 1},
    {Category::Accessory, L"配线架", L"配线架", 6, 1},
    {Category::Accessory, L"空位挡板", L"空位挡板", 6, 1},
};

std::vector<CatalogNode> MakeLeaves(const CatalogLeaf* leaves, size_t count) {
    std::vector<CatalogNode> out;
    for (size_t i = 0; i < count; ++i) {
        CatalogNode node;
        node.label = leaves[i].label;
        node.isLeaf = true;
        node.leaf = leaves[i];
        out.push_back(node);
    }
    return out;
}

std::vector<CatalogNode> BuildCatalog() {
    std::vector<CatalogNode> nodes;

    {
        CatalogNode n;
        n.label = L"路由器";
        n.children = MakeLeaves(kRouter, sizeof(kRouter) / sizeof(kRouter[0]));
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"交换机";
        n.children = MakeLeaves(kSwitch, sizeof(kSwitch) / sizeof(kSwitch[0]));
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"防火墙";
        n.children = MakeLeaves(kFirewall, sizeof(kFirewall) / sizeof(kFirewall[0]));
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"无线设备";
        CatalogNode a;
        a.label = L"AC控制器";
        a.children = MakeLeaves(kApController, sizeof(kApController) / sizeof(kApController[0]));
        CatalogNode b;
        b.label = L"PoE交换机";
        b.children = MakeLeaves(kPoeSwitch, sizeof(kPoeSwitch) / sizeof(kPoeSwitch[0]));
        CatalogNode c;
        c.label = L"AC路由一体机";
        c.children = MakeLeaves(kAcRouter, sizeof(kAcRouter) / sizeof(kAcRouter[0]));
        n.children.push_back(a);
        n.children.push_back(b);
        n.children.push_back(c);
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"小型设备";
        n.children = MakeLeaves(kSmall, sizeof(kSmall) / sizeof(kSmall[0]));
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"监控设备";
        n.children = MakeLeaves(kMonitor, sizeof(kMonitor) / sizeof(kMonitor[0]));
        nodes.push_back(n);
    }
    {
        CatalogNode n;
        n.label = L"机柜配件";
        n.children = MakeLeaves(kAccessory, sizeof(kAccessory) / sizeof(kAccessory[0]));
        nodes.push_back(n);
    }
    return nodes;
}

void CollectLeaves(const std::vector<CatalogNode>& nodes, std::vector<const CatalogLeaf*>* out) {
    for (const CatalogNode& n : nodes) {
        if (n.isLeaf) {
            out->push_back(&n.leaf);
        } else {
            CollectLeaves(n.children, out);
        }
    }
}

}  // namespace

const std::vector<CatalogNode>& Catalog() {
    static const std::vector<CatalogNode> nodes = BuildCatalog();
    return nodes;
}

CatTheme CatThemeOf(Category cat) {
    switch (cat) {
        case Category::Router:    return {0x3f89c9, 0x2b6cb0, 0x22568c, 0x3182ce};
        case Category::Switch:    return {0x556273, 0x2d3748, 0x1f2733, 0x4a5568};
        case Category::Fw:        return {0xd35f5f, 0xb13a3a, 0x8a2c2c, 0xc53030};
        case Category::Wireless:  return {0x57b183, 0x35785a, 0x275e45, 0x2f855a};
        case Category::Small:     return {0xe0ac55, 0xb57f22, 0x8a611a, 0xb7791f};
        case Category::Monitor:   return {0x8c7bd4, 0x6c5cb8, 0x4f418c, 0x7a6ac8};
        case Category::Accessory: return {0x8496ad, 0x5f7186, 0x4a5a6d, 0x5f7186};
        case Category::Custom:    return {0x93a1b3, 0x66727f, 0x4b5563, 0x718096};
    }
    return {0x93a1b3, 0x66727f, 0x4b5563, 0x718096};
}

const wchar_t* CatLabel(Category cat) {
    switch (cat) {
        case Category::Router:    return L"路由器";
        case Category::Switch:    return L"交换机";
        case Category::Fw:        return L"防火墙";
        case Category::Wireless:  return L"无线设备";
        case Category::Small:     return L"小型设备";
        case Category::Monitor:   return L"监控设备";
        case Category::Accessory: return L"机柜配件";
        case Category::Custom:    return L"自定义";
    }
    return L"自定义";
}

const wchar_t* CatLeds(const Device& d) {
    if (d.cat == Category::Monitor) return IsRecorder(d) ? L"gag" : L"ga";
    switch (d.cat) {
        case Category::Router:   return L"gag";  // 绿 琥珀 绿
        case Category::Switch:   return L"ga";   // 绿 琥珀
        case Category::Fw:       return L"gga";  // 绿 绿 琥珀（与 HTML 对照版导出图口径一致）
        case Category::Wireless: return L"ga";
        default:                 return L"";
    }
}

bool IsPoE(const Device& d) { return d.name.find(L"PoE") != std::wstring::npos; }
bool IsHdmi(const Device& d) { return d.name.find(L"HDMI") != std::wstring::npos; }
bool IsCableMgr(const Device& d) {
    return d.cat == Category::Accessory && d.name.find(L"理线") != std::wstring::npos;
}
bool IsPatchPanel(const Device& d) {
    return d.cat == Category::Accessory && d.name.find(L"配线") != std::wstring::npos;
}
bool IsBlankPanel(const Device& d) {
    return d.cat == Category::Accessory && !IsCableMgr(d) && !IsPatchPanel(d);
}

bool IsRecorder(const Device& d) {
    return d.cat == Category::Monitor && d.name.find(L"录像") != std::wstring::npos;
}

int PortCount(const Device& d) {
    switch (d.cat) {
        case Category::Router:   return d.w == 6 ? 4 : 2;
        case Category::Switch:   return d.w == 6 ? 14 : (d.w == 3 ? 7 : 4);
        case Category::Fw:       return (d.w + 1) < 6 ? (d.w + 1) : 6;
        case Category::Wireless: return IsPoE(d) ? 4 : 2;
        case Category::Monitor:
            if (IsRecorder(d)) return d.w == 6 ? 6 : (d.w == 3 ? 3 : 2);   // 硬盘位
            return d.w == 6 ? 14 : (d.w == 3 ? 7 : 4);                      // 网口
        case Category::Accessory:
            if (IsCableMgr(d)) return d.w == 6 ? 8 : 4;
            if (IsPatchPanel(d)) return d.w == 6 ? 12 : 6;
            return 0;
        default: return 0;
    }
}

std::wstring NormalizeName(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t ch : s) {
        if (ch == L' ' || ch == L'\t' || ch == L'\u00b7' || ch == L'\u30fb' || ch == L'\u3000') continue;
        out.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return out;
}

NameMatch MatchDeviceName(const std::wstring& name, const std::vector<Archived>& archive) {
    static std::vector<const CatalogLeaf*> leaves = [] {
        std::vector<const CatalogLeaf*> out;
        CollectLeaves(Catalog(), &out);
        return out;
    }();

    // ① 精确匹配内置目录
    for (const CatalogLeaf* leaf : leaves) {
        if (name == leaf->full) return {leaf->cat, leaf->full, false};
    }
    // ② 精确匹配存档
    for (const Archived& a : archive) {
        if (name == a.name) return {Category::Custom, a.name, false};
    }
    // ③ 模糊匹配内置
    const std::wstring want = NormalizeName(name);
    for (const CatalogLeaf* leaf : leaves) {
        if (NormalizeName(leaf->full) == want) return {leaf->cat, leaf->full, false};
    }
    // ④ 模糊匹配存档
    for (const Archived& a : archive) {
        if (NormalizeName(a.name) == want) return {Category::Custom, a.name, false};
    }
    // 未知设备：按文件里的几何尺寸建自定义设备
    return {Category::Custom, name, true};
}

}  // namespace rack
