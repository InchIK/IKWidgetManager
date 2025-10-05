#include "StickyNotesWidget.h"
#include "core/WidgetExport.h"
#include <windowsx.h>
#include <richedit.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <ole2.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")

// Windows 自黏便箋風格顏色
const StickyNotesWidget::ColorInfo StickyNotesWidget::NOTE_COLORS[COLOR_COUNT] = {
    { RGB(255, 242, 157), L"黃色(Y)" },      // 黃色
    { RGB(204, 235, 197), L"綠色(G)" },      // 綠色
    { RGB(253, 228, 235), L"粉色(P)" },      // 粉色
    { RGB(230, 224, 241), L"紫色(P)" },      // 紫色
    { RGB(207, 228, 248), L"藍色(B)" },      // 藍色
    { RGB(224, 224, 224), L"灰色(G)" },      // 灰色
    { RGB(255, 255, 255), L"白色(W)" },      // 白色
    { RGB(255, 228, 196), L"橘色(O)" }       // 橘色
};

StickyNotesWidget::StickyNotesWidget(HINSTANCE hInstance)
    : hInstance_(hInstance) {
}

StickyNotesWidget::~StickyNotesWidget() {
    Shutdown();
}

bool StickyNotesWidget::Initialize() {
    if (!RegisterWindowClass()) {
        return false;
    }
    LoadConfiguration();
    return true;
}

bool StickyNotesWidget::Start() {
    if (isRunning_) return true;

    // 為從配置載入的便簽創建視窗，或顯示已存在的視窗
    for (auto& note : notes_) {
        if (!note.hwnd) {
            // 創建視窗（從配置載入的便簽）
            note.hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW,
                windowClassName_,
                L"Sticky Note",
                WS_POPUP | WS_VISIBLE | WS_SIZEBOX,
                note.position.x, note.position.y, note.size.cx, note.size.cy,
                nullptr,
                nullptr,
                hInstance_,
                this
            );

            if (note.hwnd) {
                // 設置窗口 Z-order 為底層，不擋住其他視窗但不受顯示桌面影響
                SetWindowPos(note.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

                DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
                DwmSetWindowAttribute(note.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

                // 擴展框架到客戶區，移除視窗邊框但保留陰影
                MARGINS margins = { 0, 0, 0, 1 };
                DwmExtendFrameIntoClientArea(note.hwnd, &margins);

                CreateNoteControls(&note);

                if (note.hEdit) {
                    SetWindowTextW(note.hEdit, note.content.c_str());
                }

                // 設置釘選按鈕狀態
                if (note.hBtnPin) {
                    SetWindowTextW(note.hBtnPin, note.isPinned ? L"🔒" : L"🔓");
                }
            }
        } else {
            // 顯示已存在的視窗
            ShowWindow(note.hwnd, SW_SHOW);
        }
    }

    isRunning_ = true;
    return true;
}

void StickyNotesWidget::Stop() {
    if (!isRunning_) return;

    SaveConfiguration();

    for (auto& note : notes_) {
        if (note.hwnd) {
            ShowWindow(note.hwnd, SW_HIDE);
        }
    }

    isRunning_ = false;
}

void StickyNotesWidget::Shutdown() {
    // 先保存配置（在銷毀視窗之前）
    SaveConfiguration();

    // 標記為關閉中，防止後續的 SaveConfiguration 調用
    isShuttingDown_ = true;
    isRunning_ = false;

    // 銷毀所有視窗
    for (auto& note : notes_) {
        if (note.hwnd) {
            DestroyWindow(note.hwnd);
        }
    }
    notes_.clear();

    UnregisterWindowClass();
}

bool StickyNotesWidget::RegisterWindowClass() {
    if (classRegistered_) {
        return true;
    }

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(StickyNotesWidget*);
    wcex.hInstance = hInstance_;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = windowClassName_;

    if (!RegisterClassExW(&wcex)) {
        return false;
    }

    classRegistered_ = true;
    return true;
}

void StickyNotesWidget::UnregisterWindowClass() {
    if (classRegistered_) {
        UnregisterClassW(windowClassName_, hInstance_);
        classRegistered_ = false;
    }
}

LRESULT CALLBACK StickyNotesWidget::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StickyNotesWidget* widget = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        widget = reinterpret_cast<StickyNotesWidget*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
    } else {
        widget = reinterpret_cast<StickyNotesWidget*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (widget) {
        return widget->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT StickyNotesWidget::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StickyNote* note = FindNote(hwnd);

    switch (msg) {
    case WM_NCCALCSIZE: {
        // 移除視窗邊框，讓客戶區填滿整個視窗
        if (wParam == TRUE) {
            // 返回 0 表示客戶區 = 整個視窗區域
            return 0;
        }
        break;
    }

    case WM_NCPAINT:
        // 阻止非客戶區繪製（移除灰白框）
        return 0;

    case WM_NCACTIVATE:
        // 阻止非客戶區啟用狀態變化的繪製
        return TRUE;

    case WM_NCHITTEST: {
        // 處理調整大小的邊緣檢測和拖動
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);

        const int RESIZE_BORDER = 8;
        const int TOOLBAR_HEIGHT = 40;

        // 如果釘選了，不允許拖動和調整大小
        if (note && note->isPinned) {
            return HTCLIENT;
        }

        // 檢查是否在邊緣（用於調整大小）
        bool onLeft = pt.x < RESIZE_BORDER;
        bool onRight = pt.x > rc.right - RESIZE_BORDER;
        bool onTop = pt.y < RESIZE_BORDER;
        bool onBottom = pt.y > rc.bottom - RESIZE_BORDER;

        // 邊角優先
        if (onTop && onLeft) return HTTOPLEFT;
        if (onTop && onRight) return HTTOPRIGHT;
        if (onBottom && onLeft) return HTBOTTOMLEFT;
        if (onBottom && onRight) return HTBOTTOMRIGHT;

        // 邊緣
        if (onLeft) return HTLEFT;
        if (onRight) return HTRIGHT;
        if (onTop) return HTTOP;
        if (onBottom) return HTBOTTOM;

        // 工具列區域可以拖動（但要避開按鈕）
        if (pt.y < TOOLBAR_HEIGHT) {
            // 檢查是否點擊在按鈕上
            HWND hwndChild = ChildWindowFromPoint(hwnd, pt);
            if (hwndChild == hwnd || hwndChild == nullptr) {
                return HTCAPTION;  // 可以拖動
            }
        }

        // 其他區域正常處理（可以在編輯框中輸入）
        return HTCLIENT;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintNote(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (note) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnLButtonDown(note, x, y);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (dragState_.isDragging) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseMove(x, y);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (dragState_.isDragging) {
            OnLButtonUp();
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        // 右鍵選單（選擇顏色）
        if (note) {
            POINT pt;
            GetCursorPos(&pt);
            ShowNoteContextMenu(note, pt.x, pt.y);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (note) {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDC_BTN_BOLD:
                OnBoldClick(note);
                break;
            case IDC_BTN_ITALIC:
                OnItalicClick(note);
                break;
            case IDC_BTN_UNDERLINE:
                OnUnderlineClick(note);
                break;
            case IDC_BTN_SETTINGS:
                OnSettingsClick(note);
                break;
            case IDC_BTN_FONTSIZE: {
                // 顯示字體大小選單
                RECT btnRect;
                GetWindowRect(note->hBtnFontSize, &btnRect);
                ShowFontSizeMenu(note, btnRect.left, btnRect.bottom);
                break;
            }
            case IDC_BTN_COLOR: {
                // 顯示顏色選單
                RECT btnRect;
                GetWindowRect(note->hBtnColor, &btnRect);
                ShowColorMenu(note, btnRect.left, btnRect.bottom);
                break;
            }
            case IDC_BTN_PIN: {
                // 切換釘選狀態
                note->isPinned = !note->isPinned;
                // 更新按鈕文字（🔒鎖定 / 🔓解鎖）
                SetWindowTextW(note->hBtnPin, note->isPinned ? L"🔒" : L"🔓");
                SaveConfiguration();
                break;
            }
            case IDM_DELETE_NOTE:
                DeleteStickyNote(hwnd);
                break;
            case IDM_NEW_NOTE:
                CreateStickyNote();
                break;
            case IDM_FONTSIZE_10:
            case IDM_FONTSIZE_12:
            case IDM_FONTSIZE_14:
            case IDM_FONTSIZE_16:
            case IDM_FONTSIZE_18:
            case IDM_FONTSIZE_20:
            case IDM_FONTSIZE_22:
            case IDM_FONTSIZE_24:
            case IDM_FONTSIZE_26:
            case IDM_FONTSIZE_28: {
                // 計算字體大小 (10-28)
                int fontSize = 10 + (wmId - IDM_FONTSIZE_10) * 2;
                note->fontSize = fontSize;

                // 更新編輯框字體
                if (note->hEdit) {
                    HFONT hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(note->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                    InvalidateRect(note->hEdit, nullptr, TRUE);
                }
                SaveConfiguration();
                break;
            }
            case IDM_COLOR_YELLOW:
            case IDM_COLOR_GREEN:
            case IDM_COLOR_PINK:
            case IDM_COLOR_PURPLE:
            case IDM_COLOR_BLUE:
            case IDM_COLOR_GRAY:
            case IDM_COLOR_WHITE:
            case IDM_COLOR_ORANGE:
                note->color = NOTE_COLORS[wmId - IDM_COLOR_YELLOW].color;
                if (note->hEdit) {
                    SendMessageW(note->hEdit, EM_SETBKGNDCOLOR, 0, note->color);
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                SaveConfiguration();
                break;
            }
        }
        return 0;
    }

    case WM_SIZE: {
        if (note) {
            UpdateControlsLayout(note);
        }
        return 0;
    }

    case WM_DESTROY: {
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// RichEdit 子類化處理，限制只能貼上純文字
LRESULT CALLBACK StickyNotesWidget::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    switch (msg) {
    case WM_KEYDOWN: {
        // 攔截 Ctrl+V
        if (wParam == 'V' && GetKeyState(VK_CONTROL) < 0) {
            // 手動觸發我們自訂的貼上邏輯
            SendMessageW(hwnd, WM_PASTE, 0, 0);
            return 0;
        }
        break;
    }

    case WM_PASTE: {
        // 完全攔截貼上，只允許純文字
        // 清空剪貼簿中的所有非文字格式
        if (OpenClipboard(hwnd)) {
            std::wstring plainText;

            // 提取純文字
            if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* text = static_cast<wchar_t*>(GlobalLock(hData));
                    if (text) {
                        plainText = text;
                        GlobalUnlock(hData);
                    }
                }
            } else if (IsClipboardFormatAvailable(CF_TEXT)) {
                HANDLE hData = GetClipboardData(CF_TEXT);
                if (hData) {
                    char* text = static_cast<char*>(GlobalLock(hData));
                    if (text) {
                        int len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
                        wchar_t* wtext = new wchar_t[len];
                        MultiByteToWideChar(CP_ACP, 0, text, -1, wtext, len);
                        plainText = wtext;
                        delete[] wtext;
                        GlobalUnlock(hData);
                    }
                }
            }

            CloseClipboard();

            // 只插入純文字
            if (!plainText.empty()) {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)plainText.c_str());
            }
        }
        return 0; // 完全阻止預設的貼上行為
    }

    case EM_PASTESPECIAL: {
        // RichEdit 可能會使用 EM_PASTESPECIAL，也要攔截
        // 強制使用純文字格式
        return SendMessageW(hwnd, WM_PASTE, 0, 0);
    }

    case WM_DROPFILES: {
        // 阻止拖放檔案
        return 0;
    }

    case WM_NCDESTROY:
        // 移除子類化
        RemoveWindowSubclass(hwnd, EditSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void StickyNotesWidget::CreateNoteControls(StickyNote* note) {
    LoadLibraryW(L"Msftedit.dll");

    const int TOOLBAR_HEIGHT = 40;
    const int BTN_SIZE = 20;
    const int BTN_MARGIN = 5;

    RECT rc;
    GetClientRect(note->hwnd, &rc);

    // 建立 + 按鈕（新建便簽）在左上角
    note->hBtnBold = CreateWindowExW(
        0, L"BUTTON", L"+",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        BTN_MARGIN, BTN_MARGIN, BTN_SIZE, BTN_SIZE,
        note->hwnd,
        (HMENU)(INT_PTR)IDM_NEW_NOTE,
        hInstance_,
        nullptr
    );

    // A 按鈕（字體大小）
    note->hBtnFontSize = CreateWindowExW(
        0, L"BUTTON", L"A",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        BTN_MARGIN + BTN_SIZE + BTN_MARGIN, BTN_MARGIN, BTN_SIZE, BTN_SIZE,
        note->hwnd,
        (HMENU)(INT_PTR)IDC_BTN_FONTSIZE,
        hInstance_,
        nullptr
    );

    // ● 按鈕（顏色選擇）
    note->hBtnColor = CreateWindowExW(
        0, L"BUTTON", L"●",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        BTN_MARGIN + (BTN_SIZE + BTN_MARGIN) * 2, BTN_MARGIN, BTN_SIZE, BTN_SIZE,
        note->hwnd,
        (HMENU)(INT_PTR)IDC_BTN_COLOR,
        hInstance_,
        nullptr
    );

    // 🔓 按鈕（釘選/取消釘選）
    note->hBtnPin = CreateWindowExW(
        0, L"BUTTON", L"🔓",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        BTN_MARGIN + (BTN_SIZE + BTN_MARGIN) * 3, BTN_MARGIN, BTN_SIZE, BTN_SIZE,
        note->hwnd,
        (HMENU)(INT_PTR)IDC_BTN_PIN,
        hInstance_,
        nullptr
    );

    // × 按鈕（關閉）在右上角
    note->hBtnSettings = CreateWindowExW(
        0, L"BUTTON", L"×",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        rc.right - BTN_SIZE - BTN_MARGIN, BTN_MARGIN, BTN_SIZE, BTN_SIZE,
        note->hwnd,
        (HMENU)(INT_PTR)IDM_DELETE_NOTE,
        hInstance_,
        nullptr
    );

    // 隱藏其他按鈕（不需要）
    note->hBtnItalic = nullptr;
    note->hBtnUnderline = nullptr;

    // 設定按鈕字型
    HFONT hButtonFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");

    SendMessageW(note->hBtnBold, WM_SETFONT, (WPARAM)hButtonFont, TRUE);
    SendMessageW(note->hBtnFontSize, WM_SETFONT, (WPARAM)hButtonFont, TRUE);

    HFONT hColorFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    SendMessageW(note->hBtnColor, WM_SETFONT, (WPARAM)hColorFont, TRUE);

    HFONT hPinFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");
    SendMessageW(note->hBtnPin, WM_SETFONT, (WPARAM)hPinFont, TRUE);

    HFONT hCloseFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    SendMessageW(note->hBtnSettings, WM_SETFONT, (WPARAM)hCloseFont, TRUE);

    // 建立 RichEdit 編輯框（填滿整個便簽，帶垂直滾動條）
    note->hEdit = CreateWindowExW(
        0, L"RICHEDIT50W", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL | WS_VSCROLL,
        0, TOOLBAR_HEIGHT, rc.right, rc.bottom - TOOLBAR_HEIGHT,
        note->hwnd,
        nullptr,
        hInstance_,
        nullptr
    );

    if (note->hEdit) {
        // 設定背景顏色
        SendMessageW(note->hEdit, EM_SETBKGNDCOLOR, 0, note->color);

        // 設定字型 (預設 20px)
        HFONT hFont = CreateFontW(note->fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
        SendMessageW(note->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 移除邊框
        SetWindowLongW(note->hEdit, GWL_EXSTYLE, 0);
        SetWindowLongW(note->hEdit, GWL_STYLE,
            GetWindowLongW(note->hEdit, GWL_STYLE) & ~WS_BORDER);

        // 設定文字
        SetWindowTextW(note->hEdit, note->content.c_str());

        // 取得編輯框大小來設定邊距
        RECT rcEdit;
        GetClientRect(note->hEdit, &rcEdit);

        // 設定邊距矩形 (左8, 上5, 右-8, 下0)
        RECT rcMargin;
        rcMargin.left = 8;
        rcMargin.top = 5;
        rcMargin.right = rcEdit.right - 8;
        rcMargin.bottom = rcEdit.bottom;
        SendMessageW(note->hEdit, EM_SETRECT, 0, (LPARAM)&rcMargin);

        // 禁用 RichEdit 的 OLE 拖放功能（防止拖放圖片和檔案）
        SendMessageW(note->hEdit, EM_SETOLECALLBACK, 0, 0);

        // 撤銷 RichEdit 註冊為拖放目標
        RevokeDragDrop(note->hEdit);

        // 子類化 RichEdit 以攔截貼上事件（只允許純文字）
        SetWindowSubclass(note->hEdit, EditSubclassProc, 0, 0);
    }
}

void StickyNotesWidget::UpdateControlsLayout(StickyNote* note) {
    RECT rc;
    GetClientRect(note->hwnd, &rc);

    const int TOOLBAR_HEIGHT = 40;
    const int BTN_SIZE = 20;
    const int BTN_MARGIN = 5;

    if (note->hBtnBold) {
        SetWindowPos(note->hBtnBold, nullptr,
            BTN_MARGIN, BTN_MARGIN,
            BTN_SIZE, BTN_SIZE, SWP_NOZORDER);
    }

    if (note->hBtnFontSize) {
        SetWindowPos(note->hBtnFontSize, nullptr,
            BTN_MARGIN + BTN_SIZE + BTN_MARGIN, BTN_MARGIN,
            BTN_SIZE, BTN_SIZE, SWP_NOZORDER);
    }

    if (note->hBtnColor) {
        SetWindowPos(note->hBtnColor, nullptr,
            BTN_MARGIN + (BTN_SIZE + BTN_MARGIN) * 2, BTN_MARGIN,
            BTN_SIZE, BTN_SIZE, SWP_NOZORDER);
    }

    if (note->hBtnPin) {
        SetWindowPos(note->hBtnPin, nullptr,
            BTN_MARGIN + (BTN_SIZE + BTN_MARGIN) * 3, BTN_MARGIN,
            BTN_SIZE, BTN_SIZE, SWP_NOZORDER);
    }

    if (note->hBtnSettings) {
        SetWindowPos(note->hBtnSettings, nullptr,
            rc.right - BTN_SIZE - BTN_MARGIN, BTN_MARGIN,
            BTN_SIZE, BTN_SIZE, SWP_NOZORDER);
    }

    if (note->hEdit) {
        SetWindowPos(note->hEdit, nullptr,
            0, TOOLBAR_HEIGHT,
            rc.right, rc.bottom - TOOLBAR_HEIGHT,
            SWP_NOZORDER);
    }
}

void StickyNotesWidget::PaintNote(HWND hwnd, HDC hdc) {
    StickyNote* note = FindNote(hwnd);
    if (!note) return;

    RECT rc;
    GetClientRect(hwnd, &rc);

    // 使用記憶體 DC 進行雙緩衝
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 繪製便簽背景
    HBRUSH hBrush = CreateSolidBrush(note->color);
    FillRect(memDC, &rc, hBrush);
    DeleteObject(hBrush);

    // 繪製工具列分隔線
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN oldPen = (HPEN)SelectObject(memDC, hPen);
    MoveToEx(memDC, 0, 39, nullptr);
    LineTo(memDC, rc.right, 39);
    SelectObject(memDC, oldPen);
    DeleteObject(hPen);

    // 複製到螢幕
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    // 清理
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void StickyNotesWidget::CreateStickyNote(int x, int y) {
    StickyNote note;
    note.color = NOTE_COLORS[YELLOW].color;  // 預設黃色
    note.position = { x, y };
    note.size = { 300, 300 };
    note.content = L"";
    note.fontSize = 20;  // 預設字體大小
    note.isPinned = false;  // 預設未釘選

    // 建立便簽視窗 - Windows Sticky Notes 風格（純色背景，無標題列）
    note.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        windowClassName_,
        L"Sticky Note",
        WS_POPUP | WS_VISIBLE | WS_SIZEBOX,  // 無標題列，純彈出視窗
        x, y, note.size.cx, note.size.cy,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!note.hwnd) {
        return;
    }

    // 設置窗口 Z-order 為底層，不擋住其他視窗但不受顯示桌面影響
    SetWindowPos(note.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 設定圓角效果 (Windows 11)
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(note.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    // 擴展框架到客戶區，移除視窗邊框但保留陰影
    MARGINS margins = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(note.hwnd, &margins);

    // 建立控制項
    CreateNoteControls(&note);

    notes_.push_back(note);
    SaveConfiguration();
}

void StickyNotesWidget::DeleteStickyNote(HWND hwnd) {
    for (auto it = notes_.begin(); it != notes_.end(); ++it) {
        if (it->hwnd == hwnd) {
            DestroyWindow(hwnd);
            notes_.erase(it);
            SaveConfiguration();
            break;
        }
    }
}

void StickyNotesWidget::ClearAllNotes() {
    // 確認對話框
    int result = MessageBoxW(nullptr,
        L"確定要清除所有便簽嗎？\n\n此操作將：\n1. 刪除所有便簽窗口\n2. 清除所有便簽內容\n3. 刪除配置文件\n\n此操作無法復原！",
        L"確認清除",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

    if (result != IDYES) {
        return;
    }

    // 刪除所有便簽窗口
    for (auto& note : notes_) {
        if (note.hwnd) {
            DestroyWindow(note.hwnd);
        }
    }

    // 清空列表
    notes_.clear();

    // 刪除配置文件
    std::wstring configPath = GetConfigFilePath();
    if (!configPath.empty()) {
        DeleteFileW(configPath.c_str());
    }

    MessageBoxW(nullptr, L"已清除所有便簽！", L"完成", MB_OK | MB_ICONINFORMATION);
}

void StickyNotesWidget::OnBoldClick(StickyNote* note) {
    if (!note || !note->hEdit) return;

    CHARFORMAT2W cf = { 0 };
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_BOLD;

    SendMessageW(note->hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool isBold = (cf.dwEffects & CFE_BOLD) != 0;

    ApplyTextFormat(note->hEdit, CFM_BOLD, CFE_BOLD, !isBold);
}

void StickyNotesWidget::OnItalicClick(StickyNote* note) {
    if (!note || !note->hEdit) return;

    CHARFORMAT2W cf = { 0 };
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_ITALIC;

    SendMessageW(note->hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool isItalic = (cf.dwEffects & CFE_ITALIC) != 0;

    ApplyTextFormat(note->hEdit, CFM_ITALIC, CFE_ITALIC, !isItalic);
}

void StickyNotesWidget::OnUnderlineClick(StickyNote* note) {
    if (!note || !note->hEdit) return;

    CHARFORMAT2W cf = { 0 };
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_UNDERLINE;

    SendMessageW(note->hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool isUnderline = (cf.dwEffects & CFE_UNDERLINE) != 0;

    ApplyTextFormat(note->hEdit, CFM_UNDERLINE, CFE_UNDERLINE, !isUnderline);
}

void StickyNotesWidget::OnSettingsClick(StickyNote* note) {
    if (!note) return;

    RECT btnRect;
    GetWindowRect(note->hBtnSettings, &btnRect);
    ShowNoteContextMenu(note, btnRect.left, btnRect.bottom);
}

void StickyNotesWidget::ApplyTextFormat(HWND hEdit, DWORD mask, DWORD effects, bool enable) {
    CHARFORMAT2W cf = { 0 };
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = mask;
    cf.dwEffects = enable ? effects : 0;

    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

StickyNotesWidget::StickyNote* StickyNotesWidget::FindNote(HWND hwnd) {
    for (auto& note : notes_) {
        if (note.hwnd == hwnd) {
            return &note;
        }
    }
    return nullptr;
}

COLORREF StickyNotesWidget::GetNoteColor(NoteColor color) {
    if (color >= 0 && color < COLOR_COUNT) {
        return NOTE_COLORS[color].color;
    }
    return NOTE_COLORS[YELLOW].color;
}

void StickyNotesWidget::OnLButtonDown(StickyNote* note, int x, int y) {
    // 如果釘選了，不允許拖動
    if (note->isPinned) {
        return;
    }

    // 只在工具列區域才能拖曳
    if (y < 40) {
        // 檢查是否點擊在按鈕上
        POINT pt = { x, y };
        ClientToScreen(note->hwnd, &pt);
        HWND hwndChild = ChildWindowFromPoint(note->hwnd, POINT{ x, y });

        if (hwndChild == note->hwnd || hwndChild == nullptr) {
            dragState_.isDragging = true;
            dragState_.draggedNote = note->hwnd;
            dragState_.dragOffset.x = x;
            dragState_.dragOffset.y = y;
            SetCapture(note->hwnd);
        }
    }
}

void StickyNotesWidget::OnMouseMove(int /*x*/, int /*y*/) {
    if (dragState_.isDragging && dragState_.draggedNote) {
        POINT pt;
        GetCursorPos(&pt);

        int newX = pt.x - dragState_.dragOffset.x;
        int newY = pt.y - dragState_.dragOffset.y;

        SetWindowPos(dragState_.draggedNote, nullptr, newX, newY, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);

        // 更新位置記錄
        StickyNote* note = FindNote(dragState_.draggedNote);
        if (note) {
            note->position.x = newX;
            note->position.y = newY;
        }
    }
}

void StickyNotesWidget::OnLButtonUp() {
    if (dragState_.isDragging) {
        ReleaseCapture();
        dragState_.isDragging = false;
        dragState_.draggedNote = nullptr;
        SaveConfiguration();
    }
}

void StickyNotesWidget::ShowNoteContextMenu(StickyNote* note, int x, int y) {
    selectedNote_ = note;

    HMENU hMenu = CreatePopupMenu();

    // 新增便簽
    AppendMenuW(hMenu, MF_STRING, IDM_NEW_NOTE, L"新增便簽");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 顏色子選單 - 加入顏色方塊
    HMENU hColorMenu = CreatePopupMenu();

    for (int i = 0; i < COLOR_COUNT; i++) {
        // 使用 MENUITEMINFOW 來設定選單項目
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_FTYPE;
        mii.fType = MFT_STRING;
        mii.wID = IDM_COLOR_YELLOW + i;

        // 建立帶顏色方塊的文字 (使用 Unicode 方塊符號 ■)
        std::wstring colorText = L"■ ";
        colorText += NOTE_COLORS[i].name;
        mii.dwTypeData = const_cast<LPWSTR>(colorText.c_str());

        InsertMenuItemW(hColorMenu, i, TRUE, &mii);
    }

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hColorMenu, L"變更顏色");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 刪除
    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_NOTE, L"刪除便簽");

    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, note->hwnd, nullptr);
    DestroyMenu(hMenu);
}

void StickyNotesWidget::ShowFontSizeMenu(StickyNote* note, int x, int y) {
    selectedNote_ = note;

    HMENU hMenu = CreatePopupMenu();

    // 字體大小選項 (10個: 10, 12, 14, 16, 18, 20, 22, 24, 26, 28)
    const wchar_t* fontSizes[] = { L"10", L"12", L"14", L"16", L"18", L"20", L"22", L"24", L"26", L"28" };
    for (int i = 0; i < 10; i++) {
        UINT flags = MF_STRING;
        int fontSize = 10 + i * 2;

        // 當前字體大小加上勾選標記
        if (note->fontSize == fontSize) {
            flags |= MF_CHECKED;
        }

        AppendMenuW(hMenu, flags, IDM_FONTSIZE_10 + i, fontSizes[i]);
    }

    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, note->hwnd, nullptr);
    DestroyMenu(hMenu);
}

void StickyNotesWidget::ShowColorMenu(StickyNote* note, int x, int y) {
    selectedNote_ = note;

    HMENU hMenu = CreatePopupMenu();

    // 顏色選項
    for (int i = 0; i < COLOR_COUNT; i++) {
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_FTYPE;
        mii.fType = MFT_STRING;
        mii.wID = IDM_COLOR_YELLOW + i;

        // 建立帶顏色方塊的文字 (使用 Unicode 方塊符號 ■)
        std::wstring colorText = L"■ ";
        colorText += NOTE_COLORS[i].name;
        mii.dwTypeData = const_cast<LPWSTR>(colorText.c_str());

        // 當前顏色加上勾選標記
        if (note->color == NOTE_COLORS[i].color) {
            mii.fMask |= MIIM_STATE;
            mii.fState = MFS_CHECKED;
        }

        InsertMenuItemW(hMenu, i, TRUE, &mii);
    }

    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, note->hwnd, nullptr);
    DestroyMenu(hMenu);
}

std::wstring StickyNotesWidget::GetConfigFilePath() {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring configDir = std::wstring(appDataPath) + L"\\FencesWidget";
        CreateDirectoryW(configDir.c_str(), nullptr);
        return configDir + L"\\sticky_notes_config.json";
    }
    return L"";
}

void StickyNotesWidget::SaveConfiguration() {
    // 如果正在關閉，不要保存（防止覆蓋已保存的配置）
    if (isShuttingDown_) {
        return;
    }

    std::wstring configPath = GetConfigFilePath();
    if (configPath.empty()) return;

    std::wstring config = L"{\n  \"notes\": [\n";

    bool first = true;
    for (size_t i = 0; i < notes_.size(); ++i) {
        auto& note = notes_[i];

        // 如果視窗不存在，跳過
        if (!note.hwnd) continue;

        // 取得編輯框內容
        if (note.hEdit) {
            int len = GetWindowTextLengthW(note.hEdit);
            if (len > 0) {
                wchar_t* buffer = new wchar_t[len + 1];
                GetWindowTextW(note.hEdit, buffer, len + 1);
                notes_[i].content = buffer;
                delete[] buffer;
            } else {
                notes_[i].content = L"";
            }
        }

        // 取得視窗位置和大小
        RECT rc;
        if (!GetWindowRect(note.hwnd, &rc)) continue;

        // 添加逗號分隔符（除了第一個）
        if (!first) {
            config += L",\n";
        }
        first = false;

        config += L"    {\n";
        config += L"      \"x\": " + std::to_wstring(rc.left) + L",\n";
        config += L"      \"y\": " + std::to_wstring(rc.top) + L",\n";
        config += L"      \"width\": " + std::to_wstring(rc.right - rc.left) + L",\n";
        config += L"      \"height\": " + std::to_wstring(rc.bottom - rc.top) + L",\n";
        config += L"      \"color\": " + std::to_wstring(note.color) + L",\n";
        config += L"      \"fontSize\": " + std::to_wstring(note.fontSize) + L",\n";
        config += L"      \"isPinned\": " + std::wstring(note.isPinned ? L"true" : L"false") + L",\n";

        // 轉義內容
        std::wstring escapedContent = note.content;
        size_t pos = 0;
        while ((pos = escapedContent.find(L"\"", pos)) != std::wstring::npos) {
            escapedContent.replace(pos, 1, L"\\\"");
            pos += 2;
        }
        pos = 0;
        while ((pos = escapedContent.find(L"\n", pos)) != std::wstring::npos) {
            escapedContent.replace(pos, 1, L"\\n");
            pos += 2;
        }
        pos = 0;
        while ((pos = escapedContent.find(L"\r", pos)) != std::wstring::npos) {
            escapedContent.replace(pos, 1, L"");
        }

        config += L"      \"content\": \"" + escapedContent + L"\"\n";
        config += L"    }";
    }

    config += L"\n  ]\n}\n";

    std::wofstream file(configPath);
    if (file.is_open()) {
        file << config;
        file.close();
    }
}

void StickyNotesWidget::LoadConfiguration() {
    std::wstring configPath = GetConfigFilePath();
    if (configPath.empty()) return;

    std::wifstream file(configPath);
    if (!file.is_open()) return;

    std::wstringstream buffer;
    buffer << file.rdbuf();
    std::wstring content = buffer.str();
    file.close();

    // 簡易 JSON 解析
    size_t pos = 0;
    while ((pos = content.find(L"{", pos)) != std::wstring::npos) {
        size_t endPos = content.find(L"}", pos);
        if (endPos == std::wstring::npos) break;

        std::wstring noteBlock = content.substr(pos, endPos - pos + 1);

        int x = 100, y = 100, width = 300, height = 300;
        COLORREF color = NOTE_COLORS[YELLOW].color;
        int fontSize = 20;
        bool isPinned = false;
        std::wstring noteContent = L"";

        auto getValue = [](const std::wstring& block, const std::wstring& key) -> std::wstring {
            size_t keyPos = block.find(L"\"" + key + L"\"");
            if (keyPos != std::wstring::npos) {
                size_t colonPos = block.find(L":", keyPos);
                if (colonPos != std::wstring::npos) {
                    size_t valueStart = colonPos + 1;
                    size_t valueEnd = block.find_first_of(L",\n}", valueStart);
                    std::wstring value = block.substr(valueStart, valueEnd - valueStart);

                    value.erase(0, value.find_first_not_of(L" \t\n\r"));
                    value.erase(value.find_last_not_of(L" \t\n\r") + 1);
                    if (!value.empty() && value[0] == L'\"') {
                        value = value.substr(1, value.length() - 2);
                    }
                    return value;
                }
            }
            return L"";
        };

        std::wstring xStr = getValue(noteBlock, L"x");
        std::wstring yStr = getValue(noteBlock, L"y");
        std::wstring widthStr = getValue(noteBlock, L"width");
        std::wstring heightStr = getValue(noteBlock, L"height");
        std::wstring colorStr = getValue(noteBlock, L"color");
        std::wstring fontSizeStr = getValue(noteBlock, L"fontSize");
        std::wstring isPinnedStr = getValue(noteBlock, L"isPinned");
        noteContent = getValue(noteBlock, L"content");

        if (!xStr.empty()) x = std::stoi(xStr);
        if (!yStr.empty()) y = std::stoi(yStr);
        if (!widthStr.empty()) width = std::stoi(widthStr);
        if (!heightStr.empty()) height = std::stoi(heightStr);
        if (!colorStr.empty()) color = std::stoul(colorStr);
        if (!fontSizeStr.empty()) fontSize = std::stoi(fontSizeStr);
        if (!isPinnedStr.empty()) isPinned = (isPinnedStr == L"true");

        // 還原轉義字元
        size_t escPos = 0;
        while ((escPos = noteContent.find(L"\\n", escPos)) != std::wstring::npos) {
            noteContent.replace(escPos, 2, L"\r\n");
            escPos += 2;
        }
        escPos = 0;
        while ((escPos = noteContent.find(L"\\\"", escPos)) != std::wstring::npos) {
            noteContent.replace(escPos, 2, L"\"");
            escPos += 1;
        }

        // 只載入配置數據，不創建視窗（視窗會在 Start() 時創建）
        StickyNote note;
        note.color = color;
        note.position = { x, y };
        note.size = { width, height };
        note.content = noteContent;
        note.fontSize = fontSize;
        note.isPinned = isPinned;
        note.hwnd = nullptr;  // 視窗稍後創建
        note.hEdit = nullptr;
        note.hBtnBold = nullptr;
        note.hBtnItalic = nullptr;
        note.hBtnUnderline = nullptr;
        note.hBtnFontSize = nullptr;
        note.hBtnColor = nullptr;
        note.hBtnPin = nullptr;
        note.hBtnSettings = nullptr;

        notes_.push_back(note);

        pos = endPos + 1;
    }
}

// ==================== DLL 導出函式 ====================

extern "C" {
    WIDGET_API IWidget* CreateWidget(void* params) {
        HINSTANCE hInstance = params ? *(HINSTANCE*)params : GetModuleHandle(nullptr);
        return new StickyNotesWidget(hInstance);
    }

    WIDGET_API void DestroyWidget(IWidget* widget) {
        delete widget;
    }

    WIDGET_API const wchar_t* GetWidgetName() {
        return L"StickyNotesWidget";
    }

    WIDGET_API const wchar_t* GetWidgetVersion() {
        return L"1.0.0";
    }

    WIDGET_API void ExecuteCommand(IWidget* widget, int commandId) {
        if (!widget) return;

        StickyNotesWidget* stickyWidget = dynamic_cast<StickyNotesWidget*>(widget);
        if (!stickyWidget) return;

        switch (commandId) {
            case WIDGET_CMD_CREATE_NEW:
                // 建立新便簽（在螢幕中央偏移）
                stickyWidget->CreateStickyNote(150, 150);
                break;

            case WIDGET_CMD_CLEAR_ALL_DATA:
                // 清除所有便簽
                stickyWidget->ClearAllNotes();
                break;

            default:
                break;
        }
    }
}
