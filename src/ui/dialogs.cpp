#include "ui/dialogs.h"

#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>

#include "app/app_info.h"
#include "ui/theme.h"

namespace ui {
namespace {

const wchar_t* kDialogClass = L"RackDrawDialog";

/* ---------------- 弹窗控件 ID ---------------- */
const int kIdOk = IDOK;          // 1
const int kIdCancel = IDCANCEL;  // 2（IsDialogMessage 会把 Esc 映射到它）
const int kIdUndo = 3;

const int kIdName = 100;
const int kIdW1 = 111;
const int kIdW2 = 112;
const int kIdW3 = 113;
const int kIdHeight = 120;
const int kIdSave = 130;
const int kIdReport = 200;
const int kIdPromptEdit = 300;

/* ---------------- 弹窗状态（同一时刻只有一个模态弹窗） ---------------- */
struct CustomState {
    CustomDeviceInput* data = nullptr;
    bool accepted = false;
    HWND edit = nullptr;
    HWND combo = nullptr;
    HWND check = nullptr;
    HWND radios[3] = {nullptr, nullptr, nullptr};
};

struct ReportState {
    ReportAction action = ReportAction::None;
};

struct PromptState {
    bool accepted = false;
    HWND edit = nullptr;
    std::wstring value;   // 在窗口销毁前取出的文本
};

CustomState* g_custom = nullptr;
ReportState* g_report = nullptr;
PromptState* g_prompt = nullptr;

// 取值在窗口销毁前完成（定义见下）
void CaptureCustomValues(CustomState* state);

/* ---------------- 基础工具 ---------------- */
HFONT UiFont() {
    static HFONT font = nullptr;
    if (font == nullptr) {
        font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    }
    return font;
}

HBRUSH WhiteBrush() {
    static HBRUSH brush = nullptr;
    if (brush == nullptr) brush = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
    return brush;
}

void ApplyFont(HWND control) { SendMessageW(control, WM_SETFONT, (WPARAM)UiFont(), TRUE); }

HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y,
               int w, int h, int id) {
    HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
    if (child != nullptr) ApplyFont(child);
    return child;
}

// 模态循环：禁用属主窗口并接管消息，直到弹窗销毁。
// previousFocus 是弹窗创建之前属主线程里持有焦点的窗口（调用方在创建弹窗之前抓取）。
// 注意：手写循环没有系统 DialogBox 的那层处理，关闭弹窗后必须自己把激活状态与焦点还回去，
// 否则前台会落到 Z 序里的下一个窗口——用户看到的是「点确定/取消后切回上一个软件」。
void RunModal(HWND dlg, HWND owner, HWND previousFocus) {
    if (owner != nullptr) EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        SetForegroundWindow(owner);
        if (previousFocus != nullptr && IsWindow(previousFocus)) {
            SetFocus(previousFocus);
        } else {
            SetFocus(owner);
        }
    }
}

void CenterOnOwner(HWND dlg, HWND owner) {
    RECT dlgRect, ownerRect;
    GetWindowRect(dlg, &dlgRect);
    if (owner == nullptr || !GetWindowRect(owner, &ownerRect)) {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(dlg, nullptr, (screenW - (dlgRect.right - dlgRect.left)) / 2,
                     (screenH - (dlgRect.bottom - dlgRect.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        return;
    }
    const int x = ownerRect.left +
                  ((ownerRect.right - ownerRect.left) - (dlgRect.right - dlgRect.left)) / 2;
    const int y = ownerRect.top +
                  ((ownerRect.bottom - ownerRect.top) - (dlgRect.bottom - dlgRect.top)) / 2;
    SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

bool BelongsTo(HWND hwnd, HWND child) {
    return child != nullptr && IsWindow(child) && GetParent(child) == hwnd;
}

LRESULT CALLBACK ModalWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wparam), &rc, WhiteBrush());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(WhiteBrush());
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (g_custom != nullptr && BelongsTo(hwnd, g_custom->edit)) {
                if (id == kIdOk) {
                    CaptureCustomValues(g_custom);   // 先取值，再销毁
                    g_custom->accepted = true;
                    DestroyWindow(hwnd);
                } else if (id == kIdCancel) {
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            if (g_report != nullptr) {
                if (id == kIdUndo) {
                    g_report->action = ReportAction::Undo;
                    DestroyWindow(hwnd);
                } else if (id == kIdCancel) {
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            if (g_prompt != nullptr && BelongsTo(hwnd, g_prompt->edit)) {
                if (id == kIdOk) {
                    wchar_t buffer[256] = {0};
                    GetWindowTextW(g_prompt->edit, buffer, 256);   // 先取值，再销毁
                    g_prompt->value = buffer;
                    g_prompt->accepted = true;
                    DestroyWindow(hwnd);
                } else if (id == kIdCancel) {
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void EnsureDialogClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ModalWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = WhiteBrush();
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);
    registered = true;
}

HWND CreateModalWindow(HWND owner, const std::wstring& caption, int clientW, int clientH) {
    EnsureDialogClass();
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc = {0, 0, Scale(clientW), Scale(clientH)};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_CONTROLPARENT);
    HWND dlg = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, kDialogClass,
                               caption.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top, owner, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    if (dlg != nullptr) CenterOnOwner(dlg, owner);
    return dlg;
}

int WidthFromRadios(CustomState* state) {
    // 注意：默认回退到「标准全宽」，与弹窗初始选中项一致
    if (state->radios[1] != nullptr &&
        SendMessageW(state->radios[1], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return 3;
    }
    if (state->radios[2] != nullptr &&
        SendMessageW(state->radios[2], BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return 2;
    }
    return 6;
}

// 取值必须在窗口销毁前完成：控件一旦销毁，BM_GETCHECK / CB_GETCURSEL / GetWindowText
// 全部读不到内容（曾因此把自定义设备固定成 1/3 宽 1U、且勾选框读成未勾选）
void CaptureCustomValues(CustomState* state) {
    if (state == nullptr || state->data == nullptr) return;
    wchar_t buffer[128] = {0};
    if (state->edit != nullptr) GetWindowTextW(state->edit, buffer, 128);
    state->data->name = rack::TrimName(buffer, L"自定义设备");
    state->data->w = rack::ClampW(WidthFromRadios(state));
    const int selection = state->combo != nullptr
                              ? static_cast<int>(SendMessageW(state->combo, CB_GETCURSEL, 0, 0))
                              : 0;
    state->data->hU = rack::ClampH(selection >= 0 ? selection + 1 : 1);
    state->data->saveToArchive =
        state->check != nullptr && SendMessageW(state->check, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

}  // namespace

/* ================================================================
 * 文件选择
 * ================================================================ */
bool PickSavePath(HWND owner, const std::wstring& defaultName, const wchar_t* filterLabel,
                  const wchar_t* filterSpec, const wchar_t* defaultExt, std::wstring* out) {
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }
    const COMDLG_FILTERSPEC spec = {filterLabel, filterSpec};
    dialog->SetFileTypes(1, &spec);
    if (defaultExt != nullptr) dialog->SetDefaultExtension(defaultExt);
    if (!defaultName.empty()) dialog->SetFileName(defaultName.c_str());
    dialog->SetOptions(FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    bool ok = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                *out = path;
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
}

bool PickOpenPath(HWND owner, const wchar_t* filterLabel, const wchar_t* filterSpec,
                  const wchar_t* defaultExt, std::wstring* out) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }
    const COMDLG_FILTERSPEC spec = {filterLabel, filterSpec};
    dialog->SetFileTypes(1, &spec);
    if (defaultExt != nullptr) dialog->SetDefaultExtension(defaultExt);
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    bool ok = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                *out = path;
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
}

/* ================================================================
 * 自定义设备弹窗（F-07）
 * ================================================================ */
bool ShowCustomDeviceDialog(HWND owner, CustomDeviceInput* inOut) {
    const HWND previousFocus = GetFocus();   // 关窗后要还回去的焦点
    HWND dlg = CreateModalWindow(owner, L"自定义设备", 340, 252);
    if (dlg == nullptr) return false;

    CustomState state;
    state.data = inOut;
    g_custom = &state;

    const int pad = Scale(16);
    int y = Scale(14);
    MakeChild(dlg, L"STATIC", L"名称", SS_LEFT, pad, y + Scale(4), Scale(60), Scale(18), -1);
    state.edit = MakeChild(dlg, L"EDIT", inOut->name.c_str(), WS_BORDER | ES_AUTOHSCROLL,
                           pad + Scale(64), y, Scale(236), Scale(24), kIdName);
    SendMessageW(state.edit, EM_SETLIMITTEXT, rack::kNameMax, 0);
    y += Scale(38);

    MakeChild(dlg, L"STATIC", L"宽度", SS_LEFT, pad, y + Scale(4), Scale(60), Scale(18), -1);
    const wchar_t* labels[3] = {L"标准全宽", L"1/2 宽（小型）", L"1/3 宽（mini）"};
    for (int i = 0; i < 3; ++i) {
        state.radios[i] = MakeChild(dlg, L"BUTTON", labels[i],
                                    BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), pad + Scale(64),
                                    y + i * Scale(21), Scale(236), Scale(19), kIdW1 + i);
    }
    y += Scale(70);

    MakeChild(dlg, L"STATIC", L"高度", SS_LEFT, pad, y + Scale(4), Scale(60), Scale(18), -1);
    state.combo = MakeChild(dlg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, pad + Scale(64), y,
                            Scale(120), Scale(220), kIdHeight);
    for (int i = 1; i <= rack::kHMax; ++i) {
        const std::wstring text = std::to_wstring(i) + L"U";
        SendMessageW(state.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(state.combo, CB_SETCURSEL, static_cast<WPARAM>(rack::ClampH(inOut->hU) - 1), 0);
    y += Scale(36);

    state.check = MakeChild(dlg, L"BUTTON", L"保存到「我的设备」（跨会话复用）", BS_AUTOCHECKBOX,
                            pad + Scale(4), y, Scale(300), Scale(20), kIdSave);
    SendMessageW(state.check, BM_SETCHECK, inOut->saveToArchive ? BST_CHECKED : BST_UNCHECKED, 0);

    const int buttonY = Scale(210);
    MakeChild(dlg, L"BUTTON", L"取消", BS_PUSHBUTTON, Scale(340) - pad - Scale(80), buttonY,
              Scale(80), Scale(28), kIdCancel);
    MakeChild(dlg, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, Scale(340) - pad - Scale(168), buttonY,
              Scale(80), Scale(28), kIdOk);

    if (inOut->w == 3) SendMessageW(state.radios[1], BM_SETCHECK, BST_CHECKED, 0);
    else if (inOut->w == 2) SendMessageW(state.radios[2], BM_SETCHECK, BST_CHECKED, 0);
    else SendMessageW(state.radios[0], BM_SETCHECK, BST_CHECKED, 0);

    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.edit);
    RunModal(dlg, owner, previousFocus);

    // 取值已在 WM_COMMAND 里完成（窗口销毁前），这里只判断结果
    const bool accepted = state.accepted;
    g_custom = nullptr;
    return accepted;
}

/* ================================================================
 * 导入报告弹窗（F-12，含撤销导入）
 * ================================================================ */
ReportAction ShowImportReportDialog(HWND owner, const std::wstring& title,
                                    const std::vector<rack::ReportRow>& rows, bool canUndo) {
    const HWND previousFocus = GetFocus();
    HWND dlg = CreateModalWindow(owner, title, 470, 400);
    if (dlg == nullptr) return ReportAction::None;

    ReportState state;
    g_report = &state;

    std::wstring text;
    for (const rack::ReportRow& row : rows) {
        text += row.text;
        text += L"\r\n";
    }

    const int pad = Scale(16);
    HWND edit = MakeChild(dlg, L"EDIT", L"",
                          WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                          pad, Scale(14), Scale(470) - pad * 2, Scale(320), kIdReport);
    SetWindowTextW(edit, text.c_str());

    const int buttonY = Scale(348);
    MakeChild(dlg, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, Scale(470) - pad - Scale(80), buttonY,
              Scale(80), Scale(28), kIdCancel);
    if (canUndo) {
        MakeChild(dlg, L"BUTTON", L"撤销导入", BS_PUSHBUTTON, Scale(470) - pad - Scale(176),
                  buttonY, Scale(88), Scale(28), kIdUndo);
    }

    ShowWindow(dlg, SW_SHOW);
    RunModal(dlg, owner, previousFocus);
    const ReportAction action = state.action;
    g_report = nullptr;
    return action;
}

/* ================================================================
 * 文本输入 / 消息
 * ================================================================ */
bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label,
                const std::wstring& initial, int maxLength, std::wstring* out) {
    const HWND previousFocus = GetFocus();
    HWND dlg = CreateModalWindow(owner, title, 320, 132);
    if (dlg == nullptr) return false;

    PromptState state;
    g_prompt = &state;
    const int pad = Scale(16);
    MakeChild(dlg, L"STATIC", label.c_str(), SS_LEFT, pad, Scale(14), Scale(288), Scale(18), -1);
    state.edit = MakeChild(dlg, L"EDIT", initial.c_str(), WS_BORDER | ES_AUTOHSCROLL, pad,
                           Scale(38), Scale(288), Scale(24), kIdPromptEdit);
    SendMessageW(state.edit, EM_SETLIMITTEXT, static_cast<WPARAM>(maxLength), 0);
    MakeChild(dlg, L"BUTTON", L"取消", BS_PUSHBUTTON, Scale(320) - pad - Scale(80), Scale(86),
              Scale(80), Scale(28), kIdCancel);
    MakeChild(dlg, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, Scale(320) - pad - Scale(168), Scale(86),
              Scale(80), Scale(28), kIdOk);

    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.edit);
    RunModal(dlg, owner, previousFocus);

    const bool accepted = state.accepted;
    if (accepted) *out = state.value;   // 文本已在窗口销毁前取出
    g_prompt = nullptr;
    return accepted;
}

void ShowInfo(HWND owner, const std::wstring& text) {
    MessageBoxW(owner, text.c_str(), APP_NAME_CN, MB_OK | MB_ICONINFORMATION);
}

void ShowError(HWND owner, const std::wstring& text) {
    MessageBoxW(owner, text.c_str(), APP_NAME_CN, MB_OK | MB_ICONERROR);
}

bool Confirm(HWND owner, const std::wstring& text, const std::wstring& title) {
    return MessageBoxW(owner, text.c_str(), title.empty() ? APP_NAME_CN : title.c_str(),
                       MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}

}  // namespace ui
