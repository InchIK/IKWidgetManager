#include "core/WidgetManager.h"
#include "core/PluginLoader.h"
#include <windows.h>
#include <iostream>
#include <memory>
#include <vector>
#include <filesystem>
#include <fstream>
#include <shlobj.h>

// Global variables
NOTIFYICONDATAW g_nid = { 0 };
HWND g_hControlWindow = nullptr;
const UINT WM_TRAYICON = WM_USER + 1;
std::vector<PluginInfo> g_loadedPlugins;

// Registry key for auto-start
const wchar_t* REGISTRY_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"DesktopWidgetManager";

// Control window procedure
LRESULT CALLBACK ControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Widget 狀態儲存/載入
std::wstring GetWidgetStateConfigPath() {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring configDir = std::wstring(appDataPath) + L"\\FencesWidget";
        CreateDirectoryW(configDir.c_str(), nullptr);
        return configDir + L"\\widget_states.conf";
    }
    return L"";
}

void SaveWidgetStates(WidgetManager& manager) {
    std::wstring configPath = GetWidgetStateConfigPath();
    if (configPath.empty()) return;

    std::wofstream file(configPath);
    if (!file.is_open()) return;

    for (const auto& plugin : g_loadedPlugins) {
        bool isEnabled = manager.IsWidgetEnabled(plugin.name);
        file << plugin.name << L"=" << (isEnabled ? L"1" : L"0") << L"\n";
    }
    file.close();
}

void LoadWidgetStates(WidgetManager& manager) {
    std::wstring configPath = GetWidgetStateConfigPath();
    if (configPath.empty()) return;

    std::wifstream file(configPath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        size_t pos = line.find(L'=');
        if (pos != std::wstring::npos) {
            std::wstring widgetName = line.substr(0, pos);
            std::wstring stateStr = line.substr(pos + 1);

            bool shouldEnable = (stateStr == L"1");

            // 檢查該 Widget 是否已載入
            bool widgetExists = false;
            for (const auto& plugin : g_loadedPlugins) {
                if (plugin.name == widgetName) {
                    widgetExists = true;
                    break;
                }
            }

            if (widgetExists) {
                if (shouldEnable) {
                    manager.EnableWidget(widgetName);
                } else {
                    manager.DisableWidget(widgetName);
                }
            }
        }
    }
    file.close();
}

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

    // 動態添加已載入的 Widget 控制選項（帶子選單）
    int menuId = 1000;
    for (auto& plugin : g_loadedPlugins) {
        // 為每個 Widget 創建子選單
        HMENU hSubMenu = CreatePopupMenu();

        // 添加啟用/停用選項
        AppendMenuW(hSubMenu, MF_STRING, menuId, L"啟用/停用");
        bool isEnabled = manager.IsWidgetEnabled(plugin.name);
        CheckMenuItem(hSubMenu, menuId, MF_BYCOMMAND | (isEnabled ? MF_CHECKED : MF_UNCHECKED));
        menuId++;

        // 如果 Widget 支持自定義命令，添加相應選項
        if (plugin.executeCommandFunc) {
            AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);

            // FencesWidget 特定選項
            if (plugin.name == L"FencesWidget") {
                AppendMenuW(hSubMenu, MF_STRING, menuId, L"建立新柵欄");
                menuId++;
                AppendMenuW(hSubMenu, MF_STRING, menuId, L"清除所有記錄");
                menuId++;
            }
            // StickyNotesWidget 特定選項
            else if (plugin.name == L"StickyNotesWidget") {
                AppendMenuW(hSubMenu, MF_STRING, menuId, L"建立新便簽");
                menuId++;
                AppendMenuW(hSubMenu, MF_STRING, menuId, L"清除所有便簽");
                menuId++;
            }
        }

        // 添加帶子選單的 Widget 項目
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, plugin.name.c_str());
    }

    if (!g_loadedPlugins.empty()) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    // Auto-start option
    AppendMenuW(hMenu, MF_STRING, 2, L"開機自動啟動");
    bool autoStartEnabled = IsAutoStartEnabled();
    CheckMenuItem(hMenu, 2, MF_BYCOMMAND | (autoStartEnabled ? MF_CHECKED : MF_UNCHECKED));

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 100, L"退出");

    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMenu);

    // Handle menu commands
    if (cmd >= 1000) {
        // 計算命令對應的 Widget 和動作
        int cmdOffset = 0;
        bool handled = false;

        for (auto& plugin : g_loadedPlugins) {
            int widgetCmdCount = 1;  // 至少有一個 "啟用/停用" 選項

            // 計算此 Widget 有多少選項
            if (plugin.executeCommandFunc) {
                if (plugin.name == L"FencesWidget") {
                    widgetCmdCount += 2;  // 建立新柵欄 + 清除所有記錄
                } else if (plugin.name == L"StickyNotesWidget") {
                    widgetCmdCount += 2;  // 建立新便簽 + 清除所有便簽
                }
            }

            if (cmd >= 1000 + cmdOffset && cmd < 1000 + cmdOffset + widgetCmdCount) {
                int localCmd = cmd - (1000 + cmdOffset);

                if (localCmd == 0) {
                    // 啟用/停用
                    if (manager.IsWidgetEnabled(plugin.name)) {
                        manager.DisableWidget(plugin.name);
                    } else {
                        manager.EnableWidget(plugin.name);
                    }
                    SaveWidgetStates(manager);
                } else if (plugin.executeCommandFunc) {
                    // 自定義命令
                    if (plugin.name == L"FencesWidget") {
                        if (localCmd == 1) {
                            // 建立新柵欄
                            plugin.executeCommandFunc(plugin.widgetInstance.get(), WIDGET_CMD_CREATE_NEW);
                        } else if (localCmd == 2) {
                            // 清除所有記錄
                            plugin.executeCommandFunc(plugin.widgetInstance.get(), WIDGET_CMD_CLEAR_ALL_DATA);
                        }
                    } else if (plugin.name == L"StickyNotesWidget") {
                        if (localCmd == 1) {
                            // 建立新便簽
                            plugin.executeCommandFunc(plugin.widgetInstance.get(), WIDGET_CMD_CREATE_NEW);
                        } else if (localCmd == 2) {
                            // 清除所有便簽
                            plugin.executeCommandFunc(plugin.widgetInstance.get(), WIDGET_CMD_CLEAR_ALL_DATA);
                        }
                    }
                }

                handled = true;
                break;
            }

            cmdOffset += widgetCmdCount;
        }

        if (handled) {
            return;  // 命令已處理
        }
    }

    if (cmd == 2) {
        // Toggle Auto-start
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
    } else if (cmd == 100) {
        // Exit
        PostQuitMessage(0);
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

    // 掃描並載入 Widget DLL 插件
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    g_loadedPlugins = PluginLoader::ScanPlugins(exeDir.wstring());

    // 為每個插件創建 Widget 實例並註冊
    for (auto& plugin : g_loadedPlugins) {
        auto widgetInstance = PluginLoader::CreateWidgetInstance(plugin, &hInstance);
        if (widgetInstance) {
            manager.RegisterWidget(widgetInstance);
        }
    }

    // 載入上次保存的 Widget 狀態（如果沒有配置則全部啟用）
    LoadWidgetStates(manager);

    // 如果是第一次運行（沒有配置檔案），預設啟用所有 Widget
    std::wstring configPath = GetWidgetStateConfigPath();
    std::wifstream testFile(configPath);
    if (!testFile.is_open()) {
        // 首次運行，啟用所有 Widget
        for (auto& plugin : g_loadedPlugins) {
            manager.EnableWidget(plugin.name);
        }
        SaveWidgetStates(manager);
    }
    testFile.close();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    manager.Shutdown();

    // 卸載所有插件
    for (auto& plugin : g_loadedPlugins) {
        PluginLoader::UnloadPlugin(plugin);
    }

    RemoveTrayIcon();
    DestroyWindow(g_hControlWindow);

    // 釋放 Mutex
    if (hMutex) {
        CloseHandle(hMutex);
    }

    CoUninitialize();

    return (int)msg.wParam;
}
