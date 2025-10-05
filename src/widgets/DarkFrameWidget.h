#pragma once
#include "core/IWidget.h"
#include <windows.h>
#include <vector>

class DarkFrameWidget : public IWidget {
public:
    DarkFrameWidget(HINSTANCE hInstance);
    ~DarkFrameWidget() override;

    bool Initialize() override;
    bool Start() override;
    void Stop() override;
    void Shutdown() override;
    std::wstring GetName() const override { return L"DarkFrameWidget"; }
    std::wstring GetDescription() const override { return L"Dark Frame Container Widget"; }
    bool IsRunning() const override { return isRunning_; }
    std::wstring GetWidgetVersion() const override { return L"1.0.0"; }

    // 框架管理
    void CreateFrame(int x = 100, int y = 100, int width = 300, int height = 400);
    void DeleteFrame(HWND hwnd);

private:
    struct DarkFrame {
        HWND hwnd;                   // 框架視窗句柄
        POINT position;              // 位置
        SIZE size;                   // 大小
    };

    std::vector<DarkFrame> frames_;
    HINSTANCE hInstance_;
    const wchar_t* windowClassName_ = L"DarkFrameWidgetClass";
    bool classRegistered_ = false;
    bool isRunning_ = false;

    // 視窗相關
    bool RegisterWindowClass();
    void UnregisterWindowClass();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 繪製
    void PaintFrame(HWND hwnd, HDC hdc);

    // 輔助函式
    DarkFrame* FindFrame(HWND hwnd);

    // 拖曳和調整大小
    struct {
        bool isDragging = false;
        bool isResizing = false;
        POINT dragOffset = { 0, 0 };
        HWND activeFrame = nullptr;
    } dragState_;

    // 滑鼠事件
    void OnLButtonDown(DarkFrame* frame, int x, int y);
    void OnMouseMove(int x, int y);
    void OnLButtonUp();

    // 右鍵選單
    void ShowFrameContextMenu(DarkFrame* frame, int x, int y);

    // 儲存/載入
    void SaveConfiguration();
    void LoadConfiguration();
    std::wstring GetConfigFilePath();

    // 調整大小輔助函式
    bool IsInResizeArea(const RECT& rc, int x, int y);

    // 選單命令 ID
    enum {
        IDM_DELETE_FRAME = 5000,
        IDM_NEW_FRAME = 5001
    };

    DarkFrame* selectedFrame_ = nullptr;
};
