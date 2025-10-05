#include "core/WidgetManager.h"
#include "core/PluginLoader.h"
#include <windows.h>
#include <iostream>
#include <memory>
#include <vector>

// Global variables
NOTIFYICONDATAW g_nid = { 0 };
HWND g_hControlWindow = nullptr;
const UINT WM_TRAYICON = WM_USER + 1;

// Registry key for auto-start
const wchar_t* REGISTRY_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"DesktopWidgetManager";

// Control window procedure
LRESULT CALLBACK ControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Check if auto-start is enabled
bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH];
    DWORD size = sizeof(value);
    DWORD type;
    bool exists = (RegQueryValueExW(hKey, APP_NAME, nullptr, &type, (LPBYTE)value, &size) == ERROR_SUCCESS);

    RegCloseKey(hKey);
    return exists;
}

// Enable auto-start
bool EnableAutoStart() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Add quotes to handle paths with spaces
    std::wstring quotedPath = L"\"";
    quotedPath += exePath;
    quotedPath += L"\"";

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = RegSetValueExW(hKey, APP_NAME, 0, REG_SZ,
        (const BYTE*)quotedPath.c_str(),
        (DWORD)((quotedPath.length() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

// Disable auto-start
bool DisableAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = RegDeleteValueW(hKey, APP_NAME);
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

// Create system tray icon
bool CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Desktop Widget Manager");

    return Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// Remove system tray icon
void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// Show tray menu
void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    // 設定滑鼠游標為箭頭
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    auto& manager = WidgetManager::GetInstance();

    // Add Widget control items
    AppendMenuW(hMenu, MF_STRING, 1, L"Fences Widget");
    bool fencesEnabled = manager.IsWidgetEnabled(L"FencesWidget");
    CheckMenuItem(hMenu, 1, MF_BYCOMMAND | (fencesEnabled ? MF_CHECKED : MF_UNCHECKED));

    AppendMenuW(hMenu, MF_STRING, 4, L"Sticky Notes Widget");
    bool stickyNotesEnabled = manager.IsWidgetEnabled(L"StickyNotesWidget");
    CheckMenuItem(hMenu, 4, MF_BYCOMMAND | (stickyNotesEnabled ? MF_CHECKED : MF_UNCHECKED));

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Auto-start option
    AppendMenuW(hMenu, MF_STRING, 2, L"開機自動啟動");
    bool autoStartEnabled = IsAutoStartEnabled();
    CheckMenuItem(hMenu, 2, MF_BYCOMMAND | (autoStartEnabled ? MF_CHECKED : MF_UNCHECKED));

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 3, L"自動分類桌面圖示");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 100, L"退出");

    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMenu);

    // Handle menu commands
    switch (cmd) {
    case 1: // Toggle Fences Widget
        if (fencesEnabled) {
            manager.DisableWidget(L"FencesWidget");
        } else {
            manager.EnableWidget(L"FencesWidget");
        }
        break;

    case 2: // Toggle Auto-start
        if (autoStartEnabled) {
            if (DisableAutoStart()) {
                MessageBoxW(hwnd, L"已停用開機自動啟動", L"設定成功", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"停用開機自動啟動失敗", L"錯誤", MB_OK | MB_ICONERROR);
            }
        } else {
            if (EnableAutoStart()) {
                MessageBoxW(hwnd, L"已啟用開機自動啟動", L"設定成功", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"啟用開機自動啟動失敗", L"錯誤", MB_OK | MB_ICONERROR);
            }
        }
        break;

    case 3: // Auto-categorize desktop icons
        {
            auto widget = manager.GetWidget(L"FencesWidget");
            if (widget) {
                auto fencesWidget = std::dynamic_pointer_cast<FencesWidget>(widget);
                if (fencesWidget) {
                    fencesWidget->AutoCategorizeDesktopIcons();
                }
            }
        }
        break;

    case 4: // Toggle Sticky Notes Widget
        if (stickyNotesEnabled) {
            manager.DisableWidget(L"StickyNotesWidget");
        } else {
            manager.EnableWidget(L"StickyNotesWidget");
        }
        break;

    case 100: // Exit
        PostQuitMessage(0);
        break;
    }
}

// Control window procedure
LRESULT CALLBACK ControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Create control window
HWND CreateControlWindow(HINSTANCE hInstance) {
    const wchar_t* className = L"WidgetManagerControl";

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = ControlWindowProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = className;

    if (!RegisterClassExW(&wcex)) {
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"Widget Manager Control",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        nullptr
    );

    return hwnd;
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    // 檢查是否已有實例在運行
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Global\\DesktopWidgetManager_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            L"Desktop Widget Manager 已經在運行中！\n\n請檢查系統托盤圖示。",
            L"程序已運行",
            MB_OK | MB_ICONINFORMATION);
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 0;
    }

    // Initialize COM
    CoInitialize(nullptr);

    // Create control window
    g_hControlWindow = CreateControlWindow(hInstance);
    if (!g_hControlWindow) {
        MessageBoxW(nullptr, L"Failed to create control window", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Create system tray icon
    if (!CreateTrayIcon(g_hControlWindow)) {
        MessageBoxW(nullptr, L"Failed to create system tray icon", L"Error", MB_OK | MB_ICONERROR);
        DestroyWindow(g_hControlWindow);
        CoUninitialize();
        return 1;
    }

    // Initialize Widget manager
    auto& manager = WidgetManager::GetInstance();
    if (!manager.Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize Widget Manager", L"Error", MB_OK | MB_ICONERROR);
        RemoveTrayIcon();
        DestroyWindow(g_hControlWindow);
        CoUninitialize();
        return 1;
    }

    // Register Fences Widget
    auto fencesWidget = std::make_shared<FencesWidget>();
    if (!manager.RegisterWidget(fencesWidget)) {
        MessageBoxW(nullptr, L"Failed to register Fences Widget", L"Error", MB_OK | MB_ICONERROR);
        manager.Shutdown();
        RemoveTrayIcon();
        DestroyWindow(g_hControlWindow);
        CoUninitialize();
        return 1;
    }

    // Register Sticky Notes Widget
    auto stickyNotesWidget = std::make_shared<StickyNotesWidget>(hInstance);
    if (!manager.RegisterWidget(stickyNotesWidget)) {
        MessageBoxW(nullptr, L"Failed to register Sticky Notes Widget", L"Error", MB_OK | MB_ICONERROR);
        manager.Shutdown();
        RemoveTrayIcon();
        DestroyWindow(g_hControlWindow);
        CoUninitialize();
        return 1;
    }

    // Enable Fences Widget by default
    manager.EnableWidget(L"FencesWidget");

    // Enable Sticky Notes Widget by default
    manager.EnableWidget(L"StickyNotesWidget");

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    manager.Shutdown();
    RemoveTrayIcon();
    DestroyWindow(g_hControlWindow);

    // 釋放 Mutex
    if (hMutex) {
        CloseHandle(hMutex);
    }

    CoUninitialize();

    return (int)msg.wParam;
}
