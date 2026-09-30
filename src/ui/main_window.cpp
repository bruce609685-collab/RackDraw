// 主窗口：顶部工具栏、设备目录侧栏、机柜舞台、拖拽会话、缩放、提示条。
// 界面完全自绘（与 HTML 对照版一致），弹窗内的输入控件用系统原生控件（见 dialogs.cpp 说明）。
//
// 交互语义与 HTML 对照版 v0.2.1 一致（§3.5 松手判定表 / §3.17 误操作防护）：
//   · 指针位移 < 4px 视为点击：目录设备 → 待放置态；机柜内设备 → 选中（不挪位）
//   · 指针（= 设备图形中心）在机柜外框内才吸附；离开外框松手 = 移出（新设备则取消）
//   · 切换机柜若会移除设备先确认；机柜非空时关闭窗口先确认

#include "ui/main_window.h"

#include <commctrl.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "app/app_info.h"
#include "app/dpi_aware.h"
#include "core/catalog.h"
#include "core/markdown.h"
#include "core/store.h"
#include "ui/dialogs.h"
#include "ui/image_export.h"
#include "ui/rack_view.h"
#include "ui/theme.h"
#include "util/shell_open.h"
#include "util/text_convert.h"

namespace ui {
namespace {

using rack::Archived;
using rack::CatalogLeaf;
using rack::Category;
using rack::Device;
using rack::RackKind;
using rack::SnapMode;
using rack::State;

// 图标资源 id（与 resource/app.rc 保持一致）
const int kIconId = 101;

/* ---------------- 布局常量（96 DPI 设计值） ---------------- */
constexpr int kSidebarW = 292;
constexpr int kTopPad = 10;
constexpr int kRow1H = 32;
constexpr int kRowGap = 8;
constexpr int kRow2H = 34;
constexpr int kTopbarPadX = 20;
constexpr int kStagePadTop = 30;
constexpr int kStagePadBottom = 60;
constexpr int kTreeRowH = 28;
constexpr int kTreeIndent = 16;
constexpr int kSidePad = 14;
constexpr int kTreeTopGap = 30;   // 侧栏标题下方留白

struct Layout {
    RECT client{};
    RECT topbar{};
    RECT updateBtn{};
    RECT rackBtn{};
    RECT modeU{};
    RECT modeHole{};
    RECT clearBtn{};
    RECT saveBtn{};
    RECT exportBtn{};
    RECT importBtn{};
    RECT zoomMinus{};
    RECT zoomTrack{};
    RECT zoomPlus{};
    RECT zoomReset{};
    RECT sidebar{};
    RECT stage{};
    RECT stats{};
};

enum class RowKind { Group, Leaf, AddCustom, MyDevices, Archive, Empty };
/** 行首小图标（自绘，不用 Emoji——微软雅黑没有 Emoji 字形，会渲染成方框） */
enum class RowIcon { None, Folder, Plus };

struct TreeRow {
    RowKind kind = RowKind::Group;
    RowIcon icon = RowIcon::None;
    std::wstring label;
    std::wstring tag;
    std::wstring path;
    int depth = 0;
    bool open = false;
    CatalogLeaf leaf{};
    int archiveIndex = -1;
    RECT rect{};
};

struct Session {
    bool active = false;
    Device dev;
    int id = 0;            // >0 = 拖动已有设备
    bool pending = false;  // 待放置态
    bool moved = false;
    bool fromPending = false;
    POINT start{};
    bool hasSnap = false;
    rack::SnapResult snap;
};

struct App {
    State state;
    HWND hwnd = nullptr;
    Layout layout;
    std::vector<TreeRow> rows;
    std::map<std::wstring, bool> expanded;
    Session session;
    int stageScroll = 0;
    int sidebarScroll = 0;
    int sidebarContentH = 0;
    int hoverRow = -1;
    int hoverControl = 0;
    bool draggingTrack = false;
    bool draggingSidebarThumb = false;   // 正在拖动侧栏滚动条滑块
    float sidebarThumbGrabDy = 0.0f;
    std::wstring toast;
    ULONGLONG toastUntil = 0;
    std::wstring hint;
    bool hasBackup = false;
    State backup;
    POINT lastMouse{};
    Gdiplus::Bitmap* ghostCache = nullptr;
    std::wstring ghostCacheKey;
    int ghostCacheW = 0;
    int ghostCacheH = 0;
};

App* g_app = nullptr;

/* ---------------- 控件 id（自绘控件的命中标记） ---------------- */
enum : int {
    kCtlNone = 0,
    kCtlUpdate,
    kCtlRack,
    kCtlModeU,
    kCtlModeHole,
    kCtlClear,
    kCtlSave,
    kCtlExport,
    kCtlImport,
    kCtlZoomMinus,
    kCtlZoomTrack,
    kCtlZoomPlus,
    kCtlZoomReset,
};

const wchar_t* kNotes[] = {
    L"① 点击左侧设备，出现可拖动的示意外形，拖到机柜卡位松手即吸附；",
    L"② 同一 U 内可混排，总宽不超过 1 个标准宽度；",
    L"③ 拖动机柜内设备可调整位置；拖出机柜松手移出，选中后按 Delete 或点 × 删除；",
    L"④ 版式切换：U 数版式按整 U 吸附，螺丝孔位版式每 U 分 3 孔；",
    L"⑤ 「我的设备」中的存档设备支持右键：重命名 / 复制 / 删除。",
};

/* ================================================================
 * 布局
 * ================================================================ */
Layout ComputeLayout(HWND hwnd) {
    Layout L;
    GetClientRect(hwnd, &L.client);
    const int padX = Scale(kTopbarPadX);
    const int topPad = Scale(kTopPad);
    const int row1H = Scale(kRow1H);
    const int row2H = Scale(kRow2H);
    const int gap = Scale(12);
    const int rowGap = Scale(6);

    // 第一行：品牌 + 检查更新
    const int updateW = Scale(76);
    L.updateBtn = {L.client.right - padX - updateW, topPad + Scale(2),
                   L.client.right - padX, topPad + Scale(2) + row1H - Scale(2)};

    // 第二行起：机柜 / 版式 / 清空 / 保存 / 导出 / 导入 / 缩放 / 统计
    // 宽度不足时按 HTML 对照版 flex-wrap 的行为换行（统计整行时右对齐，对应 margin-left:auto）
    const int rowRight = L.client.right - padX;
    int rowTop = topPad + row1H + Scale(kRowGap);
    int x = padX;

    auto wrap = [&]() {
        rowTop += row2H + rowGap;
        x = padX;
    };
    auto fits = [&](int width) { return x + width <= rowRight || x <= padX; };
    auto place = [&](RECT* r, int width) {
        if (!fits(width)) wrap();
        r->left = x;
        r->top = rowTop;
        r->right = x + width;
        r->bottom = rowTop + row2H;
        x += width + gap;
    };

    place(&L.rackBtn, Scale(170));

    // 版式分段控件（两段一体）
    {
        const int segW = Scale(104) + Scale(112);
        if (!fits(segW)) wrap();
        L.modeU = {x, rowTop, x + Scale(104), rowTop + row2H};
        L.modeHole = {L.modeU.right, rowTop, L.modeU.right + Scale(112), rowTop + row2H};
        x = L.modeHole.right + gap;
    }

    place(&L.clearBtn, Scale(80));
    place(&L.saveBtn, Scale(86));
    place(&L.exportBtn, Scale(86));
    place(&L.importBtn, Scale(86));

    // 缩放控件（圆角容器内：− 轨道 ＋ 数值 重置，宽度配比与 HTML 对照版一致）
    {
        const int zoomW = Scale(280);
        const int zoomH = Scale(30);
        if (!fits(zoomW)) wrap();
        const int zoomTop = rowTop + (row2H - zoomH) / 2;
        L.zoomMinus = {x + Scale(6), zoomTop, x + Scale(6) + Scale(18), zoomTop + zoomH};
        L.zoomTrack = {L.zoomMinus.right + Scale(6), zoomTop + Scale(13),
                       L.zoomMinus.right + Scale(6) + Scale(110), zoomTop + Scale(17)};
        L.zoomPlus = {L.zoomTrack.right + Scale(6), zoomTop,
                      L.zoomTrack.right + Scale(6) + Scale(18), zoomTop + zoomH};
        L.zoomReset = {x + zoomW - Scale(58), zoomTop + Scale(3), x + zoomW - Scale(6),
                       zoomTop + zoomH - Scale(3)};
        x += zoomW + gap;
    }

    // 统计：放得下就同一行右对齐，否则另起一行
    {
        const int statsW = Scale(260);
        if (!fits(statsW)) wrap();
        L.stats = {x, rowTop, rowRight, rowTop + row2H};
    }

    L.topbar = L.client;
    L.topbar.bottom = rowTop + row2H + Scale(12);

    // 侧栏 + 舞台
    L.sidebar = {0, L.topbar.bottom, Scale(kSidebarW), L.client.bottom};
    L.stage = {Scale(kSidebarW), L.topbar.bottom, L.client.right, L.client.bottom};
    return L;
}

/* ================================================================
 * 目录树行
 * ================================================================ */
std::wstring PathOf(int depth, int index) {
    return std::to_wstring(depth) + L"-" + std::to_wstring(index);
}

bool IsExpanded(App* app, const std::wstring& path, bool fallback) {
    auto it = app->expanded.find(path);
    if (it == app->expanded.end()) return fallback;
    return it->second;
}

void AddGroupRows(App* app, const std::vector<rack::CatalogNode>& nodes, int depth,
                  const std::wstring& parentPath, int* y) {
    int index = 0;
    for (const rack::CatalogNode& node : nodes) {
        TreeRow row;
        const std::wstring path = parentPath + L"/" + PathOf(depth, index++);
        row.depth = depth;
        row.path = path;
        row.rect = {app->layout.sidebar.left, *y, app->layout.sidebar.right, *y + Scale(kTreeRowH)};
        *y += Scale(kTreeRowH);

        if (node.isLeaf) {
            row.kind = RowKind::Leaf;
            row.label = node.label;
            row.tag = rack::WidthText(node.leaf.w);
            row.leaf = node.leaf;
            app->rows.push_back(row);
        } else {
            row.kind = RowKind::Group;
            row.icon = RowIcon::Folder;
            row.label = node.label;
            row.open = IsExpanded(app, path, false);   // 默认全部收起（状态不落盘）
            app->rows.push_back(row);
            if (row.open) AddGroupRows(app, node.children, depth + 1, path, y);
        }
    }
}

void BuildRows(App* app) {
    app->rows.clear();
    int y = app->layout.sidebar.top + Scale(kSidePad + 20 + 10);
    AddGroupRows(app, rack::Catalog(), 0, L"root", &y);

    // 自定义分组（新建 + 我的设备）
    {
        const std::wstring path = L"root/custom";
        TreeRow row;
        row.kind = RowKind::Group;
        row.icon = RowIcon::Folder;
        row.depth = 0;
        row.path = path;
        row.label = L"自定义";
        row.open = IsExpanded(app, path, false);
        row.rect = {app->layout.sidebar.left, y, app->layout.sidebar.right, y + Scale(kTreeRowH)};
        y += Scale(kTreeRowH);
        app->rows.push_back(row);

        if (row.open) {
            TreeRow add;
            add.kind = RowKind::AddCustom;
            add.icon = RowIcon::Plus;
            add.depth = 1;
            add.label = L"新建自定义设备";
            add.tag = L"宽度/高度自选";
            add.rect = {app->layout.sidebar.left, y, app->layout.sidebar.right, y + Scale(kTreeRowH)};
            y += Scale(kTreeRowH);
            app->rows.push_back(add);

            const std::wstring minePath = path + L"/mine";
            TreeRow mine;
            mine.kind = RowKind::MyDevices;
            mine.icon = RowIcon::Folder;
            mine.depth = 1;
            mine.path = minePath;
            mine.label = L"我的设备";
            mine.tag = std::to_wstring(app->state.archive.size());
            mine.open = IsExpanded(app, minePath, false);
            mine.rect = {app->layout.sidebar.left, y, app->layout.sidebar.right, y + Scale(kTreeRowH)};
            y += Scale(kTreeRowH);
            app->rows.push_back(mine);

            if (mine.open) {
                if (app->state.archive.empty()) {
                    TreeRow empty;
                    empty.kind = RowKind::Empty;
                    empty.depth = 2;
                    empty.label = L"（暂无存档设备）";
                    empty.rect = {app->layout.sidebar.left, y, app->layout.sidebar.right,
                                  y + Scale(kTreeRowH)};
                    y += Scale(kTreeRowH);
                    app->rows.push_back(empty);
                } else {
                    for (size_t i = 0; i < app->state.archive.size(); ++i) {
                        const Archived& a = app->state.archive[i];
                        TreeRow item;
                        item.kind = RowKind::Archive;
                        item.depth = 2;
                        item.label = a.name;
                        item.tag = rack::WidthText(a.w) + L" · " + std::to_wstring(a.hU) + L"U";
                        item.archiveIndex = static_cast<int>(i);
                        item.rect = {app->layout.sidebar.left, y, app->layout.sidebar.right,
                                     y + Scale(kTreeRowH)};
                        y += Scale(kTreeRowH);
                        app->rows.push_back(item);
                    }
                }
            }
        }
    }
    app->sidebarContentH = y - app->layout.sidebar.top + Scale(kSidePad);
}

/* ================================================================
 * 机柜几何（屏幕坐标）
 * ================================================================ */
RackMetrics MetricsOf(const App* app) { return ScreenMetrics(app->state); }

// 屏幕缩放 = 界面缩放 × DPI 比。机柜在逻辑单位下作画，由 Graphics 变换负责放大，
// 因此 Logical 单位始终是 96 DPI 设计值（与 HTML 对照版 CSS px 同口径）。
float ScreenScale(const App* app) {
    return app->state.zoom / 100.0f * (app::WindowDpi() / 96.0f);
}

Gdiplus::PointF FrameOrigin(const App* app) {
    const RackMetrics m = MetricsOf(app);
    const float scale = ScreenScale(app);
    const float centerX = app->layout.stage.left + (app->layout.stage.right - app->layout.stage.left) / 2.0f;
    const float top = static_cast<float>(app->layout.stage.top) + ScaleF(kStagePadTop) -
                      static_cast<float>(app->stageScroll);
    return Gdiplus::PointF(centerX - m.frameW * scale / 2.0f, top);
}

bool PointInRack(const App* app, POINT pt) {
    const RackMetrics m = MetricsOf(app);
    const float scale = ScreenScale(app);
    const Gdiplus::PointF origin = FrameOrigin(app);
    const Gdiplus::RectF frame = FrameRectOf(m, origin, scale);
    return pt.x >= frame.X && pt.x <= frame.GetRight() && pt.y >= frame.Y && pt.y <= frame.GetBottom();
}

void ClampScroll(App* app) {
    const RackMetrics m = MetricsOf(app);
    const float scale = ScreenScale(app);
    const int viewport = app->layout.stage.bottom - app->layout.stage.top - Scale(kStagePadTop) -
                         Scale(kStagePadBottom);
    const int content = static_cast<int>(m.frameH * scale);
    const int maxScroll = std::max(0, content - viewport);
    if (app->stageScroll > maxScroll) app->stageScroll = maxScroll;
    if (app->stageScroll < 0) app->stageScroll = 0;

    const int sideViewport = app->layout.sidebar.bottom - app->layout.sidebar.top;
    const int sideMax = std::max(0, app->sidebarContentH - sideViewport);
    if (app->sidebarScroll > sideMax) app->sidebarScroll = sideMax;
    if (app->sidebarScroll < 0) app->sidebarScroll = 0;
}

/* ================================================================
 * 侧栏置顶栏与滚动条几何
 * ================================================================ */
void InvalidateAll(App* app);            // 定义在后面（提示条一节）
void DrawSidebarScrollbar(Gdiplus::Graphics& g, App* app);   // 定义在后面（侧栏绘制一节）

/** 置顶栏（「设备目录」标题）的下边界：列表区从这里开始，滚动内容不从标题上方穿过 */
int SidebarTitleBottom(App* app) {
    return app->layout.sidebar.top + Scale(kSidePad) + Scale(20);
}

struct SidebarScrollbar {
    bool visible = false;
    Gdiplus::RectF track;
    Gdiplus::RectF thumb;
};

SidebarScrollbar SidebarScrollbarOf(App* app) {
    SidebarScrollbar bar;
    const Layout& L = app->layout;
    const int top = SidebarTitleBottom(app) + Scale(6);
    const int bottom = L.sidebar.bottom - Scale(8);
    const int viewport = L.sidebar.bottom - L.sidebar.top;
    const int content = app->sidebarContentH;
    if (content <= viewport || bottom - top < Scale(24)) return bar;   // 内容放得下就不画

    const float trackX = static_cast<float>(L.sidebar.right - Scale(11));
    const float trackW = ScaleF(6.0f);
    bar.visible = true;
    bar.track = Gdiplus::RectF(trackX, static_cast<float>(top), trackW,
                               static_cast<float>(bottom - top));

    const float trackH = bar.track.Height;
    float thumbH = trackH * static_cast<float>(viewport) / static_cast<float>(content);
    if (thumbH < ScaleF(28.0f)) thumbH = ScaleF(28.0f);
    const int maxScroll = content - viewport;
    const float t = maxScroll > 0 ? static_cast<float>(app->sidebarScroll) / maxScroll : 0.0f;
    bar.thumb = Gdiplus::RectF(trackX, bar.track.Y + (trackH - thumbH) * t, trackW, thumbH);
    return bar;
}

bool IsOnSidebarScrollbar(App* app, POINT pt) {
    const SidebarScrollbar bar = SidebarScrollbarOf(app);
    if (!bar.visible) return false;
    const float slack = ScaleF(4.0f);
    return static_cast<float>(pt.x) >= bar.track.X - slack &&
           static_cast<float>(pt.x) <= bar.track.GetRight() + slack &&
           static_cast<float>(pt.y) >= bar.track.Y - slack &&
           static_cast<float>(pt.y) <= bar.track.GetBottom() + slack;
}

/** 由滑块位置反算滚动量（拖动滚动条用） */
void SetSidebarScrollByThumb(App* app, float thumbTop) {
    const SidebarScrollbar bar = SidebarScrollbarOf(app);
    if (!bar.visible) return;
    const float span = bar.track.Height - bar.thumb.Height;
    float t = span > 0.5f ? (thumbTop - bar.track.Y) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int viewport = app->layout.sidebar.bottom - app->layout.sidebar.top;
    const int maxScroll = app->sidebarContentH - viewport;
    app->sidebarScroll = static_cast<int>(t * maxScroll + 0.5f);
    ClampScroll(app);
    InvalidateAll(app);
}


/* ================================================================
 * 提示条 / 状态
 * ================================================================ */
void ShowToast(App* app, const std::wstring& text) {
    app->toast = text;
    app->toastUntil = GetTickCount64() + 2000;
    // 提示条只在显示期间驱动重绘，避免空转
    SetTimer(app->hwnd, 1, 100, nullptr);
    InvalidateRect(app->hwnd, nullptr, FALSE);
}

void UpdateHint(App* app) {
    if (!app->session.active) {
        app->hint.clear();
        return;
    }
    if (app->session.pending) {
        app->hint = L"移动光标到机柜卡位，点击落位 · Esc 取消";
    } else if (app->session.id == 0) {
        app->hint = L"松开鼠标放置到高亮卡位（绿框可放 / 红框冲突）· 拖出机柜松手取消";
    } else {
        app->hint = L"松开鼠标放置到高亮卡位（绿框可放 / 红框冲突）· 拖出机柜松手移出";
    }
}

void InvalidateAll(App* app) { InvalidateRect(app->hwnd, nullptr, FALSE); }

/* ================================================================
 * 会话
 * ================================================================ */
void ResetGhostCache(App* app) {
    if (app->ghostCache != nullptr) {
        delete app->ghostCache;
        app->ghostCache = nullptr;
    }
    app->ghostCacheKey.clear();
}

void CancelSession(App* app) {
    if (!app->session.active) return;
    app->session = Session();
    ResetGhostCache(app);
    if (GetCapture() == app->hwnd) ReleaseCapture();
    UpdateHint(app);
    InvalidateAll(app);
}

void BeginSession(App* app, const Device& dev, int id, POINT pt, bool pending) {
    CancelSession(app);
    app->state.selectedId = 0;
    Session session;
    session.active = true;
    session.dev = dev;
    session.id = id;
    session.pending = pending;
    session.start = pt;
    app->session = session;
    // 抓取鼠标：拖到窗口外也能收到移动与松手消息（对应 HTML 对照版的 pointer capture）
    SetCapture(app->hwnd);
    UpdateHint(app);
    InvalidateAll(app);
}

void UpdateSnap(App* app, POINT pt) {
    Session& s = app->session;
    if (!s.active) return;
    const RackMetrics m = MetricsOf(app);
    const float scale = ScreenScale(app);
    const Gdiplus::PointF origin = FrameOrigin(app);

    if (PointInRack(app, pt)) {
        const rack::InnerRect inner = InnerRectOf(app->state, m, origin, scale);
        s.snap = rack::CalcSnap(app->state, s.dev, inner, pt.x, pt.y, s.id);
        s.hasSnap = s.snap.valid;
    } else {
        s.hasSnap = false;
    }
    InvalidateAll(app);
}

/* ================================================================
 * 数据操作
 * ================================================================ */
void RemoveDevice(App* app, int id, bool withToast) {
    Device removed;
    bool found = false;
    std::vector<Device> keep;
    for (const Device& d : app->state.devices) {
        if (d.id == id) {
            removed = d;
            found = true;
            continue;
        }
        keep.push_back(d);
    }
    if (!found) return;
    app->state.devices = keep;
    if (app->state.selectedId == id) app->state.selectedId = 0;
    InvalidateAll(app);
    if (withToast) ShowToast(app, L"已将「" + removed.name + L"」移出机柜");
}

void CommitPlace(App* app) {
    Session& s = app->session;
    if (!s.hasSnap || !s.snap.ok) return;
    if (s.id > 0) {
        for (Device& d : app->state.devices) {
            if (d.id == s.id) {
                d.col = s.snap.col;
                d.hole = s.snap.hole;
                break;
            }
        }
        app->state.selectedId = s.id;
    } else {
        Device d = s.dev;
        d.id = app->state.seq++;
        d.col = s.snap.col;
        d.hole = s.snap.hole;
        app->state.devices.push_back(d);
        app->state.selectedId = d.id;
    }
    app->session = Session();
    ResetGhostCache(app);
    UpdateHint(app);
    InvalidateAll(app);
}

void SwitchRack(App* app, RackKind kind) {
    if (kind == app->state.rack) return;
    const int capHoles = rack::TotalHoles(kind);
    std::vector<Device> keep;
    for (const Device& d : app->state.devices) {
        if (d.hole + d.hU * rack::kHolesPerU <= capHoles) keep.push_back(d);
    }
    const int removed = static_cast<int>(app->state.devices.size() - keep.size());
    if (removed > 0) {
        const std::wstring text =
            std::wstring(L"「") + rack::RackText(kind) + L"」机柜装不下当前布设：将移除 " +
            std::to_wstring(removed) + L" 台超出高度的设备。\n是否继续切换？（取消则保持 " +
            rack::RackText(app->state.rack) + L"）";
        if (!Confirm(app->hwnd, text, L"切换机柜")) return;
    }
    CancelSession(app);
    app->state.rack = kind;
    app->state.devices = keep;
    bool selectedAlive = false;
    for (const Device& d : app->state.devices) {
        if (d.id == app->state.selectedId) selectedAlive = true;
    }
    if (!selectedAlive) app->state.selectedId = 0;
    app->stageScroll = 0;
    ClampScroll(app);
    InvalidateAll(app);
    if (removed > 0) {
        ShowToast(app, std::wstring(L"已切换为 ") + rack::RackText(kind) + L"，移除 " +
                           std::to_wstring(removed) + L" 台超出的设备");
    }
}

void SetMode(App* app, SnapMode mode) {
    if (app->state.mode == mode) return;
    app->state.mode = mode;
    InvalidateAll(app);
}

void ApplyZoom(App* app, int zoom) {
    zoom = rack::ClampInt(zoom, rack::kZoomMin, rack::kZoomMax);
    if (zoom == app->state.zoom) return;
    app->state.zoom = zoom;
    CancelSession(app);
    ClampScroll(app);
    InvalidateAll(app);
}

void ClearRack(App* app) {
    if (app->state.devices.empty()) {
        ShowToast(app, L"机柜已经是空的");
        return;
    }
    if (!Confirm(app->hwnd, L"确定清空机柜内的所有设备？", L"清空机柜")) return;
    CancelSession(app);
    app->state.devices.clear();
    app->state.selectedId = 0;
    InvalidateAll(app);
    ShowToast(app, L"已清空机柜");
}

/* ---------------- 存档 ---------------- */
void SaveArchiveWithToast(App* app) {
    if (!rack::SaveArchive(app->state.archive)) {
        ShowToast(app, L"存档写入失败：无法写入用户目录");
    }
}

void DeleteArchived(App* app, int index) {
    if (index < 0 || static_cast<size_t>(index) >= app->state.archive.size()) return;
    const Archived item = app->state.archive[static_cast<size_t>(index)];
    if (!Confirm(app->hwnd, L"确定从「我的设备」中删除「" + item.name + L"」？", L"删除存档"))
        return;
    app->state.archive.erase(app->state.archive.begin() + index);
    SaveArchiveWithToast(app);
    BuildRows(app);
    InvalidateAll(app);
    ShowToast(app, L"已删除存档设备「" + item.name + L"」");
}

void RenameArchived(App* app, int index) {
    if (index < 0 || static_cast<size_t>(index) >= app->state.archive.size()) return;
    Archived& item = app->state.archive[static_cast<size_t>(index)];
    std::wstring name;
    if (!PromptText(app->hwnd, L"重命名存档设备", L"新名称（最长 12 字符）", item.name,
                    rack::kNameMax, &name)) {
        return;
    }
    const std::wstring trimmed = rack::TrimName(name, L"");
    if (trimmed.empty()) {
        ShowToast(app, L"名称不能为空");
        return;
    }
    if (trimmed == item.name) return;
    for (size_t i = 0; i < app->state.archive.size(); ++i) {
        if (static_cast<int>(i) != index && app->state.archive[i].name == trimmed) {
            ShowToast(app, L"已存在同名存档设备「" + trimmed + L"」");
            return;
        }
    }
    item.name = trimmed;
    SaveArchiveWithToast(app);
    BuildRows(app);
    InvalidateAll(app);
    ShowToast(app, L"已重命名为「" + trimmed + L"」");
}

void CopyArchived(App* app, int index) {
    if (index < 0 || static_cast<size_t>(index) >= app->state.archive.size()) return;
    Archived copy = app->state.archive[static_cast<size_t>(index)];
    copy.id = rack::MakeArchivedId();
    copy.name = rack::CopyNameOf(app->state.archive, copy.name);
    copy.createdAt = rack::IsoNow();
    app->state.archive.push_back(copy);
    SaveArchiveWithToast(app);
    BuildRows(app);
    InvalidateAll(app);
    ShowToast(app, L"已复制为「" + copy.name + L"」");
}

void OpenArchiveMenu(App* app, int index, POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"重命名");
    AppendMenuW(menu, MF_STRING, 2, L"复制");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"删除");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x,
                                       screenPt.y, 0, app->hwnd, nullptr);
    DestroyMenu(menu);
    if (command == 1) RenameArchived(app, index);
    else if (command == 2) CopyArchived(app, index);
    else if (command == 3) DeleteArchived(app, index);
}

/* ---------------- 自定义设备（F-07 + F-10） ---------------- */
void OpenCustomDeviceDialog(App* app) {
    CustomDeviceInput input;
    input.name.clear();
    input.w = 6;
    input.hU = 1;
    input.saveToArchive = false;
    if (!ShowCustomDeviceDialog(app->hwnd, &input)) return;

    if (input.saveToArchive) {
        Archived item;
        item.id = rack::MakeArchivedId();
        item.name = input.name;
        item.w = input.w;
        item.hU = input.hU;
        item.createdAt = rack::IsoNow();
        const bool added = rack::UpsertArchive(&app->state.archive, item);
        SaveArchiveWithToast(app);
        BuildRows(app);
        ShowToast(app, added ? (L"「" + input.name + L"」已保存到我的设备")
                             : (L"已更新存档设备「" + input.name + L"」"));
    }

    Device dev;
    dev.cat = Category::Custom;
    dev.name = input.name;
    dev.w = input.w;
    dev.hU = input.hU;
    POINT pt = {app->layout.stage.left + (app->layout.stage.right - app->layout.stage.left) / 2,
                app->layout.stage.top + Scale(80)};
    BeginSession(app, dev, 0, pt, true);
}

/* ================================================================
 * 文件操作
 * ================================================================ */
void DoSaveImage(App* app) {
    Gdiplus::Bitmap* bitmap = RenderLayoutBitmap(app->state);
    if (bitmap == nullptr) {
        ShowToast(app, L"图片生成失败，请重试");
        return;
    }
    const std::wstring defaultName = L"机柜布局图_" + rack::FileStamp() + L".png";
    std::wstring path;
    const bool picked = PickSavePath(app->hwnd, defaultName, L"图片文件", L"*.png;*.jpg;*.jpeg",
                                     L"png", &path);
    if (!picked) {
        delete bitmap;
        return;
    }
    std::wstring lower = path;
    for (wchar_t& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    const bool jpeg = lower.size() > 4 && (lower.rfind(L".jpg") == lower.size() - 4 ||
                                          lower.rfind(L".jpeg") == lower.size() - 5);
    const bool ok = jpeg ? SaveBitmapAsJpeg(path, bitmap, 0.92f) : SaveBitmapAsPng(path, bitmap);
    const int width = static_cast<int>(bitmap->GetWidth());
    const int height = static_cast<int>(bitmap->GetHeight());
    delete bitmap;

    if (!ok) {
        ShowToast(app, L"写入文件失败，请换一个位置重试");
        return;
    }
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    ShowToast(app, L"布局图已保存：" + name + L"（" + std::to_wstring(width) + L"×" +
                       std::to_wstring(height) + L"）");
    InvalidateAll(app);
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    const std::string bytes = util::WideToUtf8(text);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                              nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

bool ReadUtf8File(const std::wstring& path, std::wstring* text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string bytes;
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        bytes.append(buffer, read);
    }
    CloseHandle(file);
    *text = util::DecodeFileBytes(bytes);
    return true;
}

void DoExportLayout(App* app) {
    CancelSession(app);
    const std::wstring markdown = rack::BuildLayoutMarkdown(app->state);
    const std::wstring defaultName = rack::MarkdownFileName(app->state);
    std::wstring path;
    if (!PickSavePath(app->hwnd, defaultName, L"Markdown 布设文件", L"*.md", L"md", &path)) return;
    if (!WriteUtf8File(path, markdown)) {
        ShowToast(app, L"写入文件失败，请换一个位置重试");
        return;
    }
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    ShowToast(app, L"布设已导出：" + name);
    InvalidateAll(app);
}

void ApplyState(App* app, const State& next) {
    app->state.rack = next.rack;
    app->state.mode = next.mode;
    app->state.devices = next.devices;
    app->state.seq = next.seq;
    app->state.selectedId = 0;
    BuildRows(app);
    ClampScroll(app);
    InvalidateAll(app);
}

void DoImportLayout(App* app) {
    std::wstring path;
    if (!PickOpenPath(app->hwnd, L"Markdown 布设文件", L"*.md", L"md", &path)) return;

    std::wstring text;
    if (!ReadUtf8File(path, &text)) {
        ShowError(app->hwnd, L"文件读取失败");
        return;
    }

    rack::ImportResult result = rack::ImportLayout(text, app->state);
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);

    if (!result.ok) {
        std::vector<rack::ReportRow> rows;
        rows.push_back({rack::ReportKind::Error, result.error});
        ShowImportReportDialog(app->hwnd, L"导入失败 — " + name, rows, false);
        return;
    }

    CancelSession(app);
    app->backup = app->state;
    app->hasBackup = true;
    ApplyState(app, result.next);

    const ReportAction action =
        ShowImportReportDialog(app->hwnd, L"导入完成 — " + name, result.rows, result.canUndo);
    if (action == ReportAction::Undo && app->hasBackup) {
        ApplyState(app, app->backup);
        ShowToast(app, L"已撤销导入，恢复导入前的布设");
    }
}

/* ================================================================
 * 绘制
 * ================================================================ */
void DrawButton(Gdiplus::Graphics& g, const RECT& rc, const std::wstring& text, bool primary,
                bool danger, bool enabled, bool hovered) {
    const Gdiplus::RectF box(static_cast<float>(rc.left), static_cast<float>(rc.top),
                             static_cast<float>(rc.right - rc.left),
                             static_cast<float>(rc.bottom - rc.top));
    const uint32_t fill = primary ? (hovered ? kPrimaryDark : kPrimary) : (hovered ? kHoverGray : kTopbarBg);
    FillRounded(g, box, ScaleF(6.0f), fill);
    if (!primary) {
        StrokeRounded(g, Gdiplus::RectF(box.X + 0.5f, box.Y + 0.5f, box.Width - 1.0f, box.Height - 1.0f),
                      ScaleF(6.0f), kBorder);
    }
    const uint32_t textColor = !enabled ? kTextFaint : (primary ? 0xffffff : (danger ? kDangerText : kTextMain));
    TextCenter(g, text, Font(ScaleF(13.0f)), box, textColor);
}

void DrawSegment(Gdiplus::Graphics& g, const App* app, const RECT& rc, const std::wstring& text,
                 bool active, int id) {
    const Gdiplus::RectF box(static_cast<float>(rc.left), static_cast<float>(rc.top),
                             static_cast<float>(rc.right - rc.left),
                             static_cast<float>(rc.bottom - rc.top));
    const bool hovered = app->hoverControl == id;
    FillSolid(g, box, active ? kPrimary : (hovered ? kHoverGray : kTopbarBg));
    TextCenter(g, text, Font(ScaleF(13.0f)), box, active ? 0xffffff : kTextCtl);
}

void DrawTopbar(Gdiplus::Graphics& g, App* app) {
    const Layout& L = app->layout;
    FillSolid(g, Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(L.client.right),
                                static_cast<float>(L.topbar.bottom)), kTopbarBg);
    StrokeLine(g, 0.0f, static_cast<float>(L.topbar.bottom) - 0.5f,
               static_cast<float>(L.client.right), static_cast<float>(L.topbar.bottom) - 0.5f,
               0x000000, 1.0f, 20);

    const int padX = Scale(kTopbarPadX);
    const float baseline = static_cast<float>(Scale(kTopPad + kRow1H / 2 + 2));

    // 品牌图标：迷你机柜（自绘，替代 HTML 对照版的 🗄️ 表情）
    {
        const float iconW = ScaleF(16.0f), iconH = ScaleF(18.0f);
        const float iconX = static_cast<float>(padX), iconY = baseline - iconH / 2.0f;
        FillRounded(g, Gdiplus::RectF(iconX, iconY, iconW, iconH), ScaleF(3.0f), kPrimary);
        for (int i = 0; i < 3; ++i) {
            Gdiplus::RectF slot(iconX + ScaleF(3.0f), iconY + ScaleF(3.0f) + i * ScaleF(4.5f),
                                iconW - ScaleF(6.0f), ScaleF(2.0f));
            FillRounded(g, slot, ScaleF(0.5f), 0xffffff, 216);
        }
    }

    const float nameX = static_cast<float>(padX) + ScaleF(24.0f);
    TextMiddleLeft(g, APP_NAME_CN, Font(ScaleF(16.0f), true), nameX, baseline, kTextMain);
    const float nameW = TextWidth(g, APP_NAME_CN, Font(ScaleF(16.0f), true));
    const float badgeX = nameX + nameW + ScaleF(8.0f);
    const float badgeW = ScaleF(48.0f), badgeH = ScaleF(18.0f);
    Gdiplus::RectF badge(badgeX, baseline - badgeH / 2.0f, badgeW, badgeH);
    FillRounded(g, badge, badgeH / 2.0f, kPanelSoft);
    StrokeRounded(g, badge, badgeH / 2.0f, kBorderSoft);
    TextCenter(g, APP_VERSION_TAG, Font(ScaleF(11.0f)), badge, kTextDim);

    DrawButton(g, L.updateBtn, L"检查更新", false, false, true, app->hoverControl == kCtlUpdate);

    // 第二行：机柜下拉
    {
        const RECT rc = L.rackBtn;
        const Gdiplus::RectF box(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                 static_cast<float>(rc.right - rc.left),
                                 static_cast<float>(rc.bottom - rc.top));
        FillRounded(g, box, ScaleF(6.0f), app->hoverControl == kCtlRack ? kHoverGray : kTopbarBg);
        StrokeRounded(g, Gdiplus::RectF(box.X + 0.5f, box.Y + 0.5f, box.Width - 1.0f, box.Height - 1.0f),
                      ScaleF(6.0f), kBorder);
        const std::wstring text = std::wstring(rack::RackText(app->state.rack)) + L" 标准机柜";
        TextMiddleLeft(g, text, Font(ScaleF(13.0f)), box.X + ScaleF(8.0f), box.Y + box.Height / 2.0f,
                       kTextMain, box.Width - ScaleF(30.0f));
        const float arrowX = box.GetRight() - ScaleF(14.0f);
        const float arrowY = box.Y + box.Height / 2.0f;
        Gdiplus::PointF points[3] = {Gdiplus::PointF(arrowX - ScaleF(4.0f), arrowY - ScaleF(2.0f)),
                                     Gdiplus::PointF(arrowX + ScaleF(4.0f), arrowY - ScaleF(2.0f)),
                                     Gdiplus::PointF(arrowX, arrowY + ScaleF(3.0f))};
        Gdiplus::SolidBrush brush(Col(kTextCtl));
        g.FillPolygon(&brush, points, 3);
    }

    // 版式切换（两段一体）
    {
        const Gdiplus::RectF seg(static_cast<float>(L.modeU.left), static_cast<float>(L.modeU.top),
                                 static_cast<float>(L.modeHole.right - L.modeU.left),
                                 static_cast<float>(L.modeU.bottom - L.modeU.top));
        FillRounded(g, seg, ScaleF(8.0f), kTopbarBg);
        StrokeRounded(g, Gdiplus::RectF(seg.X + 0.5f, seg.Y + 0.5f, seg.Width - 1.0f, seg.Height - 1.0f),
                      ScaleF(8.0f), kBorder);
        Gdiplus::GraphicsPath clip;
        AddRoundedRect(&clip, seg, ScaleF(8.0f));
        const Gdiplus::GraphicsState state = g.Save();
        g.SetClip(&clip);
        DrawSegment(g, app, L.modeU, L"标准 U 数版式", app->state.mode == SnapMode::U, kCtlModeU);
        DrawSegment(g, app, L.modeHole, L"螺丝孔位版式", app->state.mode == SnapMode::Hole,
                    kCtlModeHole);
        g.Restore(state);
    }

    DrawButton(g, L.clearBtn, L"清空机柜", false, true, true, app->hoverControl == kCtlClear);
    DrawButton(g, L.saveBtn, L"保存图片", true, false, true, app->hoverControl == kCtlSave);
    DrawButton(g, L.exportBtn, L"导出布设", false, false, true, app->hoverControl == kCtlExport);
    DrawButton(g, L.importBtn, L"导入布设", false, false, true, app->hoverControl == kCtlImport);

    // 缩放控件
    {
        const RECT minus = L.zoomMinus;
        const RECT plus = L.zoomPlus;
        const Gdiplus::RectF container(static_cast<float>(minus.left) - ScaleF(6.0f),
                                       static_cast<float>(minus.top),
                                       static_cast<float>(L.zoomReset.right - minus.left) + ScaleF(12.0f),
                                       static_cast<float>(minus.bottom - minus.top));
        FillRounded(g, container, ScaleF(8.0f), kTopbarBg);
        StrokeRounded(g, Gdiplus::RectF(container.X + 0.5f, container.Y + 0.5f, container.Width - 1.0f,
                                        container.Height - 1.0f), ScaleF(8.0f), kBorder);

        const float cy = container.Y + container.Height / 2.0f;
        TextMiddleCenter(g, L"−", Font(ScaleF(15.0f)), minus.left + ScaleF(9.0f), cy, kTextCtl);
        TextMiddleCenter(g, L"＋", Font(ScaleF(14.0f)), plus.left + ScaleF(9.0f), cy, kTextCtl);

        // 轨道 + 滑块
        const Gdiplus::RectF track(static_cast<float>(L.zoomTrack.left),
                                   static_cast<float>(L.zoomTrack.top),
                                   static_cast<float>(L.zoomTrack.right - L.zoomTrack.left),
                                   ScaleF(4.0f));
        FillRounded(g, track, track.Height / 2.0f, kBorder);
        const float ratio = (app->state.zoom - rack::kZoomMin) /
                            static_cast<float>(rack::kZoomMax - rack::kZoomMin);
        const float thumbX = track.X + ratio * track.Width;
        Gdiplus::SolidBrush thumb(Col(kPrimary));
        g.FillEllipse(&thumb, thumbX - ScaleF(7.0f), track.Y + track.Height / 2.0f - ScaleF(7.0f),
                      ScaleF(14.0f), ScaleF(14.0f));
        Gdiplus::Pen thumbEdge(Col(0xffffff), ScaleF(2.0f));
        g.DrawEllipse(&thumbEdge, thumbX - ScaleF(7.0f), track.Y + track.Height / 2.0f - ScaleF(7.0f),
                      ScaleF(14.0f), ScaleF(14.0f));

        const float valueRight = static_cast<float>(L.zoomReset.left) - ScaleF(10.0f);
        TextMiddleRight(g, std::to_wstring(app->state.zoom) + L"%", Font(ScaleF(12.0f)), valueRight,
                        cy, kTextMain, ScaleF(46.0f));

        const Gdiplus::RectF reset(static_cast<float>(L.zoomReset.left),
                                   static_cast<float>(L.zoomReset.top),
                                   static_cast<float>(L.zoomReset.right - L.zoomReset.left),
                                   static_cast<float>(L.zoomReset.bottom - L.zoomReset.top));
        FillRounded(g, reset, ScaleF(6.0f), app->hoverControl == kCtlZoomReset ? kHoverGray : kTopbarBg);
        StrokeRounded(g, Gdiplus::RectF(reset.X + 0.5f, reset.Y + 0.5f, reset.Width - 1.0f, reset.Height - 1.0f),
                      ScaleF(6.0f), kBorder);
        TextCenter(g, L"重置", Font(ScaleF(12.0f)), reset, kTextMain);
    }

    // 统计
    {
        const int used = rack::UsedU(app->state);
        const std::wstring stats = L"已放置 " + std::to_wstring(app->state.devices.size()) +
                                   L" 台设备 · 高度合计 " + std::to_wstring(used) + L"U / " +
                                   std::to_wstring(rack::RackU(app->state.rack)) + L"U";
        const Gdiplus::RectF box(static_cast<float>(L.stats.left), static_cast<float>(L.stats.top),
                                 static_cast<float>(L.stats.right - L.stats.left),
                                 static_cast<float>(L.stats.bottom - L.stats.top));
        TextRight(g, stats, Font(ScaleF(12.0f)), box, kTextDim);
    }
}

/* 行首小图标（自绘）：微软雅黑没有 Emoji 字形，用矢量图形代替，避免渲染成方框 */
void DrawFolderIcon(Gdiplus::Graphics& g, float x, float cy, uint32_t color) {
    FillRounded(g, Gdiplus::RectF(x, cy - ScaleF(5.0f), ScaleF(5.0f), ScaleF(4.0f)), ScaleF(1.0f), color);
    FillRounded(g, Gdiplus::RectF(x, cy - ScaleF(3.0f), ScaleF(12.0f), ScaleF(8.0f)), ScaleF(1.5f), color);
}

void DrawPlusIcon(Gdiplus::Graphics& g, float x, float cy, uint32_t color) {
    Gdiplus::Pen pen(Col(color), ScaleF(1.6f));
    g.DrawLine(&pen, x + ScaleF(6.0f), cy - ScaleF(4.0f), x + ScaleF(6.0f), cy + ScaleF(4.0f));
    g.DrawLine(&pen, x + ScaleF(2.0f), cy, x + ScaleF(10.0f), cy);
}

void DrawTreeRows(Gdiplus::Graphics& g, App* app) {
    const Layout& L = app->layout;
    for (size_t i = 0; i < app->rows.size(); ++i) {
        const TreeRow& row = app->rows[i];
        RECT rc = row.rect;
        rc.top -= app->sidebarScroll;
        rc.bottom -= app->sidebarScroll;
        if (rc.bottom < L.sidebar.top || rc.top > L.sidebar.bottom) continue;

        const bool hovered = static_cast<int>(i) == app->hoverRow &&
                             (row.kind == RowKind::Leaf || row.kind == RowKind::Archive ||
                              row.kind == RowKind::AddCustom);
        const int indent = Scale(kSidePad) + row.depth * Scale(kTreeIndent);
        const Gdiplus::RectF box(static_cast<float>(L.sidebar.left + indent), static_cast<float>(rc.top),
                                 static_cast<float>(L.sidebar.right - indent - Scale(kSidePad)),
                                 static_cast<float>(rc.bottom - rc.top));
        const float cy = box.Y + box.Height / 2.0f;

        if (row.kind == RowKind::Group || row.kind == RowKind::MyDevices) {
            if (hovered) FillRounded(g, box, ScaleF(6.0f), kHoverGray);
            const float arrowX = box.X + ScaleF(5.0f);
            Gdiplus::PointF arrow[3] = {
                Gdiplus::PointF(arrowX - ScaleF(3.0f), cy - ScaleF(4.0f)),
                Gdiplus::PointF(arrowX + ScaleF(3.0f), cy),
                Gdiplus::PointF(arrowX - ScaleF(3.0f), cy + ScaleF(4.0f))};
            if (row.open) {
                Gdiplus::PointF down[3] = {Gdiplus::PointF(arrowX - ScaleF(4.0f), cy - ScaleF(2.0f)),
                                           Gdiplus::PointF(arrowX + ScaleF(4.0f), cy - ScaleF(2.0f)),
                                           Gdiplus::PointF(arrowX, cy + ScaleF(3.0f))};
                Gdiplus::SolidBrush brush(Col(kTextFaint));
                g.FillPolygon(&brush, down, 3);
            } else {
                Gdiplus::SolidBrush brush(Col(kTextFaint));
                g.FillPolygon(&brush, arrow, 3);
            }
            TextMiddleLeft(g, row.label, Font(ScaleF(13.0f), true), box.X + ScaleF(32.0f), cy,
                           kTextMain, box.Width - ScaleF(70.0f));
            if (row.icon == RowIcon::Folder) DrawFolderIcon(g, box.X + ScaleF(16.0f), cy, kTextFaint);
            if (!row.tag.empty()) {
                Gdiplus::Font* tagFont = Font(ScaleF(10.0f));
                const float tagW = TextWidth(g, row.tag, tagFont) + ScaleF(8.0f);
                Gdiplus::RectF tagBox(box.GetRight() - ScaleF(16.0f) - tagW, cy - ScaleF(9.0f), tagW,
                                      ScaleF(18.0f));
                FillRounded(g, tagBox, ScaleF(4.0f), kTopbarBg);
                StrokeRounded(g, Gdiplus::RectF(tagBox.X + 0.5f, tagBox.Y + 0.5f, tagBox.Width - 1.0f,
                                                tagBox.Height - 1.0f), ScaleF(4.0f), kBorderSoft);
                TextCenter(g, row.tag, tagFont, tagBox, kTextFaint);
            }
            continue;
        }

        if (row.kind == RowKind::Empty) {
            TextMiddleLeft(g, row.label, Font(ScaleF(12.0f)), box.X + ScaleF(16.0f), cy, kTextFaint);
            continue;
        }

        if (hovered) FillRounded(g, box, ScaleF(6.0f), kHoverBlue);

        if (row.icon == RowIcon::Plus) {
            // 「新建自定义设备」：自绘加号（替代 HTML 对照版的 ➕ 字符）
            DrawPlusIcon(g, box.X + ScaleF(16.0f), cy, kTextFaint);
        } else {
            // 色块
            const Category chipCat = (row.kind == RowKind::Archive) ? Category::Custom : row.leaf.cat;
            const rack::CatTheme theme = rack::CatThemeOf(chipCat);
            FillRounded(g, Gdiplus::RectF(box.X + ScaleF(16.0f), cy - ScaleF(4.5f), ScaleF(9.0f),
                                          ScaleF(9.0f)), ScaleF(2.0f), theme.chip);
        }

        const std::wstring label = (row.kind == RowKind::Leaf) ? row.leaf.label : row.label;
        TextMiddleLeft(g, label, Font(ScaleF(13.0f)), box.X + ScaleF(31.0f), cy, kTextMain,
                       box.Width - ScaleF(110.0f));

        // 右侧标签
        if (!row.tag.empty()) {
            Gdiplus::Font* tagFont = Font(ScaleF(10.0f));
            const float tagW = TextWidth(g, row.tag, tagFont) + ScaleF(8.0f);
            const float tagRight = box.GetRight() - (row.kind == RowKind::Archive ? ScaleF(22.0f) : 0.0f);
            Gdiplus::RectF tagBox(tagRight - tagW, cy - ScaleF(9.0f), tagW, ScaleF(18.0f));
            FillRounded(g, tagBox, ScaleF(4.0f), kTopbarBg);
            StrokeRounded(g, Gdiplus::RectF(tagBox.X + 0.5f, tagBox.Y + 0.5f, tagBox.Width - 1.0f,
                                            tagBox.Height - 1.0f), ScaleF(4.0f), kBorderSoft);
            TextCenter(g, row.tag, tagFont, tagBox, kTextFaint);
        }

        // 存档条目：悬停时显示删除 ×
        if (row.kind == RowKind::Archive && hovered) {
            const Gdiplus::RectF x(box.GetRight() - ScaleF(18.0f), cy - ScaleF(8.0f), ScaleF(16.0f),
                                   ScaleF(16.0f));
            Gdiplus::SolidBrush brush(Col(0xe53e3e));
            g.FillEllipse(&brush, x);
            Gdiplus::Pen pen(Col(0xffffff), ScaleF(1.4f));
            g.DrawLine(&pen, x.X + ScaleF(5.0f), x.Y + ScaleF(5.0f), x.GetRight() - ScaleF(5.0f),
                       x.GetBottom() - ScaleF(5.0f));
            g.DrawLine(&pen, x.GetRight() - ScaleF(5.0f), x.Y + ScaleF(5.0f), x.X + ScaleF(5.0f),
                       x.GetBottom() - ScaleF(5.0f));
        }
    }
}

void DrawSidebar(Gdiplus::Graphics& g, App* app) {
    const Layout& L = app->layout;
    FillSolid(g, Gdiplus::RectF(0.0f, static_cast<float>(L.sidebar.top),
                                static_cast<float>(L.sidebar.right),
                                static_cast<float>(L.sidebar.bottom - L.sidebar.top)), kTopbarBg);
    StrokeLine(g, static_cast<float>(L.sidebar.right) - 0.5f, static_cast<float>(L.sidebar.top),
               static_cast<float>(L.sidebar.right) - 0.5f, static_cast<float>(L.sidebar.bottom),
               kSideBorder);

    // 列表区裁剪到「置顶栏」下方：滚动时条目从标题下面穿过，不会与标题重叠
    const int titleBottom = SidebarTitleBottom(app);
    const Gdiplus::GraphicsState state = g.Save();
    g.SetClip(Gdiplus::RectF(static_cast<float>(L.sidebar.left), static_cast<float>(titleBottom),
                             static_cast<float>(L.sidebar.right - L.sidebar.left),
                             static_cast<float>(L.sidebar.bottom - titleBottom)));

    DrawTreeRows(g, app);

    // 操作说明
    {
        const int pad = Scale(kSidePad);
        const float boxTop = static_cast<float>(L.sidebar.top + app->sidebarContentH) -
                             static_cast<float>(app->sidebarScroll);
        const float boxW = static_cast<float>(L.sidebar.right - pad * 2);
        Gdiplus::Font* font = Font(ScaleF(11.0f));
        std::vector<std::wstring> lines;
        lines.push_back(L"操作说明：");
        for (const wchar_t* note : kNotes) {
            std::wstring line;
            for (const wchar_t ch : std::wstring(note)) {
                line.push_back(ch);
                if (TextWidth(g, line, font) > boxW - ScaleF(20.0f)) {
                    lines.push_back(line);
                    line.clear();
                }
            }
            if (!line.empty()) lines.push_back(line);
        }
        const float lineH = static_cast<float>(font->GetHeight(&g)) + ScaleF(2.0f);
        const Gdiplus::RectF box(static_cast<float>(pad), boxTop, boxW,
                                 lineH * static_cast<float>(lines.size()) + ScaleF(18.0f));
        FillRounded(g, box, ScaleF(8.0f), kPanelSoft);
        float y = box.Y + ScaleF(9.0f) + lineH / 2.0f;
        for (const std::wstring& line : lines) {
            TextMiddleLeft(g, line, font, box.X + ScaleF(10.0f), y, kTextDim,
                           box.Width - ScaleF(20.0f));
            y += lineH;
        }
    }
    g.Restore(state);

    // 置顶栏（最后画，压在滚动内容之上）：标题 + 一条分隔线
    {
        const float titleTop = static_cast<float>(L.sidebar.top);
        const float titleBottomF = static_cast<float>(titleBottom);
        FillSolid(g, Gdiplus::RectF(0.0f, titleTop, static_cast<float>(L.sidebar.right),
                                    titleBottomF - titleTop), kTopbarBg);
        TextMiddleLeft(g, L"设备目录", Font(ScaleF(14.0f), true), static_cast<float>(Scale(kSidePad)),
                       (titleTop + titleBottomF) / 2.0f - ScaleF(6.0f), kTextMain);
        StrokeLine(g, static_cast<float>(Scale(kSidePad)), titleBottomF - 0.5f,
                   static_cast<float>(L.sidebar.right - Scale(kSidePad)), titleBottomF - 0.5f,
                   kBorderSoft);
    }

    DrawSidebarScrollbar(g, app);
}

/* 侧栏滚动条：深色滑块 + 浅色轨道，可拖动（滚动状态一眼可见） */
void DrawSidebarScrollbar(Gdiplus::Graphics& g, App* app) {
    const SidebarScrollbar bar = SidebarScrollbarOf(app);
    if (!bar.visible) return;

    FillRounded(g, bar.track, ScaleF(3.0f), kScrollTrack);
    FillRounded(g, bar.thumb, ScaleF(3.0f), app->draggingSidebarThumb ? kScrollThumbHot : kScrollThumb);
}

void DrawStage(Gdiplus::Graphics& g, App* app) {
    const Layout& L = app->layout;
    const Gdiplus::RectF stage(static_cast<float>(L.stage.left), static_cast<float>(L.stage.top),
                               static_cast<float>(L.stage.right - L.stage.left),
                               static_cast<float>(L.stage.bottom - L.stage.top));
    FillVertical(g, stage, kStageTop, kStageBottom, 0.35f, kStageMid);

    const Gdiplus::GraphicsState state = g.Save();
    g.SetClip(stage);

    const RackMetrics m = MetricsOf(app);
    const float scale = ScreenScale(app);
    const Gdiplus::PointF origin = FrameOrigin(app);

    // 机柜本体：逻辑单位作画（96 DPI 设计值），由变换负责缩放
    {
        const Gdiplus::GraphicsState inner = g.Save();
        g.TranslateTransform(origin.X, origin.Y);
        g.ScaleTransform(scale, scale);
        const int dimId = (app->session.active && app->session.id != 0) ? app->session.id : 0;
        DrawRack(g, app->state, m, app->state.selectedId, 9.5f, dimId);
        g.Restore(inner);
    }

    // 地面阴影（HTML 对照版为机柜下方的椭圆投影，位于缩放之外）
    {
        const float shadowW = 420.0f * scale;
        const float shadowY = origin.Y + m.frameH * scale + ScaleF(10.0f);
        Gdiplus::RectF shadow(origin.X + (m.frameW * scale - shadowW) / 2.0f, shadowY, shadowW,
                              ScaleF(20.0f));
        Gdiplus::GraphicsPath path;
        AddRoundedRect(&path, shadow, shadow.Height / 2.0f);
        Gdiplus::SolidBrush brush(Gdiplus::Color(56, 15, 25, 40));
        g.FillPath(&brush, &path);
    }

    // 示意外形（ghost）：88% 不透明度 + 绿/红描边，位置随吸附结果
    if (app->session.active) {
        const Device& d = app->session.dev;
        const float w = d.w * rack::ColW() * scale;
        const float h = d.hU * rack::kUPx * scale;
        float x = 0.0f, y = 0.0f;
        bool ok = false, bad = false;
        if (app->session.hasSnap) {
            const rack::InnerRect inner = InnerRectOf(app->state, m, origin, scale);
            x = static_cast<float>(inner.left) +
                static_cast<float>((rack::kPadX + app->session.snap.col * rack::ColW()) * scale);
            y = static_cast<float>(inner.top) +
                static_cast<float>((rack::kPadTop + app->session.snap.hole * rack::SlotHExact()) * scale);
            ok = app->session.snap.ok;
            bad = !ok;
        } else {
            x = static_cast<float>(app->lastMouse.x) - w / 2.0f;
            y = static_cast<float>(app->lastMouse.y) - h / 2.0f;
        }
        const Gdiplus::RectF box(x, y, w, h);
        const Gdiplus::RectF logical = DeviceRectIn(m, d);   // 外框坐标系（含内框原点偏移）

        const int pad = static_cast<int>(std::ceil(4.0f * scale)) + 2;
        const int layerW = std::max(1, static_cast<int>(w) + pad * 2);
        const int layerH = std::max(1, static_cast<int>(h) + pad * 2);
        Gdiplus::Bitmap layer(layerW, layerH, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics lg(&layer);
            lg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            lg.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            lg.TranslateTransform(static_cast<float>(pad) - logical.X * scale,
                                  static_cast<float>(pad) - logical.Y * scale);
            lg.ScaleTransform(scale, scale);
            DrawDevicePanel(lg, d, logical, 9.5f);
        }
        Gdiplus::ImageAttributes attributes;
        Gdiplus::ColorMatrix matrix = {};
        matrix.m[0][0] = 1.0f;
        matrix.m[1][1] = 1.0f;
        matrix.m[2][2] = 1.0f;
        matrix.m[3][3] = 0.88f;
        matrix.m[4][4] = 1.0f;
        attributes.SetColorMatrix(&matrix);
        g.DrawImage(&layer,
                    Gdiplus::RectF(box.X - static_cast<float>(pad), box.Y - static_cast<float>(pad),
                                   w + pad * 2.0f, h + pad * 2.0f),
                    0.0f, 0.0f, static_cast<Gdiplus::REAL>(layerW),
                    static_cast<Gdiplus::REAL>(layerH), Gdiplus::UnitPixel, &attributes);

        const float edge = ScaleF(2.0f);
        if (ok) {
            StrokeRounded(g, Gdiplus::RectF(box.X - edge / 2.0f, box.Y - edge / 2.0f, box.Width + edge,
                                            box.Height + edge),
                          2.0f, kOkGreen, edge);
        } else if (bad) {
            StrokeRounded(g, Gdiplus::RectF(box.X - edge / 2.0f, box.Y - edge / 2.0f, box.Width + edge,
                                            box.Height + edge),
                          2.0f, kBadRed, edge);
        }
    }

    g.Restore(state);
}

void DrawOverlays(Gdiplus::Graphics& g, App* app) {
    const Layout& L = app->layout;
    const float clientW = static_cast<float>(L.client.right);

    // 提示条（拖动会话进行中）
    if (app->session.active && !app->hint.empty()) {
        Gdiplus::Font* font = Font(ScaleF(12.0f));
        const float textW = TextWidth(g, app->hint, font);
        const Gdiplus::RectF pill(clientW / 2.0f - textW / 2.0f - ScaleF(16.0f),
                                  static_cast<float>(L.client.bottom) - ScaleF(46.0f),
                                  textW + ScaleF(32.0f), ScaleF(28.0f));
        FillRounded(g, pill, pill.Height / 2.0f, kHintBg);
        TextCenter(g, app->hint, font, pill, 0xffffff);
    }

    // Toast
    if (!app->toast.empty() && GetTickCount64() < app->toastUntil) {
        Gdiplus::Font* font = Font(ScaleF(13.0f));
        const float textW = TextWidth(g, app->toast, font);
        const Gdiplus::RectF box(clientW / 2.0f - textW / 2.0f - ScaleF(16.0f), ScaleF(16.0f),
                                 textW + ScaleF(32.0f), ScaleF(32.0f));
        FillRounded(g, box, ScaleF(8.0f), kToastBg);
        TextCenter(g, app->toast, font, box, kToastText);
    }
}

void PaintAll(Gdiplus::Graphics& g, App* app) {
    FillSolid(g, Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(app->layout.client.right),
                                static_cast<float>(app->layout.client.bottom)), kPageBg);
    DrawStage(g, app);
    DrawSidebar(g, app);
    DrawTopbar(g, app);
    DrawOverlays(g, app);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        // 屏幕文字用 ClearType（与系统其它界面一致，笔画比 GDI+ 抗锯齿更实）；
        // 机柜内的文字处于缩放变换下，由 TextCrisp 换算到设备像素后再绘制（见 ui/theme.h）
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        PaintAll(g, g_app);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

/* ---------------- 命中测试 ---------------- */
bool InRect(const RECT& rc, POINT pt) {
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

int HitControl(App* app, POINT pt) {
    const Layout& L = app->layout;
    if (InRect(L.updateBtn, pt)) return kCtlUpdate;
    if (InRect(L.rackBtn, pt)) return kCtlRack;
    if (InRect(L.modeU, pt)) return kCtlModeU;
    if (InRect(L.modeHole, pt)) return kCtlModeHole;
    if (InRect(L.clearBtn, pt)) return kCtlClear;
    if (InRect(L.saveBtn, pt)) return kCtlSave;
    if (InRect(L.exportBtn, pt)) return kCtlExport;
    if (InRect(L.importBtn, pt)) return kCtlImport;
    if (InRect(L.zoomMinus, pt)) return kCtlZoomMinus;
    if (InRect(L.zoomTrack, pt) ||
        (pt.x >= L.zoomTrack.left && pt.x <= L.zoomTrack.right && pt.y >= L.zoomTrack.top - Scale(10) &&
         pt.y <= L.zoomTrack.bottom + Scale(10))) {
        return kCtlZoomTrack;
    }
    if (InRect(L.zoomPlus, pt)) return kCtlZoomPlus;
    if (InRect(L.zoomReset, pt)) return kCtlZoomReset;
    return kCtlNone;
}

int HitRow(App* app, POINT pt) {
    if (pt.x < app->layout.sidebar.left || pt.x > app->layout.sidebar.right) return -1;
    if (pt.y < app->layout.sidebar.top || pt.y > app->layout.sidebar.bottom) return -1;
    // 置顶栏下方才是列表：压在标题下面的条目不可点（它们只是从视觉上被标题遮住）
    if (pt.y < SidebarTitleBottom(app)) return -1;
    if (IsOnSidebarScrollbar(app, pt)) return -1;
    for (size_t i = 0; i < app->rows.size(); ++i) {
        RECT rc = app->rows[i].rect;
        rc.top -= app->sidebarScroll;
        rc.bottom -= app->sidebarScroll;
        if (InRect(rc, pt)) return static_cast<int>(i);
    }
    return -1;
}

void ToggleExpand(App* app, const std::wstring& path, bool current) {
    app->expanded[path] = !current;
    BuildRows(app);
    ClampScroll(app);
    InvalidateAll(app);
}

void ZoomFromTrack(App* app, int mouseX) {
    const Layout& L = app->layout;
    const int trackW = L.zoomTrack.right - L.zoomTrack.left;
    if (trackW <= 0) return;
    const int offset = rack::ClampInt(mouseX - L.zoomTrack.left, 0, trackW);
    const int zoom = rack::kZoomMin + offset * (rack::kZoomMax - rack::kZoomMin) / trackW;
    ApplyZoom(app, (zoom / 5) * 5);   // 滑块步进 5%
}

/* ---------------- 事件 ---------------- */
void OnMouseDown(App* app, POINT pt, bool rightButton) {
    app->lastMouse = pt;

    if (rightButton) {
        const int row = HitRow(app, pt);
        if (row >= 0 && app->rows[static_cast<size_t>(row)].kind == RowKind::Archive) {
            POINT screen = pt;
            ClientToScreen(app->hwnd, &screen);
            OpenArchiveMenu(app, app->rows[static_cast<size_t>(row)].archiveIndex, screen);
        }
        return;
    }

    // 侧栏滚动条：按住滑块拖动；按在空白处则先把滑块跳过去再拖
    if (IsOnSidebarScrollbar(app, pt)) {
        const SidebarScrollbar bar = SidebarScrollbarOf(app);
        const float y = static_cast<float>(pt.y);
        if (y >= bar.thumb.Y && y <= bar.thumb.GetBottom()) {
            app->sidebarThumbGrabDy = y - bar.thumb.Y;
        } else {
            app->sidebarThumbGrabDy = bar.thumb.Height / 2.0f;
            SetSidebarScrollByThumb(app, y - app->sidebarThumbGrabDy);
        }
        app->draggingSidebarThumb = true;
        SetCapture(app->hwnd);
        InvalidateAll(app);
        return;
    }

    // 待放置态：点在机柜上 → 转入拖动语义（松手落位）；点在其他地方 → 取消
    if (app->session.active && app->session.pending) {
        if (PointInRack(app, pt)) {
            app->session.pending = false;
            app->session.fromPending = true;
            app->session.moved = false;
            app->session.start = pt;
            UpdateSnap(app, pt);
            UpdateHint(app);
        } else {
            CancelSession(app);
        }
        return;
    }

    const int control = HitControl(app, pt);
    if (control != kCtlNone) {
        switch (control) {
            case kCtlUpdate:
                if (!util::OpenUrl(APP_RELEASE_URL)) {
                    ShowToast(app, std::wstring(L"无法打开浏览器，请手动访问：") + APP_RELEASE_URL);
                }
                break;
            case kCtlRack: {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"42U 标准机柜");
                AppendMenuW(menu, MF_STRING, 2, L"36U 标准机柜");
                AppendMenuW(menu, MF_STRING, 3, L"24U 标准机柜");
                RECT rc = app->layout.rackBtn;
                POINT screen = {rc.left, rc.bottom};
                ClientToScreen(app->hwnd, &screen);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON, screen.x,
                                                   screen.y, 0, app->hwnd, nullptr);
                DestroyMenu(menu);
                if (command == 1) SwitchRack(app, RackKind::U42);
                else if (command == 2) SwitchRack(app, RackKind::U36);
                else if (command == 3) SwitchRack(app, RackKind::U24);
                break;
            }
            case kCtlModeU: SetMode(app, SnapMode::U); break;
            case kCtlModeHole: SetMode(app, SnapMode::Hole); break;
            case kCtlClear: ClearRack(app); break;
            case kCtlSave: DoSaveImage(app); break;
            case kCtlExport: DoExportLayout(app); break;
            case kCtlImport: DoImportLayout(app); break;
            case kCtlZoomMinus: ApplyZoom(app, app->state.zoom - rack::kZoomStepButton); break;
            case kCtlZoomPlus: ApplyZoom(app, app->state.zoom + rack::kZoomStepButton); break;
            case kCtlZoomTrack:
                app->draggingTrack = true;
                SetCapture(app->hwnd);
                ZoomFromTrack(app, pt.x);
                break;
            case kCtlZoomReset: ApplyZoom(app, 100); break;
            default: break;
        }
        return;
    }

    // 目录树
    const int row = HitRow(app, pt);
    if (row >= 0) {
        const TreeRow& item = app->rows[static_cast<size_t>(row)];
        if (item.kind == RowKind::Group || item.kind == RowKind::MyDevices) {
            ToggleExpand(app, item.path, item.open);
            return;
        }
        if (item.kind == RowKind::AddCustom) {
            OpenCustomDeviceDialog(app);
            return;
        }
        if (item.kind == RowKind::Archive) {
            // 悬停的 × 按钮：删除
            RECT rc = item.rect;
            rc.top -= app->sidebarScroll;
            rc.bottom -= app->sidebarScroll;
            const RECT close{rc.right - Scale(kSidePad) - Scale(18), rc.top + Scale(6),
                             rc.right - Scale(kSidePad), rc.top + Scale(22)};
            if (InRect(close, pt)) {
                DeleteArchived(app, item.archiveIndex);
                return;
            }
            const Archived& a = app->state.archive[static_cast<size_t>(item.archiveIndex)];
            Device dev;
            dev.cat = Category::Custom;
            dev.name = a.name;
            dev.w = a.w;
            dev.hU = a.hU;
            BeginSession(app, dev, 0, pt, false);
            return;
        }
        if (item.kind == RowKind::Leaf) {
            Device dev;
            dev.cat = item.leaf.cat;
            dev.name = item.leaf.full;
            dev.w = item.leaf.w;
            dev.hU = item.leaf.hU;
            BeginSession(app, dev, 0, pt, false);
            return;
        }
        return;
    }

    // 机柜内
    if (pt.x >= app->layout.stage.left) {
        const RackMetrics m = MetricsOf(app);
        const float scale = ScreenScale(app);
        const Gdiplus::PointF origin = FrameOrigin(app);
        const Gdiplus::PointF point(static_cast<float>(pt.x), static_cast<float>(pt.y));

        if (HitCloseButton(app->state, m, origin, scale, point)) {
            const int id = app->state.selectedId;
            if (id != 0) RemoveDevice(app, id, true);
            return;
        }
        const int deviceId = HitDevice(app->state, m, origin, scale, point);
        if (deviceId != 0) {
            Device dev;
            for (const Device& d : app->state.devices) {
                if (d.id == deviceId) dev = d;
            }
            BeginSession(app, dev, deviceId, pt, false);
            return;
        }
        if (app->state.selectedId != 0) {
            app->state.selectedId = 0;
            InvalidateAll(app);
        }
    }
}

void OnMouseMove(App* app, POINT pt) {
    app->lastMouse = pt;

    if (app->draggingTrack) {
        ZoomFromTrack(app, pt.x);
        return;
    }

    if (app->draggingSidebarThumb) {
        SetSidebarScrollByThumb(app, static_cast<float>(pt.y) - app->sidebarThumbGrabDy);
        return;
    }

    if (app->session.active) {
        Session& s = app->session;
        if (!s.pending) {
            if (!s.moved) {
                const int dx = pt.x - s.start.x;
                const int dy = pt.y - s.start.y;
                const int threshold = Scale(rack::kDragThreshold);
                if (dx * dx + dy * dy >= threshold * threshold) s.moved = true;
            }
        }
        UpdateSnap(app, pt);
        return;
    }

    const int control = HitControl(app, pt);
    const int row = control == kCtlNone ? HitRow(app, pt) : -1;
    if (control != app->hoverControl || row != app->hoverRow) {
        app->hoverControl = control;
        app->hoverRow = row;
        InvalidateAll(app);
    }
}

void OnMouseUp(App* app, POINT pt) {
    if (app->draggingTrack) {
        app->draggingTrack = false;
        ReleaseCapture();
        return;
    }
    if (app->draggingSidebarThumb) {
        app->draggingSidebarThumb = false;
        ReleaseCapture();
        InvalidateAll(app);
        return;
    }
    if (!app->session.active || app->session.pending) {
        if (GetCapture() == app->hwnd && !app->session.active) ReleaseCapture();
        return;
    }

    Session& s = app->session;
    if (s.moved || s.fromPending) {
        if (s.hasSnap && s.snap.ok) {
            CommitPlace(app);
            if (GetCapture() == app->hwnd) ReleaseCapture();
            return;
        }
        if (s.id == 0) {
            if (PointInRack(app, pt)) {
                s.pending = true;            // 机柜内但卡位冲突：留在待放置态
                s.hasSnap = false;
                if (GetCapture() == app->hwnd) ReleaseCapture();
                UpdateHint(app);
                InvalidateAll(app);
            } else {
                CancelSession(app);
                ShowToast(app, L"已取消放置");
            }
            return;
        }
        if (!PointInRack(app, pt)) {
            const int id = s.id;
            app->session = Session();
            ResetGhostCache(app);
            if (GetCapture() == app->hwnd) ReleaseCapture();
            UpdateHint(app);
            RemoveDevice(app, id, true);
            return;
        }
        CancelSession(app);                   // 机柜内但无效：恢复原位
        return;
    }

    // 点击语义
    if (s.id == 0) {
        s.pending = true;
        s.hasSnap = false;
        if (GetCapture() == app->hwnd) ReleaseCapture();
        UpdateHint(app);
        InvalidateAll(app);
        return;
    }
    const int id = s.id;
    app->session = Session();
    ResetGhostCache(app);
    if (GetCapture() == app->hwnd) ReleaseCapture();
    UpdateHint(app);
    app->state.selectedId = id;
    InvalidateAll(app);
}

void OnWheel(App* app, POINT pt, int delta, bool ctrl) {
    if (ctrl) {
        ApplyZoom(app, app->state.zoom + (delta > 0 ? rack::kZoomStepButton : -rack::kZoomStepButton));
        return;
    }
    if (pt.x < app->layout.sidebar.right) {
        app->sidebarScroll -= delta / 120 * Scale(48);
    } else {
        app->stageScroll -= delta / 120 * Scale(48);
    }
    ClampScroll(app);
    InvalidateAll(app);
}

void OnKeyDown(App* app, WPARAM key) {
    if (key == VK_ESCAPE) {
        CancelSession(app);
        return;
    }
    if (key == VK_DELETE || key == VK_BACK) {
        if (app->state.selectedId != 0 && !app->session.active) {
            RemoveDevice(app, app->state.selectedId, true);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    App* app = g_app;
    switch (msg) {
        case WM_PAINT:
            OnPaint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (app != nullptr) {
                app->layout = ComputeLayout(hwnd);
                BuildRows(app);
                ClampScroll(app);
                InvalidateAll(app);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = Scale(1080);
            info->ptMinTrackSize.y = Scale(620);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (app == nullptr) return 0;
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            SetFocus(hwnd);
            OnMouseDown(app, pt, false);
            return 0;
        }
        case WM_RBUTTONUP: {
            if (app == nullptr) return 0;
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnMouseDown(app, pt, true);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (app == nullptr) return 0;
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnMouseMove(app, pt);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (app == nullptr) return 0;
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnMouseUp(app, pt);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (app == nullptr) return 0;
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            const bool ctrl = (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0;
            OnWheel(app, pt, GET_WHEEL_DELTA_WPARAM(wparam), ctrl);
            return 0;
        }
        case WM_SETCURSOR: {
            if (app != nullptr) {
                if (app->session.active) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    return TRUE;
                }
            }
            break;
        }
        case WM_KEYDOWN: {
            if (app == nullptr) return 0;
            OnKeyDown(app, wparam);
            return 0;
        }
        case WM_TIMER: {
            if (app != nullptr) {
                if (!app->toast.empty() && GetTickCount64() >= app->toastUntil) {
                    app->toast.clear();
                    KillTimer(hwnd, 1);
                    InvalidateAll(app);
                }
            }
            return 0;
        }
        case WM_CLOSE: {
            if (app != nullptr && !app->state.devices.empty()) {
                if (!Confirm(hwnd, L"机柜里还有设备，未导出的布设会丢失。\n确定关闭吗？", L"关闭")) {
                    return 0;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

const wchar_t* MainWindowClassName() { return APP_WINDOW_CLASS; }

bool CreateMainWindow(HINSTANCE instance, int showCommand, const State* initialState) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
                                             Scale(32), Scale(32), LR_DEFAULTCOLOR));
    if (wc.hIcon == nullptr) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
                                               Scale(16), Scale(16), LR_DEFAULTCOLOR));
    if (wc.hIconSm == nullptr) wc.hIconSm = wc.hIcon;
    wc.lpszClassName = APP_WINDOW_CLASS;
    if (RegisterClassExW(&wc) == 0) return false;

    App* app = new App();
    if (initialState != nullptr) app->state = *initialState;
    app->state.archive = rack::LoadArchive();
    g_app = app;

    RECT rc = {0, 0, Scale(1120), Scale(760)};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const std::wstring title = std::wstring(APP_NAME_CN) + L" " + APP_VERSION_TAG;
    HWND hwnd = CreateWindowExW(0, APP_WINDOW_CLASS, title.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                                rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        delete app;
        g_app = nullptr;
        return false;
    }
    app->hwnd = hwnd;
    app->layout = ComputeLayout(hwnd);
    BuildRows(app);

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    // 存档默认放在程序目录（便携）；程序目录不可写时提示一次实际落点
    if (!rack::StoreIsPortable()) {
        ShowToast(app, L"程序目录不可写，存档改存于 " + rack::StoreDir());
    }
    return true;
}

int RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace ui
