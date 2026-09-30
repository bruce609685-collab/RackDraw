#include "core/markdown.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app_info.h"
#include "core/catalog.h"

namespace rack {
namespace {

// 导出文档「按类别统计」的行的顺序（与目录树一致）；Custom 固定放最后
Category CategoryAt(int index) {
    switch (index) {
        case 0: return Category::Router;
        case 1: return Category::Switch;
        case 2: return Category::Fw;
        case 3: return Category::Wireless;
        case 4: return Category::Small;
        case 5: return Category::Monitor;
        case 6: return Category::Accessory;
        default: return Category::Custom;
    }
}

std::wstring Trim(const std::wstring& s) {
    size_t begin = 0;
    size_t end = s.size();
    auto space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'\u3000';
    };
    while (begin < end && space(s[begin])) ++begin;
    while (end > begin && space(s[end - 1])) --end;
    return s.substr(begin, end - begin);
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    return lines;
}

// 去掉首尾 | 后按未转义的 | 切分（与 HTML 对照版 parseTableRow 同规则）
bool ParseTableRow(const std::wstring& line, std::vector<std::wstring>* cells) {
    const std::wstring text = Trim(line);
    if (text.empty() || text[0] != L'|') return false;

    size_t begin = 1;
    size_t end = text.size();
    if (end > begin && text[end - 1] == L'|') --end;

    cells->clear();
    std::wstring cell;
    for (size_t i = begin; i < end; ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\\' && i + 1 < end) {
            cell.push_back(ch);
            cell.push_back(text[i + 1]);
            ++i;
            continue;
        }
        if (ch == L'|') {
            cells->push_back(Trim(cell));
            cell.clear();
            continue;
        }
        cell.push_back(ch);
    }
    cells->push_back(Trim(cell));
    return true;
}

bool IsSeparatorRow(const std::wstring& line) {
    const std::wstring text = Trim(line);
    if (text.size() < 2 || text[0] != L'|') return false;
    for (wchar_t ch : text) {
        if (ch != L'|' && ch != L'-' && ch != L' ' && ch != L':') return false;
    }
    return true;
}

int FirstInt(const std::wstring& s, bool* found) {
    size_t at = 0;
    while (at < s.size() && (s[at] < L'0' || s[at] > L'9')) ++at;
    if (at >= s.size()) {
        if (found) *found = false;
        return 0;
    }
    if (found) *found = true;
    return static_cast<int>(std::wcstol(s.substr(at).c_str(), nullptr, 10));
}

bool Contains(const std::wstring& haystack, const wchar_t* needle) {
    return haystack.find(needle) != std::wstring::npos;
}

int IndexOf(const std::vector<std::wstring>& cells, const wchar_t* name) {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i] == name) return static_cast<int>(i);
    }
    return -1;
}

std::wstring Join(const std::vector<std::wstring>& cells, int index) {
    if (index < 0 || static_cast<size_t>(index) >= cells.size()) return std::wstring();
    return cells[static_cast<size_t>(index)];
}

}  // namespace

int CharWidth(wchar_t ch) {
    const unsigned code = static_cast<unsigned>(ch);
    const bool wide =
        (code >= 0x1100 && code <= 0x115F) ||
        (code >= 0x2E80 && code <= 0xA4CF) ||
        (code >= 0xAC00 && code <= 0xD7A3) ||
        (code >= 0xF900 && code <= 0xFAFF) ||
        (code >= 0xFE30 && code <= 0xFE6F) ||
        (code >= 0xFF00 && code <= 0xFF60) ||
        (code >= 0xFFE0 && code <= 0xFFE6);
    return wide ? 2 : 1;
}

int DispWidth(const std::wstring& s) {
    int width = 0;
    for (wchar_t ch : s) width += CharWidth(ch);
    return width;
}

std::wstring FitLabel(const std::wstring& name, int span) {
    if (DispWidth(name) <= span) return name;
    std::wstring out;
    int width = 0;
    for (wchar_t ch : name) {
        const int cw = CharWidth(ch);
        if (width + cw > span - 1) break;   // 留 1 列给省略号
        out.push_back(ch);
        width += cw;
    }
    return out + L"…";
}

std::wstring MdEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t ch : s) {
        switch (ch) {
            case L'\\': out += L"\\\\"; break;
            case L'|': out += L"\\|"; break;
            case L'*': out += L"\\*"; break;
            case L'_': out += L"\\_"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::wstring MdUnescape(const std::wstring& s) {
    std::wstring stage1;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            const wchar_t next = s[i + 1];
            if (next == L'|' || next == L'*' || next == L'_') {
                stage1.push_back(next);
                ++i;
                continue;
            }
        }
        stage1.push_back(s[i]);
    }
    std::wstring out;
    for (size_t i = 0; i < stage1.size(); ++i) {
        if (stage1[i] == L'\\' && i + 1 < stage1.size() && stage1[i + 1] == L'\\') {
            out.push_back(L'\\');
            ++i;
            continue;
        }
        out.push_back(stage1[i]);
    }
    return out;
}

std::wstring FileStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[24] = {0};
    std::swprintf(buf, 24, L"%04d%02d%02d_%02d%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

std::wstring LocalStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[40] = {0};
    std::swprintf(buf, 40, L"%d/%d/%d %02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring MarkdownFileName(const State& s) {
    return std::wstring(L"布设_") + RackText(s.rack) + L"_" + FileStamp() + L".md";
}

std::wstring BuildAsciiMap(const State& s) {
    const int n = RackU(s.rack);
    const int cw = 5;
    const int width = kHGrid * cw;
    std::wstring out;

    // 顶部设备顺序与导出表一致：自下而上、同 U 自左向右
    std::vector<const Device*> sorted;
    for (const Device& d : s.devices) sorted.push_back(&d);
    std::sort(sorted.begin(), sorted.end(), [&s](const Device* a, const Device* b) {
        const int ua = BottomU(s, *a);
        const int ub = BottomU(s, *b);
        if (ua != ub) return ua < ub;
        return a->col < b->col;
    });

    out += L"```\n";
    out += L"    ┌";
    out.append(static_cast<size_t>(width), L'─');
    out += L"┐\n";

    for (int u = n; u >= 1; --u) {
        std::vector<wchar_t> cells(static_cast<size_t>(width), L' ');
        std::vector<bool> hidden(static_cast<size_t>(width), false);
        /* 分两遍画：先铺满所有设备的机身，再统一写名称——
           否则后画设备的机身会把先前设备的名称擦掉（结果会依赖设备顺序） */
        for (const Device* d : sorted) {
            if (u < BottomU(s, *d) || u > TopU(s, *d)) continue;
            // 夹紧到网格范围：设备数据若越界（外部构造的状态）也不能越界写
            int x0 = d->col * cw;
            int x1 = (d->col + d->w) * cw;
            if (x0 < 0) x0 = 0;
            if (x1 > width) x1 = width;
            if (x0 >= x1) continue;
            for (int x = x0; x < x1; ++x) cells[static_cast<size_t>(x)] = L'█';
        }
        for (const Device* d : sorted) {
            if (u != TopU(s, *d)) continue;
            int x0 = d->col * cw;
            int x1 = (d->col + d->w) * cw;
            if (x0 < 0) x0 = 0;
            if (x1 > width) x1 = width;
            if (x0 >= x1) continue;
            const std::wstring text = FitLabel(d->name, x1 - x0);
            int at = x0 + (x1 - x0 - DispWidth(text)) / 2;
            if (at < x0) at = x0;
            for (wchar_t ch : text) {
                const int charW = CharWidth(ch);
                if (at + charW > x1 || at < 0 || at >= width) break;
                cells[static_cast<size_t>(at)] = ch;
                // 全角占两列：后一列不再输出，否则整行显示宽度会被撑长
                if (charW == 2 && at + 1 < width) hidden[static_cast<size_t>(at + 1)] = true;
                at += charW;
            }
        }
        out += (u < 10 ? L" " : L"") + std::to_wstring(u) + L"U │";
        for (int x = 0; x < width; ++x) {
            if (!hidden[static_cast<size_t>(x)]) out.push_back(cells[static_cast<size_t>(x)]);
        }
        out += L"│\n";
    }

    out += L"    └";
    out.append(static_cast<size_t>(width), L'─');
    out += L"┘\n";

    // 宽度标尺：0 / 1/2 / 1 对齐框内列位（行首 "42U │" 占 5 列，标尺同样缩进 5 列）
    std::vector<wchar_t> ruler(static_cast<size_t>(width) + 1, L' ');
    const int marks[3] = {0, width / 2, width};
    const wchar_t* texts[3] = {L"0", L"1/2", L"1"};
    for (int i = 0; i < 3; ++i) {
        const size_t len = std::wcslen(texts[i]);
        size_t at = static_cast<size_t>(marks[i]);
        if (at + len > ruler.size()) at = ruler.size() - len;
        for (size_t k = 0; k < len; ++k) ruler[at + k] = texts[i][k];
    }
    out += L"     ";
    for (wchar_t ch : ruler) out.push_back(ch);
    out += L"  宽度占比\n```";
    return out;
}

std::wstring BuildLayoutMarkdown(const State& s) {
    const int n = RackU(s.rack);
    const int used = UsedU(s);

    std::vector<const Device*> sorted;
    for (const Device& d : s.devices) sorted.push_back(&d);
    std::sort(sorted.begin(), sorted.end(), [&s](const Device* a, const Device* b) {
        const int ua = BottomU(s, *a);
        const int ub = BottomU(s, *b);
        if (ua != ub) return ua < ub;
        return a->col < b->col;
    });

    std::wstring L;
    auto line = [&L](const std::wstring& text) { L += text; L += L"\n"; };

    line(L"# 弱电机柜设备布设表");
    line(L"");
    line(L"## 一、基本信息");
    line(L"");
    line(L"| 项目 | 内容 |");
    line(L"|---|---|");
    line(std::wstring(L"| 机柜规格 | ") + RackText(s.rack) + L" 标准机柜 |");
    line(std::wstring(L"| 卡位版式 | ") +
         (s.mode == SnapMode::U ? L"标准 U 数版式" : L"螺丝孔位版式") + L" |");
    line(L"| 设备总数 | " + std::to_wstring(s.devices.size()) + L" 台 |");
    line(L"| 高度占用 | " + std::to_wstring(used) + L"U / " + std::to_wstring(n) + L"U |");
    line(L"| 导出时间 | " + LocalStamp() + L" |");
    line(L"");

    line(L"## 二、设备清单（按 U 位自下而上）");
    line(L"");
    // 表头始终输出（空机柜也输出）；「顶边孔位」保证孔位版式下同 U 内偏移也能还原
    line(L"| 序号 | U 位 | 顶边孔位 | 横向位置 | 设备名称 | 宽度 | 高度 | 占用空间 |");
    line(L"|---|---|---|---|---|---|---|---|");
    if (sorted.empty()) {
        line(L"（机柜为空）");
    } else {
        int index = 1;
        for (const Device* d : sorted) {
            line(L"| " + std::to_wstring(index++) + L" | " + std::to_wstring(TopU(s, *d)) + L"U | 第 " +
                 std::to_wstring(d->hole + 1) + L" 孔 | " + ColText(*d) + L" | " + MdEscape(d->name) +
                 L" | " + WidthText(d->w) + L" | " + std::to_wstring(d->hU) + L"U | " + SpanText(*d) + L" |");
        }
    }
    line(L"");

    line(L"## 三、U 位占用对照图");
    line(L"");
    line(BuildAsciiMap(s));
    line(L"");

    line(L"## 四、空间统计");
    line(L"");
    line(L"| 统计项 | 数值 |");
    line(L"|---|---|");
    line(L"| 机柜总容量 | " + std::to_wstring(n) + L"U |");
    line(L"| 已占用高度 | " + std::to_wstring(used) + L"U |");
    line(L"| 剩余可用 | " + std::to_wstring(n - used) + L"U |");
    {
        wchar_t buf[32] = {0};
        const double rate = n > 0 ? (static_cast<double>(used) * 100.0 / n) : 0.0;
        std::swprintf(buf, 32, L"%.1f", rate);
        line(std::wstring(L"| 空间利用率 | ") + buf + L"% |");
    }
    line(L"| 设备总数 | " + std::to_wstring(s.devices.size()) + L" 台 |");
    line(L"");
    line(L"### 按类别统计");
    line(L"");
    line(L"| 类别 | 数量 | 占用高度 |");
    line(L"|---|---|---|");
    int sumCount = 0;
    int sumU = 0;
    for (int i = 0; i < 8; ++i) {
        const Category cat = CategoryAt(i);
        int count = 0;
        int height = 0;
        for (const Device& d : s.devices) {
            if (d.cat == cat) {
                ++count;
                height += d.hU;
            }
        }
        sumCount += count;
        sumU += height;
        line(std::wstring(L"| ") + CatLabel(cat) + L" | " + std::to_wstring(count) + L" | " +
             std::to_wstring(height) + L"U |");
    }
    line(L"| **合计** | **" + std::to_wstring(sumCount) + L"** | **" + std::to_wstring(sumU) + L"U** |");
    line(L"");

    line(L"## 五、备注");
    line(L"");
    line(std::wstring(L"<!-- 由 ") + APP_NAME_CN + L" v" + APP_VERSION + L" 自动生成 -->");
    line(L"");
    return L;
}

ImportResult ImportLayout(const std::wstring& text, const State& current) {
    ImportResult result;
    const std::vector<std::wstring> lines = SplitLines(text);

    /* 1. 机柜规格与版式 */
    std::wstring rackText;
    std::wstring modeText;
    for (const std::wstring& ln : lines) {
        std::vector<std::wstring> cells;
        if (!ParseTableRow(ln, &cells) || cells.size() < 2) continue;
        if (cells[0] == L"机柜规格") rackText = cells[1];
        if (cells[0] == L"卡位版式") modeText = cells[1];
    }
    bool found = false;
    const int rackNumber = FirstInt(rackText, &found);
    if (!found) {
        result.error = L"× 未找到「机柜规格」信息，文件可能不是 RackDraw 布设文件";
        return result;
    }
    RackKind targetRack = RackKind::U42;
    if (!RackFromText(std::to_wstring(rackNumber) + L"U", &targetRack)) {
        result.error = L"× 不支持的机柜规格：" + std::to_wstring(rackNumber) + L"U";
        return result;
    }
    const SnapMode targetMode = Contains(modeText, L"螺丝孔位") ? SnapMode::Hole : SnapMode::U;
    const int targetRackU = RackU(targetRack);
    const int targetHoles = TotalHoles(targetRack);

    /* 2. 设备清单表 */
    int headerIndex = -1;
    std::vector<std::wstring> header;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::vector<std::wstring> cells;
        if (!ParseTableRow(lines[i], &cells)) continue;
        if (IndexOf(cells, L"U 位") >= 0 && IndexOf(cells, L"设备名称") >= 0) {
            headerIndex = static_cast<int>(i);
            header = cells;
            break;
        }
    }
    const bool emptyRack = Contains(text, L"（机柜为空）");
    if (headerIndex < 0 && !emptyRack) {
        result.error = L"× 未找到设备清单表（缺少「U 位」「设备名称」表头），文件可能不是 RackDraw 布设文件";
        return result;
    }

    struct Parsed {
        int u = 0;
        int holeFromFile = -1;
        int hU = 1;
        int w = 6;
        std::wstring pos;
        std::wstring name;
    };
    std::vector<Parsed> parsed;

    if (headerIndex >= 0) {
        const int iU = IndexOf(header, L"U 位");
        const int iPos = IndexOf(header, L"横向位置");
        const int iName = IndexOf(header, L"设备名称");
        const int iW = IndexOf(header, L"宽度");
        const int iH = IndexOf(header, L"高度");
        const int iHole = IndexOf(header, L"顶边孔位");

        std::wstring missing;
        const wchar_t* required[5] = {L"U 位", L"横向位置", L"设备名称", L"宽度", L"高度"};
        const int requiredIndex[5] = {iU, iPos, iName, iW, iH};
        for (int i = 0; i < 5; ++i) {
            if (requiredIndex[i] < 0) {
                if (!missing.empty()) missing += L"、";
                missing += required[i];
            }
        }
        if (!missing.empty()) {
            result.error = L"× 设备清单缺少必需列：" + missing;
            return result;
        }

        const int need = std::max(std::max(iU, iPos), std::max(std::max(iName, iW), iH));
        for (size_t i = static_cast<size_t>(headerIndex) + 1; i < lines.size(); ++i) {
            const std::wstring trimmed = Trim(lines[i]);
            if (trimmed.empty() || trimmed[0] != L'|') {
                if (!parsed.empty()) break;
                continue;
            }
            if (IsSeparatorRow(trimmed)) continue;
            std::vector<std::wstring> cells;
            if (!ParseTableRow(trimmed, &cells)) continue;
            if (static_cast<int>(cells.size()) <= need) continue;

            bool okU = false, okH = false;
            Parsed p;
            p.u = FirstInt(Join(cells, iU), &okU);
            p.hU = ClampH(FirstInt(Join(cells, iH), &okH));
            const std::wstring wText = Join(cells, iW);
            int w = 0;
            if (wText.find(L"标准全宽") != std::wstring::npos) w = 6;
            else if (wText.find(L"1/2") != std::wstring::npos) w = 3;
            else if (wText.find(L"1/3") != std::wstring::npos) w = 2;
            if (!okU || !okH || w == 0) continue;
            p.w = w;
            p.pos = MdUnescape(Join(cells, iPos));
            p.name = MdUnescape(Join(cells, iName));

            if (iHole >= 0) {
                const std::wstring holeText = Join(cells, iHole);
                const size_t marker = holeText.find(L"第");
                if (marker != std::wstring::npos) {
                    bool okHole = false;
                    const int value = FirstInt(holeText.substr(marker + 1), &okHole);
                    if (okHole && value >= 1) p.holeFromFile = value - 1;
                }
            }
            parsed.push_back(p);
        }
    }

    /* 3. 名称匹配 + 坐标换算（全部通过后才切换状态） */
    struct Staged {
        Category cat = Category::Custom;
        std::wstring name;
        int w = 6;
        int hU = 1;
        int col = 0;
        int hole = 0;
        int srcU = 0;
        std::wstring srcPos;
        bool unknown = false;
        bool clamped = false;
    };
    std::vector<Staged> staged;

    for (const Parsed& p : parsed) {
        const NameMatch match = MatchDeviceName(p.name, current.archive);
        int hole = (p.holeFromFile >= 0) ? p.holeFromFile
                                         : (targetRackU - p.u) * kHolesPerU;
        if (p.holeFromFile < 0 && targetMode == SnapMode::U) {
            const int units = static_cast<int>(
                std::lround(static_cast<double>(hole) / kHolesPerU));
            hole = units * kHolesPerU;
        }

        int col = 0;
        const size_t colMarker = p.pos.find(L"col");
        bool explicitCol = false;
        if (colMarker != std::wstring::npos) {
            bool okCol = false;
            const int value = FirstInt(p.pos.substr(colMarker + 3), &okCol);
            if (okCol) {
                col = value;
                explicitCol = true;
            }
        }
        if (!explicitCol) {
            if (Contains(p.pos, L"左")) col = 0;
            else if (Contains(p.pos, L"右")) col = kHGrid - p.w;
            else col = static_cast<int>(std::lround((kHGrid - p.w) / 2.0));
        }
        const int rawCol = col;
        col = ClampInt(col, 0, kHGrid - p.w);

        const int maxHole = targetHoles - p.hU * kHolesPerU;
        const int rawHole = hole;
        hole = ClampInt(hole, 0, maxHole > 0 ? maxHole : 0);

        Staged s;
        s.cat = match.cat;
        s.name = match.name;
        s.w = p.w;
        s.hU = p.hU;
        s.col = col;
        s.hole = hole;
        s.srcU = p.u;
        s.srcPos = p.pos;
        s.unknown = match.unknown;
        s.clamped = (col != rawCol || hole != rawHole);
        staged.push_back(s);
    }

    /* 4. 原子切换：先换机柜/版式，再逐台落位（冲突自动顺延） */
    result.next = current;
    result.next.rack = targetRack;
    result.next.mode = targetMode;
    result.next.devices.clear();
    result.next.selectedId = 0;

    const int step = (targetMode == SnapMode::U) ? kHolesPerU : 1;
    for (const Staged& s : staged) {
        Device dev;
        dev.cat = s.cat;
        dev.name = s.name;
        dev.w = s.w;
        dev.hU = s.hU;
        const int maxHole = TotalHoles(result.next.rack) - s.hU * kHolesPerU;
        const SlotProbe probe = FindFreeHole(result.next, s.col, s.hole, dev, step, maxHole);
        if (!probe.found) {
            ++result.skipCount;
            result.rows.push_back({ReportKind::Error, L"× 跳过（无可用卡位）：" + s.name});
            continue;
        }
        dev.id = result.next.seq++;
        dev.col = s.col;
        dev.hole = probe.hole;
        result.next.devices.push_back(dev);
        ++result.okCount;
        if (probe.adjusted || s.clamped) {
            ++result.adjCount;
            result.rows.push_back({ReportKind::Warn, L"▲ 位置调整：" + s.name});
            result.rows.push_back({ReportKind::Indent,
                                   L"　 " + std::to_wstring(s.srcU) + L"U " + s.srcPos + L" → " +
                                   std::to_wstring(HoleToUText(result.next.rack, probe.hole)) + L"U " +
                                   ColText(dev)});
        }
        if (s.unknown) {
            ++result.unkCount;
            result.rows.push_back({ReportKind::Unknown,
                                   L"? 未知设备：" + s.name + L"（按 " + std::to_wstring(s.hU) + L"U " +
                                   WidthText(s.w) + L" 创建为自定义设备）"});
        }
    }

    std::wstring summary = L"√ 成功导入：" + std::to_wstring(result.okCount) + L" 台";
    if (result.adjCount > 0) summary += L"　▲ 位置调整：" + std::to_wstring(result.adjCount) + L" 台";
    if (result.unkCount > 0) summary += L"　? 未知设备：" + std::to_wstring(result.unkCount) + L" 台";
    if (result.skipCount > 0) summary += L"　× 跳过：" + std::to_wstring(result.skipCount) + L" 台";
    result.rows.insert(result.rows.begin(), {ReportKind::Ok, summary});

    result.canUndo = !current.devices.empty();
    result.ok = true;
    return result;
}

}  // namespace rack
