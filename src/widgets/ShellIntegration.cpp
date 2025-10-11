#include "ShellIntegration.h"
#include <shlwapi.h>
#include <commoncontrols.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// ============================================================================
// DesktopEnumerator Implementation
// ============================================================================

DesktopEnumerator::DesktopEnumerator()
    : pDesktopFolder_(nullptr)
    , initialized_(false) {
}

DesktopEnumerator::~DesktopEnumerator() {
    if (pDesktopFolder_) {
        pDesktopFolder_->Release();
        pDesktopFolder_ = nullptr;
    }
}

bool DesktopEnumerator::Initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize COM (should already be initialized by main app)
    CoInitialize(nullptr);

    // Get desktop folder interface
    HRESULT hr = SHGetDesktopFolder(&pDesktopFolder_);
    if (FAILED(hr) || !pDesktopFolder_) {
        return false;
    }

    initialized_ = true;
    return true;
}

std::vector<DesktopItem> DesktopEnumerator::EnumerateItems() {
    std::vector<DesktopItem> items;

    if (!initialized_ || !pDesktopFolder_) {
        return items;
    }

    // Get desktop PIDL
    LPITEMIDLIST pidlDesktop = nullptr;
    HRESULT hr = SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOPDIRECTORY, &pidlDesktop);
    if (FAILED(hr)) {
        return items;
    }

    // Bind to desktop folder
    IShellFolder* pDesktopSubFolder = nullptr;
    hr = pDesktopFolder_->BindToObject(pidlDesktop, nullptr, IID_IShellFolder, (void**)&pDesktopSubFolder);

    IShellFolder* pFolderToEnum = pDesktopSubFolder ? pDesktopSubFolder : pDesktopFolder_;

    // Enumerate desktop items
    IEnumIDList* pEnum = nullptr;
    hr = pFolderToEnum->EnumObjects(
        nullptr,
        SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
        &pEnum
    );

    if (SUCCEEDED(hr) && pEnum) {
        LPITEMIDLIST pidl = nullptr;
        while (pEnum->Next(1, &pidl, nullptr) == S_OK) {
            DesktopItem item;
            item.hIcon = nullptr;
            item.isFolder = false;

            // Get display name
            STRRET strret;
            if (SUCCEEDED(pFolderToEnum->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret))) {
                LPWSTR pszName = nullptr;
                if (SUCCEEDED(StrRetToStrW(&strret, pidl, &pszName)) && pszName) {
                    item.displayName = pszName;
                    CoTaskMemFree(pszName);
                }
            }

            // Get full path
            if (SUCCEEDED(pFolderToEnum->GetDisplayNameOf(pidl, SHGDN_FORPARSING, &strret))) {
                LPWSTR pszPath = nullptr;
                if (SUCCEEDED(StrRetToStrW(&strret, pidl, &pszPath)) && pszPath) {
                    item.fullPath = pszPath;
                    CoTaskMemFree(pszPath);
                }
            }

            // Check if folder
            SFGAOF attrs = SFGAO_FOLDER;
            if (SUCCEEDED(pFolderToEnum->GetAttributesOf(1, (LPCITEMIDLIST*)&pidl, &attrs))) {
                item.isFolder = (attrs & SFGAO_FOLDER) != 0;
            }

            // Get icon using IExtractIcon
            IExtractIconW* pExtract = nullptr;
            hr = pFolderToEnum->GetUIObjectOf(nullptr, 1, (LPCITEMIDLIST*)&pidl,
                                              IID_IExtractIconW, nullptr, (void**)&pExtract);
            if (SUCCEEDED(hr) && pExtract) {
                wchar_t szIconFile[MAX_PATH];
                int iIndex = 0;
                UINT wFlags = 0;

                if (SUCCEEDED(pExtract->GetIconLocation(0, szIconFile, MAX_PATH, &iIndex, &wFlags))) {
                    HICON hIconLarge = nullptr;
                    HICON hIconSmall = nullptr;
                    pExtract->Extract(szIconFile, iIndex, &hIconLarge, &hIconSmall, MAKELONG(48, 32));
                    item.hIcon = hIconLarge; // Use large icon
                    if (hIconSmall) DestroyIcon(hIconSmall);
                }
                pExtract->Release();
            }

            // Fallback: use SHGetFileInfo if IExtractIcon failed
            if (!item.hIcon && !item.fullPath.empty()) {
                SHFILEINFOW sfi = {0};
                if (SHGetFileInfoW(item.fullPath.c_str(), 0, &sfi, sizeof(sfi),
                                   SHGFI_ICON | SHGFI_LARGEICON)) {
                    item.hIcon = sfi.hIcon;
                }
            }

            items.push_back(item);
            CoTaskMemFree(pidl);
        }
        pEnum->Release();
    }

    if (pDesktopSubFolder) {
        pDesktopSubFolder->Release();
    }

    CoTaskMemFree(pidlDesktop);
    return items;
}

std::wstring DesktopEnumerator::GetDesktopPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, path))) {
        return path;
    }
    return L"";
}

// ============================================================================
// ShellNotifyListener Implementation
// ============================================================================

ShellNotifyListener::ShellNotifyListener(HWND hwnd)
    : hwnd_(hwnd)
    , notifyID_(0)
    , desktopPidl_(nullptr)
    , registered_(false) {
}

ShellNotifyListener::~ShellNotifyListener() {
    Unregister();
}

bool ShellNotifyListener::Register() {
    if (registered_) {
        return true;
    }

    // Get desktop PIDL
    HRESULT hr = SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOPDIRECTORY, &desktopPidl_);
    if (FAILED(hr)) {
        return false;
    }

    // Register for shell change notifications
    SHChangeNotifyEntry entry;
    entry.pidl = desktopPidl_;
    entry.fRecursive = FALSE;

    notifyID_ = SHChangeNotifyRegister(
        hwnd_,
        SHCNRF_ShellLevel | SHCNRF_NewDelivery,
        SHCNE_CREATE | SHCNE_DELETE | SHCNE_RENAMEITEM | SHCNE_UPDATEITEM,
        WM_SHELLNOTIFY,
        1,
        &entry
    );

    registered_ = (notifyID_ != 0);
    return registered_;
}

void ShellNotifyListener::Unregister() {
    if (notifyID_) {
        SHChangeNotifyDeregister(notifyID_);
        notifyID_ = 0;
    }

    if (desktopPidl_) {
        CoTaskMemFree(desktopPidl_);
        desktopPidl_ = nullptr;
    }

    registered_ = false;
}
