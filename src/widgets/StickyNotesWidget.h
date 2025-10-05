#pragma once
#include "core/IWidget.h"
#include <windows.h>
#include <vector>
#include <string>

class StickyNotesWidget : public IWidget {
public:
    StickyNotesWidget(HINSTANCE hInstance);
    ~StickyNotesWidget() override;

    bool Initialize() override;
    bool Start() override;
    void Stop() override;
    void Shutdown() override;
    std::wstring GetName() const override { return L"StickyNotesWidget"; }
    std::wstring GetDescription() const override { return L"Desktop Sticky Notes Widget"; }
    bool IsRunning() const override { return isRunning_; }
    std::wstring GetWidgetVersion() const override { return L"1.0.0"; }

    // 便簽管理
    void CreateStickyNote(int x = 100, int y = 100);
    void DeleteStickyNote(HWND hwnd);
    void ClearAllNotes();  // 清除所有便簽

private:
    struct StickyNote {
        HWND hwnd;                   // 便簽視窗句柄
        std::wstring content;        // 便簽內容
        COLORREF color;              // 便簽顏色
        POINT position;              // 位置
        SIZE size;                   // 大小
        HWND hEdit;                  // 編輯框句柄
        HWND hBtnBold;               // 粗體按鈕（現在是+按鈕）
        HWND hBtnItalic;             // 斜體按鈕（未使用）
        HWND hBtnUnderline;          // 底線按鈕（未使用）
        HWND hBtnFontSize;           // 字體大小按鈕
        HWND hBtnColor;              // 顏色按鈕
        HWND hBtnPin;                // 釘選按鈕
        HWND hBtnSettings;           // 設定按鈕（×按鈕）
        int fontSize = 20;           // 當前字體大小（預設20px）
        bool isPinned = false;       // 是否釘選（鎖定不能移動）
    };

    // 預設便簽顏色（Windows 風格）
    enum NoteColor {
        YELLOW = 0,      // 黃色（預設）
        GREEN,           // 綠色
        PINK,            // 粉色
        PURPLE,          // 紫色
        BLUE,            // 藍色
        GRAY,            // 灰色
        WHITE,           // 白色
        ORANGE,          // 橘色
        COLOR_COUNT
    };

    struct ColorInfo {
        COLORREF color;
        const wchar_t* name;
    };

    static const ColorInfo NOTE_COLORS[COLOR_COUNT];

    std::vector<StickyNote> notes_;
    HINSTANCE hInstance_;
    const wchar_t* windowClassName_ = L"StickyNoteWidgetClass";
    bool classRegistered_ = false;
    bool isRunning_ = false;
    bool isShuttingDown_ = false;  // 防止在關閉時重複保存

    // 視窗相關
    bool RegisterWindowClass();
    void UnregisterWindowClass();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    // 繪製
    void PaintNote(HWND hwnd, HDC hdc);
    void CreateNoteControls(StickyNote* note);
    void UpdateControlsLayout(StickyNote* note);

    // 工具列功能
    void OnBoldClick(StickyNote* note);
    void OnItalicClick(StickyNote* note);
    void OnUnderlineClick(StickyNote* note);
    void OnSettingsClick(StickyNote* note);

    // 格式化
    void ApplyTextFormat(HWND hEdit, DWORD mask, DWORD effects, bool enable);

    // 輔助函式
    StickyNote* FindNote(HWND hwnd);
    COLORREF GetNoteColor(NoteColor color);

    // 拖曳狀態
    struct {
        bool isDragging = false;
        POINT dragOffset = { 0, 0 };
        HWND draggedNote = nullptr;
    } dragState_;

    // 滑鼠事件
    void OnLButtonDown(StickyNote* note, int x, int y);
    void OnMouseMove(int x, int y);
    void OnLButtonUp();

    // 右鍵選單
    void ShowNoteContextMenu(StickyNote* note, int x, int y);
    void ShowFontSizeMenu(StickyNote* note, int x, int y);
    void ShowColorMenu(StickyNote* note, int x, int y);

    // 儲存/載入
    void SaveConfiguration();
    void LoadConfiguration();
    std::wstring GetConfigFilePath();

    // 選單命令 ID
    enum {
        IDM_DELETE_NOTE = 3000,
        IDM_NEW_NOTE = 3001,
        IDM_COLOR_YELLOW = 3010,
        IDM_COLOR_GREEN = 3011,
        IDM_COLOR_PINK = 3012,
        IDM_COLOR_PURPLE = 3013,
        IDM_COLOR_BLUE = 3014,
        IDM_COLOR_GRAY = 3015,
        IDM_COLOR_WHITE = 3016,
        IDM_COLOR_ORANGE = 3017,

        // 工具列按鈕
        IDC_BTN_BOLD = 4001,
        IDC_BTN_ITALIC = 4002,
        IDC_BTN_UNDERLINE = 4003,
        IDC_BTN_FONTSIZE = 4004,
        IDC_BTN_COLOR = 4005,
        IDC_BTN_PIN = 4006,
        IDC_BTN_SETTINGS = 4007,

        // 字體大小選單 (10個選項: 10-28px)
        IDM_FONTSIZE_10 = 4010,
        IDM_FONTSIZE_12 = 4011,
        IDM_FONTSIZE_14 = 4012,
        IDM_FONTSIZE_16 = 4013,
        IDM_FONTSIZE_18 = 4014,
        IDM_FONTSIZE_20 = 4015,
        IDM_FONTSIZE_22 = 4016,
        IDM_FONTSIZE_24 = 4017,
        IDM_FONTSIZE_26 = 4018,
        IDM_FONTSIZE_28 = 4019
    };

    StickyNote* selectedNote_ = nullptr;
};
