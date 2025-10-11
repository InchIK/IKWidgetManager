#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

// Desktop item information from IShellFolder
struct DesktopItem {
    std::wstring displayName;     // Display name
    std::wstring fullPath;        // Full file path
    HICON hIcon;                  // Icon handle
    bool isFolder;                // Is directory
};

// Desktop enumerator using IShellFolder
class DesktopEnumerator {
public:
    DesktopEnumerator();
    ~DesktopEnumerator();

    // Initialize COM and get desktop folder
    bool Initialize();

    // Enumerate all desktop items
    std::vector<DesktopItem> EnumerateItems();

    // Get desktop folder path
    static std::wstring GetDesktopPath();

private:
    IShellFolder* pDesktopFolder_;
    bool initialized_;
};

// Shell notification listener
class ShellNotifyListener {
public:
    ShellNotifyListener(HWND hwnd);
    ~ShellNotifyListener();

    // Register for shell notifications
    bool Register();

    // Unregister shell notifications
    void Unregister();

    // Custom message for shell notifications
    static const UINT WM_SHELLNOTIFY = WM_USER + 100;

private:
    HWND hwnd_;
    ULONG notifyID_;
    LPITEMIDLIST desktopPidl_;
    bool registered_;
};
