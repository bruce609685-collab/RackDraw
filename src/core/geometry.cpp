#include "core/types.h"

#include <algorithm>
#include <cmath>

namespace rack {
namespace {

bool IsSpace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
           ch == L'\u3000' || ch == 0x0b || ch == 0x0c;
}

}  // namespace

int RackU(RackKind kind) {
    switch (kind) {
        case RackKind::U42: return 42;
        case RackKind::U36: return 36;
        case RackKind::U24: return 24;
    }
    return 42;
}

int TotalHoles(RackKind kind) { return RackU(kind) * kHolesPerU; }

const wchar_t* RackText(RackKind kind) {
    switch (kind) {
        case RackKind::U42: return L"42U";
        case RackKind::U36: return L"36U";
        case RackKind::U24: return L"24U";
    }
    return L"42U";
}

bool RackFromText(const std::wstring& text, RackKind* out) {
    if (out == nullptr) return false;
    if (text == L"42U") { *out = RackKind::U42; return true; }
    if (text == L"36U") { *out = RackKind::U36; return true; }
    if (text == L"24U") { *out = RackKind::U24; return true; }
    return false;
}

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int ClampH(int hU) { return ClampInt(hU, 1, kHMax); }

int ClampW(int w) { return (w == 6 || w == 3 || w == 2) ? w : 6; }

std::wstring TrimName(const std::wstring& raw, const std::wstring& fallback) {
    size_t begin = 0;
    size_t end = raw.size();
    while (begin < end && IsSpace(raw[begin])) ++begin;
    while (end > begin && IsSpace(raw[end - 1])) --end;
    std::wstring trimmed = raw.substr(begin, end - begin);
    if (trimmed.size() > static_cast<size_t>(kNameMax)) trimmed.resize(kNameMax);
    if (trimmed.empty()) return fallback;
    return trimmed;
}

int HoleToUText(RackKind kind, int hole) {
    // 与 HTML 对照版 holeToUText 同式：孔位 → 顶边所在 U（自顶向下 1 起）
    return static_cast<int>(std::floor(
        static_cast<double>(TotalHoles(kind) - hole - 1) / kHolesPerU)) + 1;
}

int TopU(const State& s, const Device& d) { return HoleToUText(s.rack, d.hole); }

int BottomU(const State& s, const Device& d) {
    return HoleToUText(s.rack, d.hole + d.hU * kHolesPerU - 1);
}

int UsedU(const State& s) {
    int sum = 0;
    for (const Device& d : s.devices) sum += d.hU;
    return sum;
}

bool Collides(const State& s, int col, int hole, const Device& dev, int excludeId) {
    const int dh = dev.hU * kHolesPerU;
    for (const Device& d : s.devices) {
        if (d.id == excludeId) continue;
        if (col < d.col + d.w && col + dev.w > d.col &&
            hole < d.hole + d.hU * kHolesPerU && hole + dh > d.hole) {
            return true;
        }
    }
    return false;
}

SlotProbe FindFreeHole(const State& s, int col, int hole, const Device& dev,
                       int step, int maxHole) {
    SlotProbe probe;
    if (!Collides(s, col, hole, dev, 0)) {
        probe.found = true;
        probe.hole = hole;
        probe.adjusted = false;
        return probe;
    }
    const int reach = std::max(maxHole, TotalHoles(s.rack));
    for (int dlt = step; dlt <= reach; dlt += step) {
        const int down = hole + dlt;
        if (down <= maxHole && !Collides(s, col, down, dev, 0)) {
            probe.found = true;
            probe.hole = down;
            probe.adjusted = true;
            return probe;
        }
        const int up = hole - dlt;
        if (up >= 0 && !Collides(s, col, up, dev, 0)) {
            probe.found = true;
            probe.hole = up;
            probe.adjusted = true;
            return probe;
        }
    }
    return probe;
}

std::wstring WidthText(int w) {
    if (w == 6) return L"标准全宽";
    if (w == 3) return L"1/2 宽";
    if (w == 2) return L"1/3 宽";
    return L"标准全宽";
}

std::wstring ColText(const Device& d) {
    if (d.w == kHGrid) return L"居中";
    if (d.col == 0) return L"左侧";
    if (d.col == kHGrid - d.w) return L"右侧";
    return L"col " + std::to_wstring(d.col) + L"-" + std::to_wstring(d.col + d.w - 1);
}

std::wstring SpanText(const Device& d) {
    const wchar_t* span = d.w == 6 ? L"全宽" : (d.w == 3 ? L"1/2" : L"1/3");
    return std::to_wstring(d.hU) + L"U × " + span;
}

SnapResult CalcSnap(const State& s, const Device& dev, const InnerRect& r,
                    double px, double py, int excludeId) {
    SnapResult out;

    const double cw = r.width / kHGrid;
    const double sh = SlotHExact() * r.scale;
    const double offX = dev.w * ColW() * r.scale / 2.0;      // 示意外形中心对齐指针
    const double offY = dev.hU * kUPx * r.scale / 2.0;
    const double tx = px - offX;
    const double ty = py - offY;

    const int maxHole = TotalHoles(s.rack) - dev.hU * kHolesPerU;
    if (maxHole < 0) return out;   // 设备比机柜还高，无法落位

    int col = static_cast<int>(std::lround((tx - r.left - kPadX * r.scale) / cw));
    int hole = static_cast<int>(std::lround((ty - r.top - kPadTop * r.scale) / sh));

    col = ClampInt(col, 0, kHGrid - dev.w);
    hole = ClampInt(hole, 0, maxHole);
    if (s.mode == SnapMode::U) {
        hole = static_cast<int>(std::lround(static_cast<double>(hole) / kHolesPerU)) * kHolesPerU;
    }

    out.valid = true;
    out.col = col;
    out.hole = hole;
    out.ok = !Collides(s, col, hole, dev, excludeId);
    return out;
}

}  // namespace rack
