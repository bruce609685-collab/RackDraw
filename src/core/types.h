#pragma once

// 数据模型与几何常量。
// 与 HTML 对照版 v0.2.1 的 app.js 顶部常量、数据结构一一对应（同一份口径）；
// 改动几何常量时必须同步改 HTML 对照版，否则两处的导出图/布设表会对不上。

#include <string>
#include <vector>

namespace rack {

/* ---------------- 几何常量（对应 app.js 几何常量区） ---------------- */
constexpr int kUPx = 22;          // 一格 U 高
constexpr int kHolesPerU = 3;     // 每 U 三个螺丝孔位
constexpr int kHGrid = 6;         // 横向 6 等分
constexpr int kInnerW = 312;      // 机柜内框宽
constexpr int kPadTop = 14;
constexpr int kPadBottom = 14;
constexpr int kPadX = 0;
constexpr int kCapH = 20;         // 上下盖高（界面用；导出图为 44/26，见 image_export）
constexpr int kLabelW = 26;       // U 编号栏宽（界面用；导出图 34）
constexpr int kRailW = 16;        // 立柱宽（界面用；导出图 18）
constexpr int kFramePadX = 14;    // 机柜外框左右内边距

/* ---------------- 交互常量 ---------------- */
constexpr int kZoomMin = 50;
constexpr int kZoomMax = 300;
constexpr int kZoomStepButton = 10;
constexpr int kDragThreshold = 4;   // 指针位移小于该值视为点击（逻辑像素）
constexpr int kNameMax = 12;        // 自定义设备名长度上限
constexpr int kHMax = 24;           // 设备高度上限（U）
constexpr int kSaveScale = 2;       // 导出图倍率
constexpr long long kMaxCanvasPx = 40000000LL;  // 画布像素上限

/* ---------------- 枚举与结构 ---------------- */
enum class Category { Router, Switch, Fw, Wireless, Small, Monitor, Accessory, Custom };
enum class SnapMode { U, Hole };
enum class RackKind { U42, U36, U24 };

struct Device {
    int id = 0;
    Category cat = Category::Switch;
    std::wstring name;
    int w = 6;      // 6 = 全宽 | 3 = 1/2 宽 | 2 = 1/3 宽
    int hU = 1;     // 高度 U 数（1~24）
    int col = 0;    // 左缘格位（0 起，最大 6-w）
    int hole = 0;   // 顶边孔位索引（自顶向下 0 起）
};

// 设备目录叶子（内置目录）
struct CatalogLeaf {
    Category cat;
    const wchar_t* label;   // 目录里显示的名字（如「标准」）
    const wchar_t* full;    // 规范全名（如「交换机 · 标准」），导入匹配的键
    int w;
    int hU;
};

// 存档设备（「我的设备」，对应 localStorage rackdraw.customDevices.v1）
struct Archived {
    std::wstring id;
    std::wstring name;
    int w = 6;
    int hU = 1;
    std::wstring createdAt;
};

struct State {
    RackKind rack = RackKind::U42;
    SnapMode mode = SnapMode::U;
    std::vector<Device> devices;
    std::vector<Archived> archive;
    int seq = 1;
    int selectedId = 0;   // 0 = 未选中
    int zoom = 100;
};

/* ---------------- 机柜规格 ---------------- */
int RackU(RackKind kind);
int TotalHoles(RackKind kind);
const wchar_t* RackText(RackKind kind);          // 「42U」
bool RackFromText(const std::wstring& text, RackKind* out);

/* ---------------- 派生几何 ---------------- */
inline int SlotH() { return kUPx / kHolesPerU; }          // 22/3 → 7（仅用于离散计算）
inline double SlotHExact() { return static_cast<double>(kUPx) / kHolesPerU; }
inline double ColW() { return static_cast<double>(kInnerW) / kHGrid; }
inline int BodyH(RackKind kind) { return RackU(kind) * kUPx; }
inline int InnerH(RackKind kind) { return BodyH(kind) + kPadTop + kPadBottom; }

/* ---------------- 坐标换算 ---------------- */
int HoleToUText(RackKind kind, int hole);   // 孔位 → 顶边所在 U（自顶向下 U 号，1 起）
int TopU(const State& s, const Device& d);
int BottomU(const State& s, const Device& d);
int UsedU(const State& s);

/* ---------------- 冲突与顺延 ---------------- */
bool Collides(const State& s, int col, int hole, const Device& dev, int excludeId);

struct SlotProbe {
    bool found = false;
    int hole = 0;
    bool adjusted = false;
};
SlotProbe FindFreeHole(const State& s, int col, int hole, const Device& dev,
                       int step, int maxHole);

/* ---------------- 文字描述（导出表用） ---------------- */
std::wstring WidthText(int w);              // 标准全宽 / 1/2 宽 / 1/3 宽
std::wstring ColText(const Device& d);      // 居中 / 左侧 / 右侧 / col N-M
std::wstring SpanText(const Device& d);     // 「3U × 全宽」

/* ---------------- 吸附 ---------------- */
// 机柜内框在屏幕上的位置与缩放（对应 HTML 对照版 els.inner.getBoundingClientRect()）
struct InnerRect {
    double left = 0;
    double top = 0;
    double width = 0;
    double height = 0;
    double scale = 1.0;
};

struct SnapResult {
    bool valid = false;   // 是否能给出吸附格位（设备比机柜还高时为 false）
    bool ok = false;      // 该格位是否无冲突
    int col = 0;
    int hole = 0;
};

// px/py = 指针位置（= 示意外形中心），语义与 HTML 对照版 calcSnap 完全一致
SnapResult CalcSnap(const State& s, const Device& dev, const InnerRect& r,
                    double px, double py, int excludeId);

/* ---------------- 小工具 ---------------- */
int ClampInt(int v, int lo, int hi);
int ClampH(int hU);
int ClampW(int w);
std::wstring TrimName(const std::wstring& raw, const std::wstring& fallback);

}  // namespace rack
