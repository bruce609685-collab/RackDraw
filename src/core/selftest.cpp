#include "core/selftest.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

#include "core/catalog.h"
#include "core/markdown.h"
#include "core/store.h"
#include "ui/image_export.h"
#include "util/text_convert.h"

namespace rack {
namespace {

int g_pass = 0;
int g_fail = 0;
std::vector<std::wstring> g_lines;
std::wstring g_logPath;

void Emit(const std::wstring& line) {
    g_lines.push_back(line);
    // 控制台输出写 UTF-8 字节：本工具链的 stdout 处于字节模式，fputws 会静默失败
    std::fputs(util::WideToUtf8(line + L"\n").c_str(), stdout);

    // 每行立即追加到日志：崩溃时也能看到跑到哪一条（正式使用只看 selftest-report.txt）
    if (g_logPath.empty()) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path = exePath;
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            path.resize(slash + 1);
        } else {
            path.clear();
        }
        g_logPath = path + L"selftest-log.txt";
    }
    const std::string bytes = util::WideToUtf8(line + L"\r\n");
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(file);
    }
}

void Check(const char* id, bool condition, const std::wstring& detail) {
    if (condition) {
        ++g_pass;
    } else {
        ++g_fail;
    }
    std::wstring wideId;
    for (const char* p = id; p != nullptr && *p != 0; ++p) {
        wideId.push_back(static_cast<wchar_t>(*p));
    }
    Emit(std::wstring(L"@@RSLT@@ ") + (condition ? L"PASS" : L"FAIL") + L" | " + wideId + L" | " +
         detail);
}

// 逐台设备的关键字段，排序后拼接（导入后按 U 位排序，与源顺序无关，故比较前先排序）
std::wstring Key(const State& s) {
    std::vector<std::wstring> items;
    for (const Device& d : s.devices) {
        items.push_back(d.name + L"|" + std::to_wstring(d.w) + L"|" + std::to_wstring(d.hU) + L"|" +
                        std::to_wstring(d.col) + L"|" + std::to_wstring(d.hole));
    }
    std::sort(items.begin(), items.end());
    std::wstring out;
    for (const std::wstring& item : items) {
        out += item + L";";
    }
    return out;
}

void Add(State* s, Category cat, const wchar_t* name, int w, int hU, int col, int hole) {
    Device d;
    d.id = s->seq++;
    d.cat = cat;
    d.name = name;
    d.w = w;
    d.hU = hU;
    d.col = col;
    d.hole = hole;
    s->devices.push_back(d);
}

rack::InnerRect MakeInner(double left, double top, double scale) {
    rack::InnerRect r;
    r.left = left;
    r.top = top;
    r.width = rack::kInnerW * scale;
    r.height = (rack::BodyH(RackKind::U42) + rack::kPadTop + rack::kPadBottom) * scale;
    r.scale = scale;
    return r;
}

}  // namespace

State MakeDemoState() {
    State s;
    s.rack = RackKind::U42;
    s.mode = SnapMode::U;
    // 每台设备的 col + w 都 ≤ 6（横向 6 等分网格），便于界面与对照图直接核对
    Add(&s, Category::Router, L"路由器 · 标准", 6, 1, 0, 0);
    Add(&s, Category::Switch, L"交换机 · 标准", 6, 1, 0, 3);
    Add(&s, Category::Fw, L"防火墙 · 小型", 3, 1, 0, 6);
    Add(&s, Category::Wireless, L"PoE交换机 · mini", 2, 1, 3, 6);
    Add(&s, Category::Accessory, L"理线架", 6, 1, 0, 9);
    Add(&s, Category::Custom, L"集成网关", 3, 3, 0, 12);
    Add(&s, Category::Small, L"HDMI延长器", 2, 1, 3, 12);
    Add(&s, Category::Monitor, L"监控录像机", 6, 3, 0, 15);
    Add(&s, Category::Accessory, L"配线架", 6, 1, 0, 21);
    Add(&s, Category::Accessory, L"空位挡板", 6, 1, 0, 24);
    Add(&s, Category::Small, L"光猫", 2, 1, 0, 30);
    Add(&s, Category::Custom, L"光端机", 2, 2, 4, 30);
    Add(&s, Category::Monitor, L"监控交换机", 6, 1, 0, 42);
    return s;
}

int RunSelfTest() {
    Emit(L"RackDraw 自检（核心逻辑，无界面）");

    /* ---------- S1 目录与名称匹配 ---------- */
    {
        const std::vector<CatalogNode>& nodes = Catalog();
        bool hasSwitch = false;
        for (const CatalogNode& n : nodes) {
            for (const CatalogNode& child : n.children) {
                if (child.isLeaf && child.leaf.cat == Category::Switch &&
                    std::wstring(child.leaf.full) == L"交换机 · 标准") {
                    hasSwitch = true;
                }
            }
        }
        Check("S1-catalog", nodes.size() == 7 && hasSwitch,
              L"一级项=" + std::to_wstring(nodes.size()) + L" 交换机叶子=" +
                  (hasSwitch ? L"有" : L"无"));

        // 监控设备：一级项 + 两个叶子（标准全宽 / 3U 与 1U）
        const CatalogLeaf* nvr = nullptr;
        const CatalogLeaf* monSwitch = nullptr;
        for (const CatalogNode& n : nodes) {
            if (n.label != L"监控设备") continue;
            for (const CatalogNode& child : n.children) {
                if (!child.isLeaf) continue;
                if (std::wstring(child.leaf.full) == L"监控录像机") nvr = &child.leaf;
                if (std::wstring(child.leaf.full) == L"监控交换机") monSwitch = &child.leaf;
            }
        }
        Check("S1b-monitor", nvr != nullptr && monSwitch != nullptr && nvr->w == 6 && nvr->hU == 3 &&
                                 monSwitch->w == 6 && monSwitch->hU == 1,
              L"监控录像机=" + std::wstring(nvr ? L"6宽/3U" : L"缺") + L" 监控交换机=" +
                  std::wstring(monSwitch ? L"6宽/1U" : L"缺"));

        const NameMatch exact = MatchDeviceName(L"交换机 · 标准", {});
        const NameMatch fuzzy = MatchDeviceName(L"交换机·标准 ", {});
        const NameMatch unknown = MatchDeviceName(L"某不存在的设备", {});
        Check("S2-name-match",
              exact.cat == Category::Switch && !exact.unknown &&
                  fuzzy.cat == Category::Switch && !fuzzy.unknown && unknown.unknown &&
                  unknown.cat == Category::Custom,
              L"精确=交换机? " + std::to_wstring(exact.cat == Category::Switch) + L" 模糊=" +
                  std::to_wstring(fuzzy.cat == Category::Switch) + L" 未知=" +
                  std::to_wstring(unknown.unknown));
    }

    /* ---------- S3 坐标换算 ---------- */
    {
        const int total = TotalHoles(RackKind::U42);
        Check("S3-coords",
              total == 126 && HoleToUText(RackKind::U42, 0) == 42 &&
                  HoleToUText(RackKind::U42, 123) == 1 && HoleToUText(RackKind::U42, 3) == 41,
              L"总孔=" + std::to_wstring(total) + L" hole0→U" +
                  std::to_wstring(HoleToUText(RackKind::U42, 0)) + L" hole123→U" +
                  std::to_wstring(HoleToUText(RackKind::U42, 123)));
    }

    /* ---------- S4 吸附（U 数版式整 U 取整） ---------- */
    {
        State s;
        s.rack = RackKind::U42;
        s.mode = SnapMode::U;
        Device dev;
        dev.cat = Category::Switch;
        dev.name = L"交换机 · 标准";
        dev.w = 6;
        dev.hU = 1;
        const InnerRect inner = MakeInner(100.0, 50.0, 1.0);

        // 指针放在内框左上角附近：整宽设备中心对齐 → col 0，hole 取整为 0
        const SnapResult snap = CalcSnap(s, dev, inner, 100.0 + 156.0, 50.0 + 14.0 + 11.0, 0);
        Check("S4-snap",
              snap.valid && snap.ok && snap.col == 0 && snap.hole == 0,
              L"col=" + std::to_wstring(snap.col) + L" hole=" + std::to_wstring(snap.hole));

        // 螺丝孔位版式：不按整 U 取整
        s.mode = SnapMode::Hole;
        const SnapResult holeSnap = CalcSnap(s, dev, inner, 100.0 + 156.0, 50.0 + 14.0 + 18.0, 0);
        Check("S5-snap-hole", holeSnap.valid && holeSnap.hole != 0,
              L"hole=" + std::to_wstring(holeSnap.hole));
    }

    /* ---------- S6 冲突与顺延 ---------- */
    {
        State s;
        s.rack = RackKind::U42;
        Add(&s, Category::Switch, L"交换机 · 标准", 6, 1, 0, 0);
        Device other;
        other.w = 6;
        other.hU = 1;
        const bool conflict = Collides(s, 0, 0, other, 0);
        const SlotProbe probe = FindFreeHole(s, 0, 0, other, kHolesPerU, TotalHoles(s.rack) - kHolesPerU);
        Check("S6-conflict", conflict && probe.found && probe.adjusted && probe.hole == kHolesPerU,
              L"冲突=" + std::to_wstring(conflict) + L" 顺延到 hole=" + std::to_wstring(probe.hole));
    }

    /* ---------- S7 往返：U 数版式 ---------- */
    {
        State s;
        s.rack = RackKind::U42;
        s.mode = SnapMode::U;
        Add(&s, Category::Router, L"路由器 · 标准", 6, 1, 0, 0);
        Add(&s, Category::Switch, L"交换机 · 小型", 3, 2, 0, 6);
        Add(&s, Category::Small, L"光猫", 2, 1, 4, 9);
        Add(&s, Category::Custom, L"集成网关 | 主*_机", 3, 3, 1, 12);
        Add(&s, Category::Accessory, L"配线架", 6, 1, 0, 24);
        const std::wstring md = BuildLayoutMarkdown(s);
        State empty;
        empty.rack = RackKind::U42;
        const ImportResult result = ImportLayout(md, empty);
        Check("S7-roundtrip-u", result.ok && result.next.devices.size() == s.devices.size() &&
                                   Key(result.next) == Key(s),
              result.ok ? (Key(result.next) == Key(s) ? L"一致" : L"不一致：" + Key(result.next))
                        : (L"解析失败：" + result.error));
    }

    /* ---------- S8 往返：螺丝孔位版式（含同 U 内偏移） ---------- */
    {
        State s;
        s.rack = RackKind::U36;
        s.mode = SnapMode::Hole;
        Add(&s, Category::Custom, L"高密度设备", 2, 5, 4, 70);
        Add(&s, Category::Custom, L"偏移设备", 3, 1, 0, 34);
        Add(&s, Category::Small, L"光电转换器", 2, 1, 2, 105);
        const std::wstring md = BuildLayoutMarkdown(s);
        State empty;
        empty.rack = RackKind::U42;
        const ImportResult result = ImportLayout(md, empty);
        Check("S8-roundtrip-hole",
              result.ok && result.next.rack == RackKind::U36 && result.next.mode == SnapMode::Hole &&
                  Key(result.next) == Key(s),
              result.ok ? (Key(result.next) == Key(s) ? L"一致" : L"不一致：" + Key(result.next))
                        : (L"解析失败：" + result.error));
    }

    /* ---------- S9 导入缺清单表：不改状态 ---------- */
    {
        State current;
        current.rack = RackKind::U42;
        Add(&current, Category::Switch, L"交换机 · 标准", 6, 1, 0, 0);
        const std::wstring text =
            L"# 弱电机柜设备布设表\n\n## 一、基本信息\n\n| 项目 | 内容 |\n|---|---|\n"
            L"| 机柜规格 | 42U 标准机柜 |\n| 卡位版式 | 标准 U 数版式 |\n";
        const ImportResult result = ImportLayout(text, current);
        Check("S9-import-no-table",
              !result.ok && result.next.devices.empty() && current.devices.size() == 1,
              L"ok=" + std::to_wstring(result.ok) + L" 错误=" + result.error);
    }

    /* ---------- S10 存档规整与去重 ---------- */
    {
        Archived dirty;
        dirty.name = L"这个存档名字明显超过十二个字";
        dirty.w = 99;
        dirty.hU = 0;
        const Archived clean = SanitizeArchived(dirty, 0);
        Check("S10-archive-sanitize",
              clean.w == 6 && clean.hU == 1 && clean.name.size() == 12 && !clean.id.empty(),
              L"w=" + std::to_wstring(clean.w) + L" hU=" + std::to_wstring(clean.hU) + L" 名字长=" +
                  std::to_wstring(clean.name.size()));

        std::vector<Archived> list;
        Archived first;
        first.name = L"测试网关";
        first.w = 3;
        first.hU = 2;
        const bool added1 = UpsertArchive(&list, first);
        Archived second = first;
        second.w = 6;
        second.hU = 4;
        const bool added2 = UpsertArchive(&list, second);
        Check("S11-archive-dedupe", added1 && !added2 && list.size() == 1 && list[0].w == 6 &&
                                        list[0].hU == 4,
              L"条数=" + std::to_wstring(list.size()) + L" w=" + std::to_wstring(list[0].w));

        const std::wstring copy = CopyNameOf(list, first.name);
        Check("S12-copy-name", copy != first.name && copy.size() <= kNameMax, L"副本名=" + copy);
    }

    /* ---------- S13 Markdown 转义往返 ---------- */
    {
        const std::wstring raw = L"集成网关 | 主*_机 \\ 备";
        const std::wstring round = MdUnescape(MdEscape(raw));
        Check("S13-md-escape", round == raw, L"raw=" + raw + L" round=" + round);
    }

    /* ---------- S14 ASCII 对照图：行宽一致 + 标尺对齐 ---------- */
    {
        State s;
        s.rack = RackKind::U42;
        Add(&s, Category::Switch, L"交换机 · 标准", 6, 1, 0, 0);
        Add(&s, Category::Small, L"HDMI延长器", 2, 1, 4, 3);
        const std::wstring map = BuildAsciiMap(s);
        std::vector<std::wstring> lines;
        std::wstring current;
        for (size_t i = 0; i < map.size(); ++i) {
            if (map[i] == L'\n') {
                lines.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(map[i]);
        }
        lines.push_back(current);

        bool widthOk = true;
        int firstWidth = -1;
        std::wstring rulerLine;
        for (const std::wstring& line : lines) {
            if (line.find(L"宽度占比") != std::wstring::npos) {
                rulerLine = line;
                continue;
            }
            if (line.find(L"┌") == std::wstring::npos && line.find(L"└") == std::wstring::npos &&
                line.find(L"│") == std::wstring::npos) {
                continue;   // ``` 行
            }
            const int width = DispWidth(line);
            if (firstWidth < 0) firstWidth = width;
            if (width != firstWidth) widthOk = false;
        }
        const size_t half = rulerLine.find(L"1/2");
        const size_t full = rulerLine.rfind(L"1");
        Check("S14-ascii", widthOk && half != std::wstring::npos && half - 5 == 15 && full - 5 == 30,
              L"行宽一致=" + std::to_wstring(widthOk) + L" 行宽=" + std::to_wstring(firstWidth) +
                  L" 1/2列=" + std::to_wstring(half == std::wstring::npos ? -1
                                                                          : static_cast<int>(half) - 5) +
                  L" 1列=" + std::to_wstring(static_cast<int>(full) - 5));
    }

    /* ---------- S15 示例场景数据 ---------- */
    {
        const State demo = MakeDemoState();
        Check("S15-demo", demo.devices.size() == 13 && UsedU(demo) == 18,
              L"设备=" + std::to_wstring(demo.devices.size()) + L" 高度合计=" +
                  std::to_wstring(UsedU(demo)) + L"U");
    }

    /* ---------- S15b 监控设备形态口径 ---------- */
    {
        Device nvr;
        nvr.cat = Category::Monitor;
        nvr.name = L"监控录像机";
        nvr.w = 6;
        nvr.hU = 3;
        Device sw;
        sw.cat = Category::Monitor;
        sw.name = L"监控交换机";
        sw.w = 6;
        sw.hU = 1;
        Check("S15b-monitor-shape",
              IsRecorder(nvr) && !IsRecorder(sw) && PortCount(nvr) == 6 && PortCount(sw) == 14 &&
                  std::wstring(CatLeds(nvr)) == L"gag" && std::wstring(CatLeds(sw)) == L"ga",
              L"录像机: 录像=" + std::to_wstring(IsRecorder(nvr)) + L" 元素=" +
                  std::to_wstring(PortCount(nvr)) + L" 灯=" + CatLeds(nvr) + L" / 交换机: 元素=" +
                  std::to_wstring(PortCount(sw)) + L" 灯=" + CatLeds(sw));
    }

    /* ---------- S16 导出图渲染与 PNG 编码 ---------- */
    {
        const State demo = MakeDemoState();
        ui::CanvasBox box = ui::ComputeCanvasBox(demo);
        Gdiplus::Bitmap* bitmap = ui::RenderLayoutBitmap(demo);
        bool rendered = bitmap != nullptr;
        int width = 0, height = 0;
        if (rendered) {
            width = static_cast<int>(bitmap->GetWidth());
            height = static_cast<int>(bitmap->GetHeight());
            wchar_t tempPath[MAX_PATH] = {0};
            GetTempPathW(MAX_PATH, tempPath);
            const std::wstring file = std::wstring(tempPath) + L"rackdraw_selftest.png";
            const bool saved = ui::SaveBitmapAsPng(file, bitmap);
            WIN32_FILE_ATTRIBUTE_DATA info = {};
            const bool exists = GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) != 0;
            const long long size = exists
                                       ? (static_cast<long long>(info.nFileSizeHigh) << 32) +
                                             info.nFileSizeLow
                                       : 0;
            Check("S16-export-png", saved && size > 1000,
                  L"尺寸=" + std::to_wstring(width) + L"×" + std::to_wstring(height) + L" 文件=" +
                      std::to_wstring(size) + L" 字节");
            DeleteFileW(file.c_str());
            delete bitmap;
        } else {
            Check("S16-export-png", false, L"渲染失败");
        }
        Check("S17-canvas-box", width == static_cast<int>(box.totalW * box.scale) &&
                                    height == static_cast<int>(box.totalH * box.scale),
              L"画布=" + std::to_wstring(width) + L"×" + std::to_wstring(height) + L" 期望=" +
                  std::to_wstring(static_cast<int>(box.totalW * box.scale)) + L"×" +
                  std::to_wstring(static_cast<int>(box.totalH * box.scale)));
    }

    /* ---------- S18 示例场景导出布设（分段定位） ---------- */
    {
        const State demo = MakeDemoState();
        Check("S18a-demo-state", demo.devices.size() == 13,
              L"设备=" + std::to_wstring(demo.devices.size()));
        const std::wstring map = BuildAsciiMap(demo);
        Check("S18b-ascii", map.size() > 1000, L"字符=" + std::to_wstring(map.size()));
        const std::wstring markdown = BuildLayoutMarkdown(demo);
        Check("S18c-markdown", markdown.size() > 2000, L"字符=" + std::to_wstring(markdown.size()));
        const std::string utf8 = util::WideToUtf8(markdown);
        Check("S18d-utf8", utf8.size() > markdown.size(),
              L"UTF-8 字节=" + std::to_wstring(utf8.size()));
    }

    /* ---------- S19 布设文件落盘（与 --export-md 同一条代码路径，分段定位） ---------- */
    {
        const State demo = MakeDemoState();
        const std::wstring markdown = BuildLayoutMarkdown(demo);
        Check("S19a-markdown", markdown.size() > 2000, L"字符=" + std::to_wstring(markdown.size()));

        wchar_t tempPath[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tempPath);
        const std::wstring file = std::wstring(tempPath) + L"rackdraw_selftest.md";
        Check("S19b-temppath", !file.empty(), file);

        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        Check("S19c-createfile", handle != INVALID_HANDLE_VALUE,
              L"句柄=" + std::to_wstring(handle != INVALID_HANDLE_VALUE));

        if (handle != INVALID_HANDLE_VALUE) {
            const std::string bytes = util::WideToUtf8(markdown);
            Check("S19d-utf8", bytes.size() > 2000, L"UTF-8 字节=" + std::to_wstring(bytes.size()));

            DWORD written = 0;
            const BOOL ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                                      &written, nullptr);
            Check("S19e-writefile", ok != 0 && written == bytes.size(),
                  L"已写=" + std::to_wstring(written));

            CloseHandle(handle);
            WIN32_FILE_ATTRIBUTE_DATA info = {};
            const bool got = GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) != 0;
            const long long size = got
                                       ? (static_cast<long long>(info.nFileSizeHigh) << 32) +
                                             info.nFileSizeLow
                                       : 0;
            Check("S19f-attrs", got && size > 2000, L"文件=" + std::to_wstring(size) + L" 字节");
            DeleteFileW(file.c_str());
        }
    }

    Emit(L"@@RSLT@@ SUMMARY | PASS=" + std::to_wstring(g_pass) + L" FAIL=" +
         std::to_wstring(g_fail));

    // 结果同时落盘：本程序是窗口子系统（-mwindows），从控制台启动时未必能拿到 stdout
    {
        std::wstring report;
        for (const std::wstring& line : g_lines) {
            report += line;
            report += L"\r\n";
        }
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path = exePath;
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) path.resize(slash + 1);
        path += L"selftest-report.txt";
        const std::string bytes = util::WideToUtf8(report);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
        }
    }
    return g_fail == 0 ? 0 : 1;
}

bool ExportDemoPng(const std::wstring& path) {
    const State demo = MakeDemoState();
    Gdiplus::Bitmap* bitmap = ui::RenderLayoutBitmap(demo);
    if (bitmap == nullptr) return false;
    std::wstring lower = path;
    for (wchar_t& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    const bool jpeg = lower.size() > 4 && lower.rfind(L".jpg") == lower.size() - 4;
    const bool ok = jpeg ? ui::SaveBitmapAsJpeg(path, bitmap, 0.92f)
                         : ui::SaveBitmapAsPng(path, bitmap);
    delete bitmap;
    return ok;
}

bool ExportDemoMarkdown(const std::wstring& path) {
    const State demo = MakeDemoState();
    const std::wstring text = BuildLayoutMarkdown(demo);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const std::string bytes = util::WideToUtf8(text);
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                              nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

/* ---- CLI：打印存档位置（供排查"存档到底在哪"） ---- */
int PrintStoreInfo() {
    const std::string path = util::WideToUtf8(StorePath());
    std::fputs("存档路径: ", stdout);
    std::fputs(path.c_str(), stdout);
    std::fputs(StoreIsPortable() ? "\n存放方式: 便携（exe 同目录）\n"
                                 : "\n存放方式: 程序目录不可写，已退回 %APPDATA%\\RackDraw\\\n",
               stdout);
    return 0;
}

}  // namespace rack
