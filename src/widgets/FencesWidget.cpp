#include "FencesWidget.h"
#include "ShellIntegration.h"
#include "DropTarget.h"
#include "core/WidgetExport.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commoncontrols.h>
#include <dwmapi.h>
#include <richedit.h>
#include <algorithm>
#include <map>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// Macros from windowsx.h
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

// Context menu IDs
#define IDM_RENAME_FENCE      1001
#define IDM_CHANGE_COLOR      1002
#define IDM_CREATE_FENCE      1003
#define IDM_DELETE_FENCE      1004
#define IDM_ICON_SIZE_32      1005
#define IDM_ICON_SIZE_48      1006
#define IDM_ICON_SIZE_64      1007
#define IDM_CHANGE_TRANSPARENCY 1008
#define IDM_REMOVE_ICON       1009
#define IDM_CHANGE_TITLE_COLOR 1010
#define IDM_AUTO_CATEGORIZE   1011

// Title bar height
const int TITLE_BAR_HEIGHT = 35;

// Icon spacing and padding
const int ICON_PADDING_LEFT = 15;
const int ICON_PADDING_RIGHT = 15;
const int ICON_PADDING_TOP = 15;
const int ICON_PADDING_BOTTOM = 15;

// Color presets for fence backgrounds
static const COLORREF COLOR_PRESETS[] = {
    RGB(240, 240, 240),  // Light gray
    RGB(230, 240, 255),  // Light blue
    RGB(240, 255, 240),  // Light green
    RGB(255, 245, 230),  // Light orange
    RGB(255, 240, 245),  // Light pink
    RGB(245, 245, 220),  // Beige
    RGB(230, 230, 250),  // Lavender
    RGB(240, 255, 255)   // Light cyan
};
const int COLOR_PRESETS_COUNT = sizeof(COLOR_PRESETS) / sizeof(COLOR_PRESETS[0]);

FencesWidget::FencesWidget()
    : running_(false)
    , shutdownCalled_(false)
    , hInstance_(GetModuleHandle(nullptr))
    , windowClassName_(L"DesktopFenceWidget")
    , classRegistered_(false)
    , desktopWindow_(nullptr)
    , desktopListView_(nullptr)
    , desktopShellView_(nullptr)
    , originalShellViewProc_(nullptr)
    , selectedIconIndex_(-1)
    , selectedFence_(nullptr)
    , shellNotifyListener_(nullptr)
    , desktopEnumerator_(nullptr)
    , fileManager_(nullptr) {
}

FencesWidget::~FencesWidget() {
    Shutdown();
}

bool FencesWidget::Initialize() {
    OutputDebugStringW(L"========================================\n");
    OutputDebugStringW(L"[FencesWidget] Initialize() START\n");
    OutputDebugStringW(L"========================================\n");

    if (classRegistered_) {
        OutputDebugStringW(L"[FencesWidget] Already registered, returning true\n");
        return true;
    }

    // =========================================================================
    // NEW in v3.1: Initialize OLE for IDropTarget support
    // =========================================================================
    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr) && hr != S_FALSE) {
        OutputDebugStringW(L"[FencesWidget] Failed to initialize OLE\n");
        return false;
    }
    OutputDebugStringW(L"[FencesWidget] OLE initialized for drag-drop support\n");

    // =========================================================================
    // NEW in v4.0: Initialize FileManager for safe file operations
    // =========================================================================
    fileManager_ = new FileManager();
    if (!fileManager_->Initialize()) {
        OutputDebugStringW(L"[FencesWidget] Failed to initialize FileManager\n");
        delete fileManager_;
        fileManager_ = nullptr;
        return false;
    }
    OutputDebugStringW(L"[FencesWidget] FileManager initialized successfully\n");

    desktopWindow_ = GetDesktopWindow();

    // Initialize desktop enumerator
    desktopEnumerator_ = new DesktopEnumerator();
    if (!desktopEnumerator_->Initialize()) {
        delete desktopEnumerator_;
        desktopEnumerator_ = nullptr;
        return false;
    }

    // Setup desktop ListView subclass for Custom Draw
    OutputDebugStringW(L"[FencesWidget] Attempting to setup desktop subclass...\n");
    if (!SetupDesktopSubclass()) {
        // Non-fatal: fall back to old method if subclass fails
        OutputDebugStringW(L"[FencesWidget] ERROR: Failed to setup desktop subclass - Custom Draw will NOT work!\n");
    } else {
        OutputDebugStringW(L"[FencesWidget] SUCCESS: Desktop subclass setup complete\n");
    }

    bool result = RegisterWindowClass();
    if (result) {
        OutputDebugStringW(L"[FencesWidget] Initialize() completed successfully\n");
    } else {
        OutputDebugStringW(L"[FencesWidget] Initialize() FAILED at RegisterWindowClass\n");
    }
    return result;
}

bool FencesWidget::Start() {
    if (running_) {
        return true;
    }

    // 如果 fences_ 為空，才載入配置（首次啟動或清空後）
    if (fences_.empty()) {
        wchar_t appData[MAX_PATH];
        bool configLoaded = false;
        if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
            std::wstring configPath = std::wstring(appData) + L"\\FencesWidget\\config.json";
            configLoaded = LoadConfiguration(configPath);
        }

        // 如果沒有配置檔，建立示範柵欄
        if (!configLoaded && fences_.empty()) {
            if (!CreateFence(100, 100, 300, 400, L"桌面柵欄 1")) {
                return false;
            }
        }
    }

    // 顯示所有柵欄窗口
    for (auto& fence : fences_) {
        if (fence.hwnd) {
            ShowWindow(fence.hwnd, SW_SHOW);
        }
    }

    // =========================================================================
    // NEW in v3.0: Rebuild managedIconPaths and trigger desktop redraw
    // =========================================================================
    managedIconPaths_.clear();
    for (auto& fence : fences_) {
        for (const auto& icon : fence.icons) {
            managedIconPaths_.insert(icon.filePath);
        }
    }

    // Trigger desktop ListView redraw to apply Custom Draw suppression
    if (desktopListView_) {
        InvalidateRect(desktopListView_, nullptr, TRUE);
        OutputDebugStringW(L"[FencesWidget] Desktop redraw triggered for Custom Draw\n");
    }

    running_ = true;
    return true;
}

void FencesWidget::RestoreAllDesktopIcons() {
    // =========================================================================
    // NEW in v3.0: Simply clear managedIconPaths and trigger desktop redraw
    // Icons never left Desktop folder, so they will automatically reappear
    // =========================================================================
    managedIconPaths_.clear();

    if (desktopListView_) {
        InvalidateRect(desktopListView_, nullptr, TRUE);
        OutputDebugStringW(L"[FencesWidget] All icons restored via Custom Draw\n");
    }
}

// 取得檔案副檔名
std::wstring FencesWidget::GetFileExtension(const std::wstring& filePath) {
    size_t dotPos = filePath.find_last_of(L'.');
    if (dotPos != std::wstring::npos && dotPos < filePath.length() - 1) {
        std::wstring ext = filePath.substr(dotPos + 1);
        // 轉換為小寫
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext;
    }
    return L"";
}

// 根據檔案類型取得分類名稱
std::wstring FencesWidget::GetFileCategory(const std::wstring& filePath) {
    // 檢查是否為資料夾
    DWORD attrs = GetFileAttributesW(filePath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"資料夾";
    }

    std::wstring ext = GetFileExtension(filePath);

    // 文件類
    if (ext == L"doc" || ext == L"docx" || ext == L"pdf" || ext == L"txt" ||
        ext == L"xls" || ext == L"xlsx" || ext == L"ppt" || ext == L"pptx" ||
        ext == L"odt" || ext == L"ods" || ext == L"odp" || ext == L"rtf") {
        return L"文件";
    }

    // 圖片類
    if (ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"gif" ||
        ext == L"bmp" || ext == L"ico" || ext == L"svg" || ext == L"webp" ||
        ext == L"tiff" || ext == L"tif") {
        return L"圖片";
    }

    // 影片類
    if (ext == L"mp4" || ext == L"avi" || ext == L"mkv" || ext == L"mov" ||
        ext == L"wmv" || ext == L"flv" || ext == L"webm" || ext == L"m4v") {
        return L"影片";
    }

    // 音樂類
    if (ext == L"mp3" || ext == L"wav" || ext == L"flac" || ext == L"aac" ||
        ext == L"wma" || ext == L"m4a" || ext == L"ogg" || ext == L"opus") {
        return L"音樂";
    }

    // 壓縮檔
    if (ext == L"zip" || ext == L"rar" || ext == L"7z" || ext == L"tar" ||
        ext == L"gz" || ext == L"bz2" || ext == L"xz" || ext == L"iso") {
        return L"壓縮檔";
    }

    // 程式/應用
    if (ext == L"exe" || ext == L"msi" || ext == L"lnk" || ext == L"bat" ||
        ext == L"cmd" || ext == L"com") {
        return L"應用程式";
    }

    // 程式碼
    if (ext == L"cpp" || ext == L"c" || ext == L"h" || ext == L"hpp" ||
        ext == L"py" || ext == L"js" || ext == L"java" || ext == L"cs" ||
        ext == L"html" || ext == L"css" || ext == L"php" || ext == L"json" ||
        ext == L"xml" || ext == L"sql") {
        return L"程式碼";
    }

    return L"其他";
}

// 自動分類桌面圖示
void FencesWidget::AutoCategorizeDesktopIcons() {
    // 取得桌面資料夾路徑
    wchar_t desktopPath[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, desktopPath) != S_OK) {
        MessageBoxW(nullptr, L"無法取得桌面路徑", L"錯誤", MB_OK | MB_ICONERROR);
        return;
    }

    // 掃描桌面上的所有檔案和資料夾
    std::wstring searchPath = std::wstring(desktopPath) + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr, L"無法掃描桌面檔案", L"錯誤", MB_OK | MB_ICONERROR);
        return;
    }

    // 建立分類對應表
    std::map<std::wstring, std::vector<std::wstring>> categoryMap;

    do {
        // 跳過 . 和 ..
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        // 建立完整路徑
        std::wstring fullPath = std::wstring(desktopPath) + L"\\" + findData.cFileName;

        // 檢查是否已經在某個柵欄中
        bool alreadyInFence = false;
        for (const auto& fence : fences_) {
            for (const auto& icon : fence.icons) {
                if (icon.filePath == fullPath) {
                    alreadyInFence = true;
                    break;
                }
            }
            if (alreadyInFence) break;
        }

        // 如果不在柵欄中，加入分類
        if (!alreadyInFence) {
            std::wstring category = GetFileCategory(fullPath);
            categoryMap[category].push_back(fullPath);
        }

    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // 為每個分類建立柵欄（如果該分類有檔案）
    int xOffset = 100;
    int yOffset = 100;

    for (const auto& pair : categoryMap) {
        if (pair.second.empty()) continue;

        const std::wstring& category = pair.first;
        const std::vector<std::wstring>& files = pair.second;

        // 檢查是否已有同名柵欄
        Fence* existingFence = nullptr;
        for (auto& fence : fences_) {
            if (fence.title == category) {
                existingFence = &fence;
                break;
            }
        }

        Fence* targetFence = existingFence;

        // 如果沒有同名柵欄，建立新的
        if (!existingFence) {
            if (CreateFence(xOffset, yOffset, 300, 400, category)) {
                targetFence = &fences_.back();
                xOffset += 50;
                yOffset += 50;
            }
        }

        // 將檔案加入到柵欄
        if (targetFence) {
            for (const auto& filePath : files) {
                AddIconToFence(targetFence, filePath);
                // AddIconToFence now handles managedIconPaths and desktop redraw
            }

            ArrangeIcons(targetFence);
            InvalidateRect(targetFence->hwnd, nullptr, TRUE);
        }
    }

    // 儲存配置
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring configPath = std::wstring(appData) + L"\\FencesWidget\\config.json";
        std::wstring dirPath = std::wstring(appData) + L"\\FencesWidget";
        CreateDirectoryW(dirPath.c_str(), nullptr);
        SaveConfiguration(configPath);
    }

    MessageBoxW(nullptr, L"桌面圖示自動分類完成！", L"完成", MB_OK | MB_ICONINFORMATION);
}

void FencesWidget::ClearAllData() {
    // 確認是否要清除所有資料
    int result = MessageBoxW(nullptr,
        L"確定要清除所有柵欄和配置記錄嗎？\n\n此操作將：\n1. 刪除所有柵欄\n2. 恢復所有圖示到桌面\n3. 清除所有配置文件\n\n此操作無法復原！",
        L"確認清除",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

    if (result != IDYES) {
        return;
    }

    // 恢復所有桌面圖示
    RestoreAllDesktopIcons();

    // 刪除所有柵欄窗口
    for (auto& fence : fences_) {
        if (fence.hwnd) {
            DestroyWindow(fence.hwnd);
        }
    }

    // 清空柵欄列表
    fences_.clear();

    // 刪除配置文件
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring configDir = std::wstring(appData) + L"\\FencesWidget";
        std::wstring configPath = configDir + L"\\config.json";

        // 刪除 config.json
        DeleteFileW(configPath.c_str());

        // 嘗試刪除目錄（如果為空）
        RemoveDirectoryW(configDir.c_str());
    }

    MessageBoxW(nullptr, L"已清除所有柵欄和配置記錄！", L"完成", MB_OK | MB_ICONINFORMATION);
}

void FencesWidget::Stop() {
    if (!running_) {
        return;
    }

    // 關閉前恢復所有桌面圖示
    RestoreAllDesktopIcons();

    // Hide all fence windows
    for (auto& fence : fences_) {
        if (fence.hwnd) {
            ShowWindow(fence.hwnd, SW_HIDE);
        }
    }

    running_ = false;
}

bool FencesWidget::SaveConfiguration(const std::wstring& filePath) {
    // 打開檔案寫入
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::wstring config = L"{\n  \"fences\": [\n";

    for (size_t i = 0; i < fences_.size(); ++i) {
        const auto& fence = fences_[i];

        config += L"    {\n";
        config += L"      \"title\": \"" + fence.title + L"\",\n";
        config += L"      \"x\": " + std::to_wstring(fence.rect.left) + L",\n";
        config += L"      \"y\": " + std::to_wstring(fence.rect.top) + L",\n";
        config += L"      \"width\": " + std::to_wstring(fence.rect.right - fence.rect.left) + L",\n";
        config += L"      \"height\": " + std::to_wstring(fence.rect.bottom - fence.rect.top) + L",\n";
        config += L"      \"isCollapsed\": " + std::wstring(fence.isCollapsed ? L"true" : L"false") + L",\n";
        config += L"      \"isPinned\": " + std::wstring(fence.isPinned ? L"true" : L"false") + L",\n";
        config += L"      \"expandedHeight\": " + std::to_wstring(fence.expandedHeight) + L",\n";
        config += L"      \"iconSize\": " + std::to_wstring(fence.iconSize) + L",\n";
        config += L"      \"alpha\": " + std::to_wstring(fence.alpha) + L",\n";
        config += L"      \"backgroundColor\": " + std::to_wstring(fence.backgroundColor) + L",\n";
        config += L"      \"borderColor\": " + std::to_wstring(fence.borderColor) + L",\n";
        config += L"      \"titleColor\": " + std::to_wstring(fence.titleColor) + L",\n";
        config += L"      \"icons\": [\n";

        for (size_t j = 0; j < fence.icons.size(); ++j) {
            const auto& icon = fence.icons[j];
            config += L"        {\n";
            config += L"          \"filePath\": \"" + icon.filePath + L"\"\n";
            config += L"        }";
            if (j < fence.icons.size() - 1) config += L",";
            config += L"\n";
        }

        config += L"      ]\n";
        config += L"    }";
        if (i < fences_.size() - 1) config += L",";
        config += L"\n";
    }

    config += L"  ]\n}\n";

    // 轉換為 UTF-8 並寫入
    int size = WideCharToMultiByte(CP_UTF8, 0, config.c_str(), -1, nullptr, 0, nullptr, nullptr);
    char* utf8 = new char[size];
    WideCharToMultiByte(CP_UTF8, 0, config.c_str(), -1, utf8, size, nullptr, nullptr);

    DWORD written;
    WriteFile(hFile, utf8, size - 1, &written, nullptr);

    delete[] utf8;
    CloseHandle(hFile);
    return true;
}

bool FencesWidget::LoadConfiguration(const std::wstring& filePath) {
    // 讀取檔案
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    char* buffer = new char[fileSize + 1];
    DWORD bytesRead;
    ReadFile(hFile, buffer, fileSize, &bytesRead, nullptr);
    buffer[bytesRead] = '\0';
    CloseHandle(hFile);

    // 轉換為 wide string
    int wsize = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, nullptr, 0);
    if (wsize == 0) {
        delete[] buffer;
        return false;
    }

    wchar_t* wbuffer = new wchar_t[wsize];
    MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wsize);
    std::wstring json(wbuffer);

    delete[] buffer;
    delete[] wbuffer;

    // 檢查是否有柵欄數據
    if (json.find(L"\"fences\":") == std::wstring::npos) {
        return false;
    }

    // 簡單的 JSON 解析（手動解析，避免外部依賴）
    size_t pos = 0;
    int fenceCount = 0;
    while ((pos = json.find(L"\"title\":", pos)) != std::wstring::npos) {
        // 解析柵欄標題
        size_t titleStart = json.find(L'"', pos + 8) + 1;
        size_t titleEnd = json.find(L'"', titleStart);
        std::wstring title = json.substr(titleStart, titleEnd - titleStart);

        // 解析位置和大小
        size_t xPos = json.find(L"\"x\":", titleEnd);
        size_t yPos = json.find(L"\"y\":", xPos);
        size_t wPos = json.find(L"\"width\":", yPos);
        size_t hPos = json.find(L"\"height\":", wPos);

        int x = std::stoi(json.substr(json.find(L':', xPos) + 1, 10));
        int y = std::stoi(json.substr(json.find(L':', yPos) + 1, 10));
        int width = std::stoi(json.substr(json.find(L':', wPos) + 1, 10));
        int height = std::stoi(json.substr(json.find(L':', hPos) + 1, 10));

        // 解析收合狀態、固定狀態、展開高度、圖示大小、透明度和顏色
        size_t collapsedPos = json.find(L"\"isCollapsed\":", hPos);
        size_t pinnedPos = json.find(L"\"isPinned\":", hPos);
        size_t expandedHeightPos = json.find(L"\"expandedHeight\":", hPos);
        size_t iconSizePos = json.find(L"\"iconSize\":", hPos);
        size_t alphaPos = json.find(L"\"alpha\":", hPos);
        size_t bgColorPos = json.find(L"\"backgroundColor\":", hPos);
        size_t borderColorPos = json.find(L"\"borderColor\":", hPos);
        size_t titleColorPos = json.find(L"\"titleColor\":", hPos);

        bool isCollapsed = false;
        bool isPinned = false;
        int expandedHeight = height;
        int iconSize = 64; // 預設值
        int alpha = 230; // 預設透明度
        COLORREF backgroundColor = RGB(240, 240, 240); // 預設背景色
        COLORREF borderColor = RGB(100, 100, 100); // 預設邊框色
        COLORREF titleColor = RGB(50, 50, 50); // 預設標題色

        if (collapsedPos != std::wstring::npos) {
            size_t valueStart = json.find(L':', collapsedPos) + 1;
            std::wstring value = json.substr(valueStart, 10);
            isCollapsed = (value.find(L"true") != std::wstring::npos);
        }

        if (pinnedPos != std::wstring::npos) {
            size_t valueStart = json.find(L':', pinnedPos) + 1;
            std::wstring value = json.substr(valueStart, 10);
            isPinned = (value.find(L"true") != std::wstring::npos);
        }

        if (expandedHeightPos != std::wstring::npos) {
            expandedHeight = std::stoi(json.substr(json.find(L':', expandedHeightPos) + 1, 10));
        }

        if (iconSizePos != std::wstring::npos) {
            iconSize = std::stoi(json.substr(json.find(L':', iconSizePos) + 1, 10));
        }

        if (alphaPos != std::wstring::npos) {
            alpha = std::stoi(json.substr(json.find(L':', alphaPos) + 1, 10));
        }

        if (bgColorPos != std::wstring::npos) {
            backgroundColor = (COLORREF)std::stoul(json.substr(json.find(L':', bgColorPos) + 1, 15));
        }

        if (borderColorPos != std::wstring::npos) {
            borderColor = (COLORREF)std::stoul(json.substr(json.find(L':', borderColorPos) + 1, 15));
        }

        if (titleColorPos != std::wstring::npos) {
            titleColor = (COLORREF)std::stoul(json.substr(json.find(L':', titleColorPos) + 1, 15));
        }

        // 創建柵欄
        if (CreateFence(x, y, width, height, title)) {
            Fence* fence = &fences_.back();

            // 設定收合狀態、固定狀態、展開高度、圖示大小、透明度和顏色
            fence->isCollapsed = isCollapsed;
            fence->isPinned = isPinned;
            fence->expandedHeight = expandedHeight;
            fence->iconSize = iconSize;
            fence->alpha = alpha;
            fence->backgroundColor = backgroundColor;
            fence->borderColor = borderColor;
            fence->titleColor = titleColor;

            // 更新窗口透明度
            SetLayeredWindowAttributes(fence->hwnd, 0, (BYTE)fence->alpha, LWA_ALPHA);

            // 重繪以套用新顏色
            InvalidateRect(fence->hwnd, nullptr, TRUE);

            // 解析圖示
            size_t iconsStart = json.find(L"\"icons\":", hPos);
            size_t iconsEnd = json.find(L"]", iconsStart);
            size_t iconPos = iconsStart;

            // 收集所有圖示路徑以便批次隱藏
            std::vector<std::wstring> iconPathsToHide;

            while ((iconPos = json.find(L"\"filePath\":", iconPos)) != std::wstring::npos && iconPos < iconsEnd) {
                size_t pathStart = json.find(L'"', iconPos + 11) + 1;
                size_t pathEnd = json.find(L'"', pathStart);
                std::wstring iconPath = json.substr(pathStart, pathEnd - pathStart);

                // 添加圖示到柵欄
                DesktopIcon newIcon;
                newIcon.filePath = iconPath;

                // 只載入當前需要的大小（延遲載入優化）
                newIcon.hIcon32 = nullptr;
                newIcon.hIcon48 = nullptr;
                newIcon.hIcon64 = nullptr;
                newIcon.hIcon = nullptr;
                newIcon.cachedIconSize = 0;

                // 立即載入當前柵欄使用的圖示大小
                if (fence->iconSize == 32) {
                    newIcon.hIcon32 = GetFileIcon(iconPath, 32);
                } else if (fence->iconSize == 48) {
                    newIcon.hIcon48 = GetFileIcon(iconPath, 48);
                } else if (fence->iconSize == 64) {
                    newIcon.hIcon64 = GetFileIcon(iconPath, 64);
                }

                newIcon.selected = false;
                newIcon.position = { 0, 0 };

                // 提取顯示名稱
                size_t lastSlash = iconPath.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) {
                    newIcon.displayName = iconPath.substr(lastSlash + 1);
                } else {
                    newIcon.displayName = iconPath;
                }

                size_t lastDot = newIcon.displayName.find_last_of(L'.');
                if (lastDot != std::wstring::npos && lastDot > 0) {
                    newIcon.displayName = newIcon.displayName.substr(0, lastDot);
                }

                fence->icons.push_back(newIcon);

                iconPos = pathEnd;
            }

            // =====================================================================
            // NEW in v3.0: No longer hide icons, managedIconPaths will be built
            // in Start() method after all fences are loaded
            // =====================================================================
            // Icons will be suppressed via Custom Draw when Start() is called

            // 排列圖示
            ArrangeIcons(fence);
            InvalidateRect(fence->hwnd, nullptr, TRUE);
            fenceCount++;
        }

        pos = titleEnd;
    }

    return fenceCount > 0;
}

void FencesWidget::Shutdown() {
    // 防止重複調用
    if (shutdownCalled_) {
        return;
    }
    shutdownCalled_ = true;

    // 保存配置（在清空之前）
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring configPath = std::wstring(appData) + L"\\FencesWidget\\config.json";

        // 創建目錄
        std::wstring dirPath = std::wstring(appData) + L"\\FencesWidget";
        CreateDirectoryW(dirPath.c_str(), nullptr);

        SaveConfiguration(configPath);
    }

    // NEW in v3.0: Remove desktop subclass
    RemoveDesktopSubclass();

    // NEW in v3.0: Clean up shell notification listener
    if (shellNotifyListener_) {
        delete shellNotifyListener_;
        shellNotifyListener_ = nullptr;
    }

    // NEW in v3.0: Clean up desktop enumerator
    if (desktopEnumerator_) {
        delete desktopEnumerator_;
        desktopEnumerator_ = nullptr;
    }

    // NEW in v4.0: Move all managed files back to desktop before shutdown
    if (fileManager_) {
        for (auto& fence : fences_) {
            for (const auto& icon : fence.icons) {
                if (fileManager_->IsManagedFile(icon.filePath)) {
                    MoveResult result = fileManager_->MoveBackToDesktop(icon.filePath);
                    if (result.success) {
                        wchar_t msg[512];
                        swprintf_s(msg, L"[FencesWidget] Shutdown: File restored to desktop: %s\n", result.newPath.c_str());
                        OutputDebugStringW(msg);
                    }
                }
            }
        }
    }

    // NEW in v4.0: Clean up file manager
    if (fileManager_) {
        fileManager_->Shutdown();
        delete fileManager_;
        fileManager_ = nullptr;
        OutputDebugStringW(L"[FencesWidget] FileManager shutdown complete\n");
    }

    // NEW in v3.0: Clear managed icon paths and force desktop refresh
    managedIconPaths_.clear();
    if (desktopListView_) {
        InvalidateRect(desktopListView_, nullptr, TRUE);
    }

    // Clean up all fence windows and icons
    for (auto& fence : fences_) {
        // Clean up all icon handles
        for (auto& icon : fence.icons) {
            if (icon.hIcon32) {
                DestroyIcon(icon.hIcon32);
                icon.hIcon32 = nullptr;
            }
            if (icon.hIcon48) {
                DestroyIcon(icon.hIcon48);
                icon.hIcon48 = nullptr;
            }
            if (icon.hIcon64) {
                DestroyIcon(icon.hIcon64);
                icon.hIcon64 = nullptr;
            }
            if (icon.hIcon) {
                DestroyIcon(icon.hIcon);
                icon.hIcon = nullptr;
            }
        }

        if (fence.hwnd) {
            DestroyWindow(fence.hwnd);
            fence.hwnd = nullptr;
        }
    }

    fences_.clear();
    UnregisterWindowClass();

    // =========================================================================
    // NEW in v3.1: Uninitialize OLE
    // =========================================================================
    OleUninitialize();
    OutputDebugStringW(L"[FencesWidget] OLE uninitialized\n");

    OutputDebugStringW(L"[FencesWidget] Shutdown completed\n");
}

std::wstring FencesWidget::GetName() const {
    return L"FencesWidget";
}

std::wstring FencesWidget::GetDescription() const {
    return L"Desktop Fences - Stardock Fences-like Desktop Icon Organizer with Drag & Drop Support";
}

bool FencesWidget::IsRunning() const {
    return running_;
}

std::wstring FencesWidget::GetWidgetVersion() const {
    return L"2.0.0";
}

bool FencesWidget::CreateFence(int x, int y, int width, int height, const std::wstring& title) {
    if (!classRegistered_) {
        return false;
    }

    // Create layered window with drag-drop support
    // 使用 WS_EX_TOOLWINDOW 隱藏工作列圖示
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_ACCEPTFILES,
        windowClassName_,
        title.c_str(),
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hwnd) {
        return false;
    }

    // Set transparency
    SetLayeredWindowAttributes(hwnd, 0, 220, LWA_ALPHA);

    // 使用 SetWindowLong 設置特殊的擴展樣式，讓窗口不被「顯示桌面」影響
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_NOACTIVATE);

    // 設置窗口為系統窗口，不參與最小化
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    Fence fence;
    fence.hwnd = hwnd;
    fence.rect = { x, y, x + width, y + height };
    fence.title = title;
    fence.backgroundColor = RGB(240, 240, 240);
    fence.borderColor = RGB(100, 100, 100);
    fence.titleColor = RGB(50, 50, 50);
    fence.borderWidth = 2;
    fence.alpha = 220;
    fence.isResizing = false;
    fence.isDragging = false;
    fence.dragOffset = { 0, 0 };
    fence.iconSpacing = 10;
    fence.iconSize = 64;
    fence.isDraggingIcon = false;
    fence.draggingIconIndex = -1;
    fence.iconDragStart = { 0, 0 };
    fence.isCollapsed = false;
    fence.isPinned = false;
    fence.expandedHeight = height;
    fence.scrollOffset = 0;
    fence.contentHeight = 0;
    fence.isDraggingScrollbar = false;
    fence.scrollbarDragStartY = 0;
    fence.scrollOffsetAtDragStart = 0;
    fence.pDropTarget = nullptr;

    fences_.push_back(fence);

    // =========================================================================
    // NEW in v3.1: Register IDropTarget for proper drag-and-drop support
    // This replaces DragAcceptFiles which only supports COPY operations
    // =========================================================================
    Fence* pFence = &fences_.back();
    pFence->pDropTarget = new FenceDropTarget(this, pFence);
    HRESULT hr = RegisterDragDrop(hwnd, pFence->pDropTarget);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[FencesWidget] Failed to register IDropTarget\n");
        pFence->pDropTarget->Release();
        pFence->pDropTarget = nullptr;
    } else {
        OutputDebugStringW(L"[FencesWidget] Successfully registered IDropTarget\n");
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return true;
}

bool FencesWidget::RemoveFence(size_t index) {
    if (index >= fences_.size()) {
        return false;
    }

    // =========================================================================
    // NEW in v3.1: Revoke IDropTarget registration
    // =========================================================================
    if (fences_[index].pDropTarget) {
        RevokeDragDrop(fences_[index].hwnd);
        fences_[index].pDropTarget->Release();
        fences_[index].pDropTarget = nullptr;
    }

    // =========================================================================
    // NEW in v4.0: Move files back to desktop when removing fence
    // =========================================================================
    for (const auto& icon : fences_[index].icons) {
        managedIconPaths_.erase(icon.filePath);

        // 如果是管理的文件，移回桌面
        if (fileManager_ && fileManager_->IsManagedFile(icon.filePath)) {
            MoveResult result = fileManager_->MoveBackToDesktop(icon.filePath);
            if (result.success) {
                wchar_t msg[512];
                swprintf_s(msg, L"[FencesWidget] File restored to desktop: %s\n", result.newPath.c_str());
                OutputDebugStringW(msg);
            } else {
                wchar_t msg[512];
                swprintf_s(msg, L"[FencesWidget] Failed to restore file: %s (Error: %s)\n",
                          icon.filePath.c_str(), result.errorMessage.c_str());
                OutputDebugStringW(msg);
            }
        }
    }

    // Trigger desktop redraw to show icons again
    if (desktopListView_) {
        InvalidateRect(desktopListView_, nullptr, TRUE);
    }

    // Clean up icon handles
    for (auto& icon : fences_[index].icons) {
        if (icon.hIcon) {
            DestroyIcon(icon.hIcon);
        }
        if (icon.hIcon32) {
            DestroyIcon(icon.hIcon32);
        }
        if (icon.hIcon48) {
            DestroyIcon(icon.hIcon48);
        }
        if (icon.hIcon64) {
            DestroyIcon(icon.hIcon64);
        }
    }

    if (fences_[index].hwnd) {
        DestroyWindow(fences_[index].hwnd);
    }

    fences_.erase(fences_.begin() + index);

    // 保存配置以確保刪除操作同步到 JSON 文件
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring configPath = std::wstring(appData) + L"\\FencesWidget\\config.json";
        SaveConfiguration(configPath);
    }

    return true;
}

size_t FencesWidget::GetFenceCount() const {
    return fences_.size();
}

bool FencesWidget::UpdateFenceTitle(size_t index, const std::wstring& newTitle) {
    if (index >= fences_.size()) {
        return false;
    }

    fences_[index].title = newTitle;
    if (fences_[index].hwnd) {
        SetWindowTextW(fences_[index].hwnd, newTitle.c_str());
        InvalidateRect(fences_[index].hwnd, nullptr, TRUE);
    }
    return true;
}

bool FencesWidget::UpdateFenceStyle(size_t index, COLORREF bgColor, COLORREF borderColor, int alpha) {
    if (index >= fences_.size()) {
        return false;
    }

    fences_[index].backgroundColor = bgColor;
    fences_[index].borderColor = borderColor;
    fences_[index].alpha = alpha;

    if (fences_[index].hwnd) {
        SetLayeredWindowAttributes(fences_[index].hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
        InvalidateRect(fences_[index].hwnd, nullptr, TRUE);
    }
    return true;
}

bool FencesWidget::RegisterWindowClass() {
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(FencesWidget*);
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

void FencesWidget::UnregisterWindowClass() {
    if (classRegistered_) {
        UnregisterClassW(windowClassName_, hInstance_);
        classRegistered_ = false;
    }
}

LRESULT CALLBACK FencesWidget::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FencesWidget* widget = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        widget = static_cast<FencesWidget*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, 0, reinterpret_cast<LONG_PTR>(widget));
    } else {
        widget = reinterpret_cast<FencesWidget*>(GetWindowLongPtr(hwnd, 0));
    }

    if (widget) {
        return widget->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT FencesWidget::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Fence* fence = FindFence(hwnd);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintFence(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (fence) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnLButtonDown(fence, x, y);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (fence) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseMove(fence, x, y);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (fence) {
            OnLButtonUp(fence);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (fence) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int iconIndex = FindIconAtPosition(fence, x, y);
            if (iconIndex >= 0 && iconIndex < (int)fence->icons.size()) {
                // 雙擊圖示，打開檔案
                ShellExecuteW(nullptr, L"open", fence->icons[iconIndex].filePath.c_str(),
                             nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (fence) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnRButtonDown(fence, x, y);
        }
        return 0;
    }

    // =========================================================================
    // NOTE: WM_DROPFILES removed in v3.1
    // Now using IDropTarget interface for proper MOVE/COPY semantics
    // See DropTarget.h/cpp for implementation
    // =========================================================================

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT && fence) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            // 檢查是否在標題列圖示區域（收合和釘住按鈕）
            if (pt.y >= 0 && pt.y < TITLE_BAR_HEIGHT) {
                const int iconSize = 20;
                const int iconMargin = 5;
                int rightX = clientRect.right - iconMargin;

                // 檢查釘住圖示區域
                RECT pinRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                                 rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };
                if (pt.x >= pinRect.left && pt.x <= pinRect.right) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                rightX -= (iconSize + iconMargin);

                // 檢查收合圖示區域
                RECT collapseRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                                      rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };
                if (pt.x >= collapseRect.left && pt.x <= collapseRect.right) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }
            }

            if (IsInResizeArea(clientRect, pt.x, pt.y)) {
                SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                return TRUE;
            } else if (IsInTitleArea(clientRect, pt.x, pt.y)) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                return TRUE;
            } else {
                // 其他區域（圖示區域）顯示箭頭
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
        }
        break;
    }

    case WM_COMMAND: {
        if (fence) {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDM_RENAME_FENCE: {
                // 使用輸入框方式處理重新命名
                wchar_t newTitle[256] = { 0 };

                // 註冊自訂對話框窗口類別
                static bool classRegistered = false;
                const wchar_t* dialogClassName = L"FencesCustomDialog";

                if (!classRegistered) {
                    WNDCLASSEXW wcex = { 0 };
                    wcex.cbSize = sizeof(WNDCLASSEX);
                    wcex.lpfnWndProc = DefWindowProcW;
                    wcex.hInstance = hInstance_;
                    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
                    wcex.hbrBackground = CreateSolidBrush(RGB(240, 240, 240));
                    wcex.lpszClassName = dialogClassName;
                    RegisterClassExW(&wcex);
                    classRegistered = true;
                }

                // 建立對話框窗口（美化 - 無標題列）
                HWND hDialog = CreateWindowExW(
                    WS_EX_TOPMOST | WS_EX_LAYERED,
                    dialogClassName,
                    L"",
                    WS_POPUP | WS_VISIBLE,
                    0, 0, 360, 160,
                    hwnd,
                    nullptr,
                    hInstance_,
                    nullptr
                );

                if (hDialog) {
                    // 設定圓角和陰影效果
                    SetLayeredWindowAttributes(hDialog, 0, 250, LWA_ALPHA);

                    // 設定圓角窗口（Windows 11 風格）
                    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
                    DwmSetWindowAttribute(hDialog, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

                    // 對話框置中
                    RECT rcDialog, rcOwner;
                    GetWindowRect(hDialog, &rcDialog);
                    GetWindowRect(hwnd, &rcOwner);
                    int dialogWidth = rcDialog.right - rcDialog.left;
                    int dialogHeight = rcDialog.bottom - rcDialog.top;
                    int x = rcOwner.left + (rcOwner.right - rcOwner.left - dialogWidth) / 2;
                    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - dialogHeight) / 2;
                    SetWindowPos(hDialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

                    // 不使用子控制項，改為在對話框 WM_PAINT 繪製標題
                    HWND hTitleBar = nullptr;

                    HFONT hTitleFont = CreateFontW(
                        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");

                    // 建立標籤 - 使用 SS_OWNERDRAW 自訂繪製
                    HWND hLabel = CreateWindowExW(0, L"STATIC", L"新名稱：",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        20, 50, 320, 25,
                        hDialog, nullptr, hInstance_, nullptr);

                    HFONT hLabelFont = CreateFontW(
                        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(hLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);

                    // 強制讓標籤使用父窗口背景
                    SetWindowLongPtrW(hLabel, GWL_EXSTYLE, GetWindowLongPtrW(hLabel, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

                    // 載入 RichEdit 控制項 (支援 EM_SETBKGNDCOLOR)
                    LoadLibraryW(L"Msftedit.dll");

                    // 建立編輯框（使用 RichEdit 以支援背景顏色設定）
                    HWND hEdit = CreateWindowExW(
                        0, L"RICHEDIT50W", fence->title.c_str(),
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
                        20, 78, 320, 32,
                        hDialog, (HMENU)100, hInstance_, nullptr);

                    HFONT hEditFont = CreateFontW(
                        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hEditFont, TRUE);

                    // 設定 RichEdit 背景為淺灰色（與對話框背景統一）
                    SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(240, 240, 240));

                    // 建立確定按鈕（圓角、顏色）
                    HWND hBtnOK = CreateWindowExW(0, L"BUTTON", L"確定",
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                        190, 115, 70, 30,
                        hDialog, (HMENU)IDOK, hInstance_, nullptr);

                    // 建立取消按鈕
                    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                        270, 115, 70, 30,
                        hDialog, (HMENU)IDCANCEL, hInstance_, nullptr);

                    HFONT hBtnFont = CreateFontW(
                        18, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(hBtnOK, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
                    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hBtnFont, TRUE);

                    // 設定焦點並選取所有文字
                    SetFocus(hEdit);
                    SendMessageW(hEdit, EM_SETSEL, 0, -1);

                    // 子類化視窗以處理按鈕點擊和拖拉
                    struct DialogData {
                        bool* pRunning;
                        bool* pResult;
                        HWND hEdit;
                        HWND hLabel;
                        HWND hBtnOK;
                        HWND hBtnCancel;
                        HWND hTitleBar;
                        wchar_t* pTitle;
                        WNDPROC oldProc;
                        HFONT hBtnFont;
                        bool isDragging;
                        POINT dragOffset;
                        COLORREF fenceColor;
                        HBRUSH hBrushBkgnd;
                    };

                    bool dialogResult = false;
                    bool dialogRunning = true;

                    DialogData data;
                    data.pRunning = &dialogRunning;
                    data.pResult = &dialogResult;
                    data.hEdit = hEdit;
                    data.hLabel = hLabel;
                    data.hBtnOK = hBtnOK;
                    data.hBtnCancel = hBtnCancel;
                    data.hTitleBar = hTitleBar;
                    data.pTitle = newTitle;
                    data.hBtnFont = hBtnFont;
                    data.isDragging = false;
                    data.dragOffset = { 0, 0 };
                    data.fenceColor = fence->borderColor;
                    data.hBrushBkgnd = CreateSolidBrush(RGB(240, 240, 240));

                    // 保存原始窗口過程
                    data.oldProc = (WNDPROC)GetWindowLongPtrW(hDialog, GWLP_WNDPROC);

                    // 設置用戶數據
                    SetWindowLongPtrW(hDialog, GWLP_USERDATA, (LONG_PTR)&data);

                    // 子類化窗口
                    auto newProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
                        DialogData* pData = (DialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

                        if (pData) {
                            if (msg == WM_NCHITTEST) {
                                // 讓標題列區域可拖拉
                                LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
                                if (hit == HTCLIENT) {
                                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                                    ScreenToClient(hwnd, &pt);
                                    if (pt.y >= 0 && pt.y < 38) {
                                        return HTCAPTION;  // 讓系統處理拖拉
                                    }
                                }
                                return hit;
                            } else if (msg == WM_ERASEBKGND) {
                                // 自訂背景繪製
                                HDC hdc = (HDC)wParam;
                                RECT rect;
                                GetClientRect(hwnd, &rect);

                                // 繪製淺灰色背景（與控制項背景統一）
                                HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
                                FillRect(hdc, &rect, bgBrush);
                                DeleteObject(bgBrush);

                                // 繪製標題列（使用柵欄顏色）
                                RECT titleRect = { 0, 0, rect.right, 38 };
                                HBRUSH titleBrush = CreateSolidBrush(pData->fenceColor);
                                FillRect(hdc, &titleRect, titleBrush);
                                DeleteObject(titleBrush);

                                // 繪製標題文字
                                SetBkMode(hdc, TRANSPARENT);
                                SetTextColor(hdc, RGB(255, 255, 255));
                                SelectObject(hdc, pData->hBtnFont);  // 使用字體
                                RECT textRect = { 0, 9, rect.right, 31 };
                                DrawTextW(hdc, L"重新命名柵欄", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                                return TRUE;
                            } else if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC) {
                                // 設定純白色背景
                                HDC hdc = (HDC)wParam;
                                SetBkColor(hdc, RGB(240, 240, 240));
                                SetTextColor(hdc, RGB(0, 0, 0));
                                return (LRESULT)pData->hBrushBkgnd;
                            } else if (msg == WM_DRAWITEM) {
                                // 自訂繪製按鈕
                                DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;

                                if (pDIS->hwndItem == pData->hBtnOK) {
                                    // 繪製確定按鈕（圓角）
                                    COLORREF btnColor = (pDIS->itemState & ODS_SELECTED) ?
                                        RGB(GetRValue(pData->fenceColor) * 0.7, GetGValue(pData->fenceColor) * 0.7, GetBValue(pData->fenceColor) * 0.7) :
                                        pData->fenceColor;

                                    HBRUSH hBrush = CreateSolidBrush(btnColor);
                                    HPEN hPen = CreatePen(PS_SOLID, 1, btnColor);
                                    SelectObject(pDIS->hDC, hBrush);
                                    SelectObject(pDIS->hDC, hPen);
                                    RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 8, 8);
                                    DeleteObject(hBrush);
                                    DeleteObject(hPen);

                                    SetBkMode(pDIS->hDC, TRANSPARENT);
                                    SetTextColor(pDIS->hDC, RGB(255, 255, 255));
                                    SelectObject(pDIS->hDC, pData->hBtnFont);
                                    DrawTextW(pDIS->hDC, L"確定", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                                    return TRUE;
                                } else if (pDIS->hwndItem == pData->hBtnCancel) {
                                    // 繪製取消按鈕（圓角）
                                    COLORREF btnColor = (pDIS->itemState & ODS_SELECTED) ? RGB(200, 200, 200) : RGB(230, 230, 230);
                                    HBRUSH hBrush = CreateSolidBrush(btnColor);
                                    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
                                    SelectObject(pDIS->hDC, hBrush);
                                    SelectObject(pDIS->hDC, hPen);
                                    RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 8, 8);
                                    DeleteObject(hBrush);
                                    DeleteObject(hPen);

                                    SetBkMode(pDIS->hDC, TRANSPARENT);
                                    SetTextColor(pDIS->hDC, RGB(60, 60, 60));
                                    SelectObject(pDIS->hDC, pData->hBtnFont);
                                    DrawTextW(pDIS->hDC, L"取消", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                                    return TRUE;
                                }
                            } else if (msg == WM_COMMAND) {
                                HWND cmdHwnd = (HWND)lParam;

                                if (cmdHwnd == pData->hBtnOK) {
                                    GetWindowTextW(pData->hEdit, pData->pTitle, 256);
                                    *(pData->pResult) = true;
                                    *(pData->pRunning) = false;
                                    return 0;
                                } else if (cmdHwnd == pData->hBtnCancel) {
                                    *(pData->pRunning) = false;
                                    return 0;
                                }
                            } else if (msg == WM_CLOSE) {
                                *(pData->pRunning) = false;
                                return 0;
                            }
                        }

                        if (pData && pData->oldProc) {
                            return CallWindowProcW(pData->oldProc, hwnd, msg, wParam, lParam);
                        }
                        return DefWindowProcW(hwnd, msg, wParam, lParam);
                    };

                    SetWindowLongPtrW(hDialog, GWLP_WNDPROC, (LONG_PTR)static_cast<WNDPROC>(+newProc));

                    // 對話框訊息迴圈
                    MSG dialogMsg;
                    while (dialogRunning && GetMessageW(&dialogMsg, nullptr, 0, 0)) {
                        TranslateMessage(&dialogMsg);
                        DispatchMessageW(&dialogMsg);
                    }

                    // 恢復原始窗口過程
                    SetWindowLongPtrW(hDialog, GWLP_WNDPROC, (LONG_PTR)data.oldProc);

                    // 清理資源
                    DeleteObject(hTitleFont);
                    DeleteObject(hLabelFont);
                    DeleteObject(hEditFont);
                    DeleteObject(hBtnFont);
                    DeleteObject(data.hBrushBkgnd);
                    DestroyWindow(hDialog);

                    if (dialogResult && wcslen(newTitle) > 0) {
                        fence->title = newTitle;
                        SetWindowTextW(hwnd, newTitle);
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
                break;
            }

            case IDM_CHANGE_COLOR: {
                // Use Windows color picker dialog
                CHOOSECOLORW cc = { 0 };
                static COLORREF customColors[16] = { 0 };

                cc.lStructSize = sizeof(CHOOSECOLORW);
                cc.hwndOwner = hwnd;
                cc.lpCustColors = customColors;
                cc.rgbResult = fence->backgroundColor;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                if (ChooseColorW(&cc)) {
                    fence->backgroundColor = cc.rgbResult;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                break;
            }

            case IDM_CHANGE_TITLE_COLOR: {
                // Use Windows color picker dialog for title color
                CHOOSECOLORW cc = { 0 };
                static COLORREF customTitleColors[16] = { 0 };

                cc.lStructSize = sizeof(CHOOSECOLORW);
                cc.hwndOwner = hwnd;
                cc.lpCustColors = customTitleColors;
                cc.rgbResult = fence->titleColor;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                if (ChooseColorW(&cc)) {
                    fence->titleColor = cc.rgbResult;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                break;
            }

            case IDM_CHANGE_TRANSPARENCY: {
                // 註冊自訂對話框窗口類別（與上面共用）
                static bool classRegistered2 = false;
                const wchar_t* dialogClassName2 = L"FencesCustomDialog2";

                if (!classRegistered2) {
                    WNDCLASSEXW wcex = { 0 };
                    wcex.cbSize = sizeof(WNDCLASSEX);
                    wcex.lpfnWndProc = DefWindowProcW;
                    wcex.hInstance = hInstance_;
                    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
                    wcex.hbrBackground = CreateSolidBrush(RGB(240, 240, 240));
                    wcex.lpszClassName = dialogClassName2;
                    RegisterClassExW(&wcex);
                    classRegistered2 = true;
                }

                // 建立透明度滑動條對話框（美化 - 無標題列）
                HWND hDialog = CreateWindowExW(
                    WS_EX_TOPMOST | WS_EX_LAYERED,
                    dialogClassName2,
                    L"",
                    WS_POPUP | WS_VISIBLE,
                    0, 0, 380, 200,
                    hwnd,
                    nullptr,
                    hInstance_,
                    nullptr
                );

                if (hDialog) {
                    // 設定圓角和陰影效果
                    SetLayeredWindowAttributes(hDialog, 0, 250, LWA_ALPHA);

                    // 設定圓角窗口（Windows 11 風格）
                    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
                    DwmSetWindowAttribute(hDialog, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

                    // 對話框置中
                    RECT rcDialog, rcOwner;
                    GetWindowRect(hDialog, &rcDialog);
                    GetWindowRect(hwnd, &rcOwner);
                    int dialogWidth = rcDialog.right - rcDialog.left;
                    int dialogHeight = rcDialog.bottom - rcDialog.top;
                    int x = rcOwner.left + (rcOwner.right - rcOwner.left - dialogWidth) / 2;
                    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - dialogHeight) / 2;
                    SetWindowPos(hDialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

                    // 不使用子控制項，改為在對話框 WM_PAINT 繪製標題
                    HWND hTitleBar = nullptr;

                    HFONT hTitleFont = CreateFontW(
                        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");

                    // 建立透明度百分比標籤
                    int percentage = (fence->alpha * 100) / 255;
                    wchar_t labelText[64];
                    swprintf_s(labelText, 64, L"透明度: %d%%", percentage);
                    HWND hLabel = CreateWindowExW(0, L"STATIC", labelText,
                        WS_CHILD | WS_VISIBLE | SS_CENTER,
                        20, 60, 340, 28,
                        hDialog, (HMENU)101, hInstance_, nullptr);

                    HFONT hLabelFont = CreateFontW(
                        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(hLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);

                    // 強制讓標籤使用父窗口背景
                    SetWindowLongPtrW(hLabel, GWL_EXSTYLE, GetWindowLongPtrW(hLabel, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

                    // 建立滑動條 - 範圍: 20% 到 100% (51 到 255)
                    HWND hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP | TBS_NOTICKS,
                        30, 100, 320, 30,
                        hDialog, (HMENU)102, hInstance_, nullptr);

                    // 設定滑動條範圍: 51 (20%) 到 255 (100%)
                    SendMessageW(hTrackbar, TBM_SETRANGE, TRUE, MAKELONG(51, 255));
                    SendMessageW(hTrackbar, TBM_SETPOS, TRUE, fence->alpha);
                    SendMessageW(hTrackbar, TBM_SETPAGESIZE, 0, 10);

                    // 建立確定按鈕
                    HWND hBtnOK = CreateWindowExW(0, L"BUTTON", L"確定",
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                        210, 150, 70, 30,
                        hDialog, (HMENU)IDOK, hInstance_, nullptr);

                    // 建立取消按鈕
                    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                        290, 150, 70, 30,
                        hDialog, (HMENU)IDCANCEL, hInstance_, nullptr);

                    HFONT hBtnFont = CreateFontW(
                        18, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");
                    SendMessageW(hBtnOK, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
                    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hBtnFont, TRUE);

                    // 子類化視窗以處理按鈕點擊、滑動條和拖拉
                    struct TransparencyDialogData {
                        bool* pRunning;
                        bool* pResult;
                        int* pCurrentAlpha;
                        int* pOriginalAlpha;
                        HWND hTrackbar;
                        HWND hLabel;
                        HWND hBtnOK;
                        HWND hBtnCancel;
                        HWND hTitleBar;
                        HWND hFenceWnd;
                        Fence* pFence;
                        wchar_t* labelText;
                        WNDPROC oldProc;
                        HFONT hBtnFont;
                        bool isDragging;
                        POINT dragOffset;
                        COLORREF fenceColor;
                        HBRUSH hBrushBkgnd;
                    };

                    bool dialogResult = false;
                    bool dialogRunning = true;
                    int currentAlpha = fence->alpha;
                    int originalAlpha = fence->alpha;

                    TransparencyDialogData data;
                    data.pRunning = &dialogRunning;
                    data.pResult = &dialogResult;
                    data.pCurrentAlpha = &currentAlpha;
                    data.pOriginalAlpha = &originalAlpha;
                    data.hTrackbar = hTrackbar;
                    data.hLabel = hLabel;
                    data.hBtnOK = hBtnOK;
                    data.hBtnCancel = hBtnCancel;
                    data.hTitleBar = hTitleBar;
                    data.hFenceWnd = hwnd;
                    data.pFence = fence;
                    data.labelText = labelText;
                    data.hBtnFont = hBtnFont;
                    data.isDragging = false;
                    data.dragOffset = { 0, 0 };
                    data.fenceColor = fence->borderColor;
                    data.hBrushBkgnd = CreateSolidBrush(RGB(240, 240, 240));

                    // 保存原始窗口過程
                    data.oldProc = (WNDPROC)GetWindowLongPtrW(hDialog, GWLP_WNDPROC);

                    // 設置用戶數據
                    SetWindowLongPtrW(hDialog, GWLP_USERDATA, (LONG_PTR)&data);

                    // 子類化窗口
                    auto newProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
                        TransparencyDialogData* pData = (TransparencyDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

                        if (pData) {
                            if (msg == WM_NCHITTEST) {
                                // 讓標題列區域可拖拉
                                LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
                                if (hit == HTCLIENT) {
                                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                                    ScreenToClient(hwnd, &pt);
                                    if (pt.y >= 0 && pt.y < 38) {
                                        return HTCAPTION;  // 讓系統處理拖拉
                                    }
                                }
                                return hit;
                            } else if (msg == WM_ERASEBKGND) {
                                // 自訂背景繪製
                                HDC hdc = (HDC)wParam;
                                RECT rect;
                                GetClientRect(hwnd, &rect);

                                // 繪製淺灰色背景（與控制項背景統一）
                                HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
                                FillRect(hdc, &rect, bgBrush);
                                DeleteObject(bgBrush);

                                // 繪製標題列（使用柵欄顏色）
                                RECT titleRect = { 0, 0, rect.right, 38 };
                                HBRUSH titleBrush = CreateSolidBrush(pData->fenceColor);
                                FillRect(hdc, &titleRect, titleBrush);
                                DeleteObject(titleBrush);

                                // 繪製標題文字
                                SetBkMode(hdc, TRANSPARENT);
                                SetTextColor(hdc, RGB(255, 255, 255));
                                SelectObject(hdc, pData->hBtnFont);
                                RECT textRect = { 0, 9, rect.right, 31 };
                                DrawTextW(hdc, L"調整透明度", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                                return TRUE;
                            } else if (msg == WM_CTLCOLORSTATIC) {
                                // 設定純白色背景
                                HDC hdc = (HDC)wParam;
                                SetBkColor(hdc, RGB(240, 240, 240));
                                SetTextColor(hdc, RGB(0, 0, 0));
                                return (LRESULT)pData->hBrushBkgnd;
                            } else if (msg == WM_DRAWITEM) {
                                // 自訂繪製按鈕
                                DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;

                                if (pDIS->hwndItem == pData->hBtnOK) {
                                    // 繪製確定按鈕（圓角）
                                    COLORREF btnColor = (pDIS->itemState & ODS_SELECTED) ?
                                        RGB(GetRValue(pData->fenceColor) * 0.7, GetGValue(pData->fenceColor) * 0.7, GetBValue(pData->fenceColor) * 0.7) :
                                        pData->fenceColor;

                                    HBRUSH hBrush = CreateSolidBrush(btnColor);
                                    HPEN hPen = CreatePen(PS_SOLID, 1, btnColor);
                                    SelectObject(pDIS->hDC, hBrush);
                                    SelectObject(pDIS->hDC, hPen);
                                    RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 8, 8);
                                    DeleteObject(hBrush);
                                    DeleteObject(hPen);

                                    SetBkMode(pDIS->hDC, TRANSPARENT);
                                    SetTextColor(pDIS->hDC, RGB(255, 255, 255));
                                    SelectObject(pDIS->hDC, pData->hBtnFont);
                                    DrawTextW(pDIS->hDC, L"確定", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                                    return TRUE;
                                } else if (pDIS->hwndItem == pData->hBtnCancel) {
                                    // 繪製取消按鈕（圓角）
                                    COLORREF btnColor = (pDIS->itemState & ODS_SELECTED) ? RGB(200, 200, 200) : RGB(230, 230, 230);
                                    HBRUSH hBrush = CreateSolidBrush(btnColor);
                                    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
                                    SelectObject(pDIS->hDC, hBrush);
                                    SelectObject(pDIS->hDC, hPen);
                                    RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 8, 8);
                                    DeleteObject(hBrush);
                                    DeleteObject(hPen);

                                    SetBkMode(pDIS->hDC, TRANSPARENT);
                                    SetTextColor(pDIS->hDC, RGB(60, 60, 60));
                                    SelectObject(pDIS->hDC, pData->hBtnFont);
                                    DrawTextW(pDIS->hDC, L"取消", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                                    return TRUE;
                                }
                            } else if (msg == WM_HSCROLL && (HWND)lParam == pData->hTrackbar) {
                                // 即時更新透明度
                                *(pData->pCurrentAlpha) = (int)SendMessageW(pData->hTrackbar, TBM_GETPOS, 0, 0);
                                int newPercentage = (*(pData->pCurrentAlpha) * 100) / 255;
                                swprintf_s(pData->labelText, 64, L"透明度: %d%%", newPercentage);
                                SetWindowTextW(pData->hLabel, pData->labelText);

                                // 即時套用透明度到柵欄窗口
                                pData->pFence->alpha = *(pData->pCurrentAlpha);
                                SetLayeredWindowAttributes(pData->hFenceWnd, 0, static_cast<BYTE>(*(pData->pCurrentAlpha)), LWA_ALPHA);
                                RedrawWindow(pData->hFenceWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                                return 0;
                            } else if (msg == WM_COMMAND) {
                                HWND cmdHwnd = (HWND)lParam;

                                if (cmdHwnd == pData->hBtnOK) {
                                    *(pData->pCurrentAlpha) = (int)SendMessageW(pData->hTrackbar, TBM_GETPOS, 0, 0);
                                    *(pData->pResult) = true;
                                    *(pData->pRunning) = false;
                                    return 0;
                                } else if (cmdHwnd == pData->hBtnCancel) {
                                    // 恢復原始透明度
                                    pData->pFence->alpha = *(pData->pOriginalAlpha);
                                    SetLayeredWindowAttributes(pData->hFenceWnd, 0, static_cast<BYTE>(*(pData->pOriginalAlpha)), LWA_ALPHA);
                                    RedrawWindow(pData->hFenceWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                                    *(pData->pRunning) = false;
                                    return 0;
                                }
                            } else if (msg == WM_CLOSE) {
                                // 恢復原始透明度
                                pData->pFence->alpha = *(pData->pOriginalAlpha);
                                SetLayeredWindowAttributes(pData->hFenceWnd, 0, static_cast<BYTE>(*(pData->pOriginalAlpha)), LWA_ALPHA);
                                RedrawWindow(pData->hFenceWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
                                *(pData->pRunning) = false;
                                return 0;
                            }
                        }

                        if (pData && pData->oldProc) {
                            return CallWindowProcW(pData->oldProc, hwnd, msg, wParam, lParam);
                        }
                        return DefWindowProcW(hwnd, msg, wParam, lParam);
                    };

                    SetWindowLongPtrW(hDialog, GWLP_WNDPROC, (LONG_PTR)static_cast<WNDPROC>(+newProc));

                    // 對話框訊息迴圈
                    MSG dialogMsg;
                    while (dialogRunning && GetMessageW(&dialogMsg, nullptr, 0, 0)) {
                        TranslateMessage(&dialogMsg);
                        DispatchMessageW(&dialogMsg);
                    }

                    // 恢復原始窗口過程
                    SetWindowLongPtrW(hDialog, GWLP_WNDPROC, (LONG_PTR)data.oldProc);

                    // 清理資源
                    DeleteObject(hTitleFont);
                    DeleteObject(hLabelFont);
                    DeleteObject(hBtnFont);
                    DeleteObject(data.hBrushBkgnd);
                    DestroyWindow(hDialog);

                    if (dialogResult) {
                        fence->alpha = currentAlpha;
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
                break;
            }

            case IDM_CREATE_FENCE: {
                // 建立新柵欄，位置偏移於目前柵欄
                RECT rect;
                GetWindowRect(hwnd, &rect);
                CreateFence(rect.left + 50, rect.top + 50,
                          rect.right - rect.left,
                          rect.bottom - rect.top,
                          L"新柵欄");
                break;
            }

            case IDM_DELETE_FENCE: {
                // Find and remove this fence
                for (size_t i = 0; i < fences_.size(); ++i) {
                    if (&fences_[i] == fence) {
                        RemoveFence(i);
                        break;
                    }
                }
                break;
            }

            case IDM_AUTO_CATEGORIZE:
                AutoCategorizeDesktopIcons();
                break;

            case IDM_ICON_SIZE_32:
                fence->iconSize = 32;
                ArrangeIcons(fence);
                InvalidateRect(hwnd, nullptr, TRUE);
                break;

            case IDM_ICON_SIZE_48:
                fence->iconSize = 48;
                ArrangeIcons(fence);
                InvalidateRect(hwnd, nullptr, TRUE);
                break;

            case IDM_ICON_SIZE_64:
                fence->iconSize = 64;
                ArrangeIcons(fence);
                InvalidateRect(hwnd, nullptr, TRUE);
                break;

            case IDM_REMOVE_ICON:
                if (selectedFence_ && selectedIconIndex_ >= 0) {
                    RemoveIconFromFence(selectedFence_, selectedIconIndex_);
                    selectedIconIndex_ = -1;
                    selectedFence_ = nullptr;
                }
                break;
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (fence) {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int visibleHeight = clientRect.bottom - TITLE_BAR_HEIGHT;

            // Only handle scrolling if content exceeds visible area
            if (fence->contentHeight > visibleHeight) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int scrollAmount = -delta / 3;  // Scroll speed adjustment

                // Update scroll offset
                fence->scrollOffset += scrollAmount;

                // Clamp scroll offset
                int maxScroll = fence->contentHeight - visibleHeight;
                if (fence->scrollOffset < 0) {
                    fence->scrollOffset = 0;
                }
                if (fence->scrollOffset > maxScroll) {
                    fence->scrollOffset = maxScroll;
                }

                // Redraw fence
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void FencesWidget::PaintFence(HWND hwnd, HDC hdc) {
    Fence* fence = FindFence(hwnd);
    if (!fence) {
        return;
    }

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // Create memory DC for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
        clientRect.right - clientRect.left,
        clientRect.bottom - clientRect.top);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(fence->backgroundColor);
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    // Draw title bar with darker background
    if (!fence->title.empty()) {
        RECT titleBarRect = clientRect;
        titleBarRect.bottom = TITLE_BAR_HEIGHT;

        // Darken the background color for title bar
        int r = GetRValue(fence->backgroundColor);
        int g = GetGValue(fence->backgroundColor);
        int b = GetBValue(fence->backgroundColor);
        COLORREF titleBarColor = RGB(
            max(0, r - 20),
            max(0, g - 20),
            max(0, b - 20)
        );

        HBRUSH titleBrush = CreateSolidBrush(titleBarColor);
        FillRect(memDC, &titleBarRect, titleBrush);
        DeleteObject(titleBrush);

        // Draw title text
        RECT titleTextRect = titleBarRect;
        titleTextRect.left += 10;
        titleTextRect.right -= 10;

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, fence->titleColor);

        HFONT hFont = CreateFontW(
            16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
        DrawTextW(memDC, fence->title.c_str(), -1, &titleTextRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);
        DeleteObject(hFont);

        // 繪製右上角圖示：收合和釘住
        const int iconSize = 20;
        const int iconMargin = 5;
        int rightX = titleBarRect.right - iconMargin;

        // 繪製釘住圖示（第二個，最右邊）
        RECT pinRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                         rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };

        // 繪製圓角矩形背景
        HBRUSH pinBrush = CreateSolidBrush(fence->isPinned ? RGB(100, 150, 255) : RGB(180, 180, 180));
        HPEN pinPen = CreatePen(PS_SOLID, 1, fence->isPinned ? RGB(70, 120, 200) : RGB(150, 150, 150));
        SelectObject(memDC, pinBrush);
        SelectObject(memDC, pinPen);
        RoundRect(memDC, pinRect.left, pinRect.top, pinRect.right, pinRect.bottom, 4, 4);
        DeleteObject(pinBrush);
        DeleteObject(pinPen);

        // 繪製釘子圖示（簡化的圖釘）
        HPEN iconPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        SelectObject(memDC, iconPen);
        int pinCenterX = (pinRect.left + pinRect.right) / 2;
        int pinCenterY = (pinRect.top + pinRect.bottom) / 2;
        Ellipse(memDC, pinCenterX - 3, pinCenterY - 4, pinCenterX + 3, pinCenterY + 2);
        MoveToEx(memDC, pinCenterX, pinCenterY + 2, nullptr);
        LineTo(memDC, pinCenterX, pinCenterY + 7);
        DeleteObject(iconPen);

        rightX -= (iconSize + iconMargin);

        // 繪製收合圖示（第一個）
        RECT collapseRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                              rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };

        HBRUSH collapseBrush = CreateSolidBrush(fence->isCollapsed ? RGB(255, 150, 100) : RGB(180, 180, 180));
        HPEN collapsePen = CreatePen(PS_SOLID, 1, fence->isCollapsed ? RGB(200, 120, 70) : RGB(150, 150, 150));
        SelectObject(memDC, collapseBrush);
        SelectObject(memDC, collapsePen);
        RoundRect(memDC, collapseRect.left, collapseRect.top, collapseRect.right, collapseRect.bottom, 4, 4);
        DeleteObject(collapseBrush);
        DeleteObject(collapsePen);

        // 繪製箭頭（向下=展開，向上=收合）
        HPEN arrowPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        SelectObject(memDC, arrowPen);
        int arrowCenterX = (collapseRect.left + collapseRect.right) / 2;
        int arrowCenterY = (collapseRect.top + collapseRect.bottom) / 2;
        if (fence->isCollapsed) {
            // 向下箭頭（展開）
            MoveToEx(memDC, arrowCenterX - 5, arrowCenterY - 2, nullptr);
            LineTo(memDC, arrowCenterX, arrowCenterY + 3);
            LineTo(memDC, arrowCenterX + 5, arrowCenterY - 2);
        } else {
            // 向上箭頭（收合）
            MoveToEx(memDC, arrowCenterX - 5, arrowCenterY + 2, nullptr);
            LineTo(memDC, arrowCenterX, arrowCenterY - 3);
            LineTo(memDC, arrowCenterX + 5, arrowCenterY + 2);
        }
        DeleteObject(arrowPen);
    }

    // Draw border
    HPEN borderPen = CreatePen(PS_SOLID, fence->borderWidth, fence->borderColor);
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

    Rectangle(memDC,
        clientRect.left,
        clientRect.top,
        clientRect.right,
        clientRect.bottom);

    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    // 繪製圖示或提示文字（僅在未收合時）
    if (!fence->isCollapsed) {
        if (fence->icons.empty()) {
            // 繪製「拖曳檔案到這裡」提示
            RECT hintRect = clientRect;
            hintRect.top = TITLE_BAR_HEIGHT + 20;

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(150, 150, 150));

            HFONT hFont = CreateFontW(
                14, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");

            HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
            DrawTextW(memDC, L"拖曳檔案到這裡...", -1, &hintRect,
                DT_CENTER | DT_TOP | DT_SINGLELINE);
            SelectObject(memDC, oldFont);
            DeleteObject(hFont);
        } else {
            // Set clipping region to icon area (below title bar)
            RECT iconArea = clientRect;
            iconArea.top = TITLE_BAR_HEIGHT;
            HRGN hClipRgn = CreateRectRgnIndirect(&iconArea);
            SelectClipRgn(memDC, hClipRgn);

            // Draw all icons with scroll offset applied
            for (auto& icon : fence->icons) {
                int adjustedY = icon.position.y - fence->scrollOffset;

                // Only draw icons within visible area (with some margin for partial visibility)
                if (adjustedY + fence->iconSize + 35 >= TITLE_BAR_HEIGHT &&
                    adjustedY < clientRect.bottom) {
                    DrawIcon(memDC, icon, icon.position.x, adjustedY, fence->iconSize);
                }
            }

            // Remove clipping region
            SelectClipRgn(memDC, nullptr);
            DeleteObject(hClipRgn);
        }
    }

    // Draw scrollbar when content overflows (僅在未收合時)
    if (!fence->isCollapsed) {
        int visibleHeight = clientRect.bottom - TITLE_BAR_HEIGHT;
        if (fence->contentHeight > visibleHeight) {
            const int scrollbarWidth = 8;
            const int scrollbarMargin = 2;
            const int scrollbarX = clientRect.right - scrollbarWidth - scrollbarMargin;

            // Draw scrollbar track
            RECT trackRect = {
                scrollbarX,
                TITLE_BAR_HEIGHT + scrollbarMargin,
                scrollbarX + scrollbarWidth,
                clientRect.bottom - scrollbarMargin
            };
            HBRUSH trackBrush = CreateSolidBrush(RGB(200, 200, 200));
            FillRect(memDC, &trackRect, trackBrush);
            DeleteObject(trackBrush);

            // Calculate scrollbar thumb size and position
            int trackHeight = trackRect.bottom - trackRect.top;
            int thumbHeight = max(20, (visibleHeight * trackHeight) / fence->contentHeight);
            int maxScroll = fence->contentHeight - visibleHeight;
            int thumbY = trackRect.top + (fence->scrollOffset * (trackHeight - thumbHeight)) / maxScroll;

            // Draw scrollbar thumb
            RECT thumbRect = {
                scrollbarX,
                thumbY,
                scrollbarX + scrollbarWidth,
                thumbY + thumbHeight
            };
            HBRUSH thumbBrush = CreateSolidBrush(RGB(120, 120, 120));
            FillRect(memDC, &thumbRect, thumbBrush);
            DeleteObject(thumbBrush);
        }
    }

    // Draw resize indicator (僅在未收合時)
    if (!fence->isCollapsed) {
        HBRUSH resizeIndicatorBrush = CreateSolidBrush(RGB(120, 120, 120));
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (i + j >= 2) {
                    RECT dotRect = {
                        clientRect.right - 12 + (i * 4),
                        clientRect.bottom - 12 + (j * 4),
                        clientRect.right - 10 + (i * 4),
                        clientRect.bottom - 10 + (j * 4)
                    };
                    FillRect(memDC, &dotRect, resizeIndicatorBrush);
                }
            }
        }
        DeleteObject(resizeIndicatorBrush);
    }

    // Copy to screen
    BitBlt(hdc, 0, 0,
        clientRect.right - clientRect.left,
        clientRect.bottom - clientRect.top,
        memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

Fence* FencesWidget::FindFence(HWND hwnd) {
    for (auto& fence : fences_) {
        if (fence.hwnd == hwnd) {
            return &fence;
        }
    }
    return nullptr;
}

void FencesWidget::OnLButtonDown(Fence* fence, int x, int y) {
    RECT clientRect;
    GetClientRect(fence->hwnd, &clientRect);

    // 檢查是否點擊標題列中的圖示
    if (y >= 0 && y < TITLE_BAR_HEIGHT) {
        const int iconSize = 20;
        const int iconMargin = 5;
        int rightX = clientRect.right - iconMargin;

        // 檢查釘住圖示（最右邊）
        RECT pinRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                         rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };
        if (x >= pinRect.left && x <= pinRect.right && y >= pinRect.top && y <= pinRect.bottom) {
            fence->isPinned = !fence->isPinned;
            InvalidateRect(fence->hwnd, nullptr, FALSE);
            return;
        }

        rightX -= (iconSize + iconMargin);

        // 檢查收合圖示（第二個）
        RECT collapseRect = { rightX - iconSize, (TITLE_BAR_HEIGHT - iconSize) / 2,
                              rightX, (TITLE_BAR_HEIGHT - iconSize) / 2 + iconSize };
        if (x >= collapseRect.left && x <= collapseRect.right && y >= collapseRect.top && y <= collapseRect.bottom) {
            fence->isCollapsed = !fence->isCollapsed;

            RECT rect;
            GetWindowRect(fence->hwnd, &rect);
            int width = rect.right - rect.left;

            if (fence->isCollapsed) {
                // 收合：保存當前高度，然後設定為標題高度
                fence->expandedHeight = rect.bottom - rect.top;
                SetWindowPos(fence->hwnd, nullptr, 0, 0, width, TITLE_BAR_HEIGHT,
                    SWP_NOMOVE | SWP_NOZORDER);
                fence->rect.bottom = fence->rect.top + TITLE_BAR_HEIGHT;
            } else {
                // 展開：恢復原始高度
                SetWindowPos(fence->hwnd, nullptr, 0, 0, width, fence->expandedHeight,
                    SWP_NOMOVE | SWP_NOZORDER);
                fence->rect.bottom = fence->rect.top + fence->expandedHeight;
            }

            InvalidateRect(fence->hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check if clicking on scrollbar first
    if (IsInScrollbarArea(fence, x, y)) {
        fence->isDraggingScrollbar = true;
        fence->scrollbarDragStartY = y;
        fence->scrollOffsetAtDragStart = fence->scrollOffset;
        SetCapture(fence->hwnd);
        return;
    }

    // Check if clicking on an icon
    int iconIndex = FindIconAtPosition(fence, x, y);

    if (iconIndex >= 0) {
        // Start icon drag
        fence->isDraggingIcon = true;
        fence->draggingIconIndex = iconIndex;
        fence->iconDragStart.x = x;
        fence->iconDragStart.y = y;
        SetCapture(fence->hwnd);

        // 創建拖拉圖示的影像列表（使用快取的圖示）
        HICON hIcon = nullptr;
        if (fence->iconSize == 32 && fence->icons[iconIndex].hIcon32) {
            hIcon = fence->icons[iconIndex].hIcon32;
        } else if (fence->iconSize == 48 && fence->icons[iconIndex].hIcon48) {
            hIcon = fence->icons[iconIndex].hIcon48;
        } else if (fence->iconSize == 64 && fence->icons[iconIndex].hIcon64) {
            hIcon = fence->icons[iconIndex].hIcon64;
        } else if (fence->icons[iconIndex].hIcon) {
            hIcon = fence->icons[iconIndex].hIcon;
        }

        if (hIcon) {
            // 創建 ImageList
            HIMAGELIST hImageList = ImageList_Create(fence->iconSize, fence->iconSize,
                                                      ILC_COLOR32 | ILC_MASK, 1, 1);
            if (hImageList) {
                // 添加圖示到 ImageList
                int index = ImageList_AddIcon(hImageList, hIcon);
                if (index >= 0) {
                    // 開始拖拉
                    POINT ptCursor;
                    GetCursorPos(&ptCursor);
                    ImageList_BeginDrag(hImageList, index, fence->iconSize / 2, fence->iconSize / 2);
                    ImageList_DragEnter(GetDesktopWindow(), ptCursor.x, ptCursor.y);
                }
            }
        }
    } else if (IsInResizeArea(clientRect, x, y)) {
        fence->isResizing = true;
        SetCapture(fence->hwnd);
    } else if (IsInTitleArea(clientRect, x, y)) {
        // 如果釘住了，不允許拖動
        if (!fence->isPinned) {
            fence->isDragging = true;
            fence->dragOffset.x = x;
            fence->dragOffset.y = y;
            SetCapture(fence->hwnd);
        }
    }
}

void FencesWidget::OnMouseMove(Fence* fence, int x, int y) {
    if (fence->isDraggingScrollbar) {
        // Handle scrollbar dragging
        RECT clientRect;
        GetClientRect(fence->hwnd, &clientRect);
        int visibleHeight = clientRect.bottom - TITLE_BAR_HEIGHT;

        const int scrollbarMargin = 2;
        int trackHeight = clientRect.bottom - TITLE_BAR_HEIGHT - 2 * scrollbarMargin;
        int thumbHeight = max(20, (visibleHeight * trackHeight) / fence->contentHeight);
        int maxScroll = fence->contentHeight - visibleHeight;

        // Calculate new scroll offset based on mouse movement
        int deltaY = y - fence->scrollbarDragStartY;
        int scrollDelta = (deltaY * maxScroll) / (trackHeight - thumbHeight);
        int newScrollOffset = fence->scrollOffsetAtDragStart + scrollDelta;

        // Clamp scroll offset
        if (newScrollOffset < 0) {
            newScrollOffset = 0;
        }
        if (newScrollOffset > maxScroll) {
            newScrollOffset = maxScroll;
        }

        fence->scrollOffset = newScrollOffset;
        InvalidateRect(fence->hwnd, nullptr, TRUE);
    } else if (fence->isDraggingIcon) {
        // 更新拖拉圖示位置
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        ImageList_DragMove(ptCursor.x, ptCursor.y);

        // 檢查是否拖到柵欄外
        RECT clientRect;
        GetClientRect(fence->hwnd, &clientRect);

        // 計算從開始位置的距離
        int deltaX = abs(x - fence->iconDragStart.x);
        int deltaY = abs(y - fence->iconDragStart.y);

        // 如果拖動超過5像素，使用標準箭頭游標
        if (deltaX > 5 || deltaY > 5) {
            // 始終使用標準箭頭游標
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
    } else if (fence->isResizing) {
        RECT rect;
        GetWindowRect(fence->hwnd, &rect);

        int newWidth = x;
        int newHeight = y;

        if (newWidth < 150) newWidth = 150;
        if (newHeight < 100) newHeight = 100;

        SetWindowPos(fence->hwnd, nullptr, 0, 0, newWidth, newHeight,
            SWP_NOMOVE | SWP_NOZORDER);

        fence->rect.right = fence->rect.left + newWidth;
        fence->rect.bottom = fence->rect.top + newHeight;

        // Rearrange icons after resize
        ArrangeIcons(fence);
    } else if (fence->isDragging) {
        POINT pt;
        GetCursorPos(&pt);

        int newX = pt.x - fence->dragOffset.x;
        int newY = pt.y - fence->dragOffset.y;

        RECT rect;
        GetWindowRect(fence->hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        SetWindowPos(fence->hwnd, nullptr, newX, newY, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);

        fence->rect.left = newX;
        fence->rect.top = newY;
        fence->rect.right = newX + width;
        fence->rect.bottom = newY + height;
    }
}

void FencesWidget::OnLButtonUp(Fence* fence) {
    if (fence->isDraggingScrollbar) {
        fence->isDraggingScrollbar = false;
        ReleaseCapture();
    } else if (fence->isDraggingIcon && fence->draggingIconIndex >= 0 &&
        fence->draggingIconIndex < (int)fence->icons.size()) {

        // 結束拖拉圖示顯示
        ImageList_DragLeave(GetDesktopWindow());
        ImageList_EndDrag();

        // 取得目前滑鼠位置（相對於柵欄）
        POINT ptClient;
        GetCursorPos(&ptClient);
        ScreenToClient(fence->hwnd, &ptClient);

        // 檢查是否拖到柵欄外
        RECT clientRect;
        GetClientRect(fence->hwnd, &clientRect);

        if (ptClient.x < 0 || ptClient.x > clientRect.right ||
            ptClient.y < 0 || ptClient.y > clientRect.bottom) {
            // 圖示被拖出柵欄 - 恢復到桌面並設定位置
            std::wstring filePath = fence->icons[fence->draggingIconIndex].filePath;

            // =========================================================================
            // NEW in v3.0: Remove icon from fence using RemoveIconFromFence
            // which handles managedIconPaths cleanup
            // =========================================================================
            RemoveIconFromFence(fence, fence->draggingIconIndex);
            OutputDebugStringW((L"[FencesWidget] Dragged icon out of fence: " + filePath + L"\n").c_str());
        }

        fence->isDraggingIcon = false;
        fence->draggingIconIndex = -1;
    }

    fence->isResizing = false;
    fence->isDragging = false;
    ReleaseCapture();
}

void FencesWidget::OnRButtonDown(Fence* fence, int x, int y) {
    POINT pt = { x, y };

    // Check if right-click is on an icon
    int iconIndex = FindIconAtPosition(fence, x, y);

    if (iconIndex >= 0) {
        // Show icon-specific context menu
        ClientToScreen(fence->hwnd, &pt);
        ShowIconContextMenu(fence, iconIndex, pt.x, pt.y);
    } else {
        // Show fence context menu
        ClientToScreen(fence->hwnd, &pt);
        ShowFenceContextMenu(fence, pt.x, pt.y);
    }
}

// ============================================================================
// NOTE: OnDropFiles removed in v3.1 - replaced by IDropTarget (DropTarget.cpp)
// Old WM_DROPFILES mechanism only supported COPY operations
// IDropTarget provides proper MOVE/COPY semantics based on keyboard state
// ============================================================================

bool FencesWidget::IsInResizeArea(const RECT& rect, int x, int y) const {
    const int resizeMargin = 15;
    return (x >= rect.right - resizeMargin && y >= rect.bottom - resizeMargin);
}

bool FencesWidget::IsInTitleArea(const RECT& rect, int x, int y) const {
    return (y >= 0 && y < TITLE_BAR_HEIGHT && x >= 0 && x < rect.right - 30);
}

bool FencesWidget::IsInScrollbarArea(Fence* fence, int x, int y, RECT* outThumbRect) const {
    if (!fence || fence->isCollapsed) {
        return false;
    }

    RECT clientRect;
    GetClientRect(fence->hwnd, &clientRect);
    int visibleHeight = clientRect.bottom - TITLE_BAR_HEIGHT;

    // Only show scrollbar if content overflows
    if (fence->contentHeight <= visibleHeight) {
        return false;
    }

    const int scrollbarWidth = 8;
    const int scrollbarMargin = 2;
    const int scrollbarX = clientRect.right - scrollbarWidth - scrollbarMargin;

    // Calculate scrollbar thumb position
    RECT trackRect = {
        scrollbarX,
        TITLE_BAR_HEIGHT + scrollbarMargin,
        scrollbarX + scrollbarWidth,
        clientRect.bottom - scrollbarMargin
    };

    int trackHeight = trackRect.bottom - trackRect.top;
    int thumbHeight = max(20, (visibleHeight * trackHeight) / fence->contentHeight);
    int maxScroll = fence->contentHeight - visibleHeight;
    int thumbY = trackRect.top + (fence->scrollOffset * (trackHeight - thumbHeight)) / maxScroll;

    RECT thumbRect = {
        scrollbarX,
        thumbY,
        scrollbarX + scrollbarWidth,
        thumbY + thumbHeight
    };

    if (outThumbRect) {
        *outThumbRect = thumbRect;
    }

    // Check if point is in scrollbar thumb
    return (x >= thumbRect.left && x <= thumbRect.right &&
            y >= thumbRect.top && y <= thumbRect.bottom);
}

bool FencesWidget::AddIconToFence(Fence* fence, const std::wstring& filePath) {
    if (!fence) {
        return false;
    }

    // Check if icon already exists
    for (const auto& icon : fence->icons) {
        if (icon.filePath == filePath) {
            return false; // Already exists
        }
    }

    DesktopIcon newIcon;
    newIcon.filePath = filePath;

    // 只載入當前需要的大小（延遲載入優化）
    newIcon.hIcon32 = nullptr;
    newIcon.hIcon48 = nullptr;
    newIcon.hIcon64 = nullptr;
    newIcon.hIcon = nullptr;
    newIcon.cachedIconSize = 0;

    // 立即載入當前柵欄使用的圖示大小
    if (fence->iconSize == 32) {
        newIcon.hIcon32 = GetFileIcon(filePath, 32);
    } else if (fence->iconSize == 48) {
        newIcon.hIcon48 = GetFileIcon(filePath, 48);
    } else if (fence->iconSize == 64) {
        newIcon.hIcon64 = GetFileIcon(filePath, 64);
    }

    newIcon.selected = false;
    newIcon.position = { 0, 0 }; // Will be set by ArrangeIcons

    // Extract display name
    size_t lastSlash = filePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        newIcon.displayName = filePath.substr(lastSlash + 1);
    } else {
        newIcon.displayName = filePath;
    }

    // Remove file extension for display
    size_t lastDot = newIcon.displayName.find_last_of(L'.');
    if (lastDot != std::wstring::npos && lastDot > 0) {
        newIcon.displayName = newIcon.displayName.substr(0, lastDot);
    }

    fence->icons.push_back(newIcon);

    // =========================================================================
    // NEW in v3.0: Use Custom Draw rendering suppression instead of hiding
    // =========================================================================
    // Add to managed icons set (O(1) operation)
    managedIconPaths_.insert(filePath);

    // Force desktop ListView to redraw (triggers Custom Draw)
    if (desktopListView_) {
        InvalidateRect(desktopListView_, nullptr, TRUE);
    }

    // Rearrange icons in fence
    ArrangeIcons(fence);

    // Redraw fence window
    InvalidateRect(fence->hwnd, nullptr, TRUE);

    OutputDebugStringW((L"[FencesWidget] Added icon to fence: " + filePath + L"\n").c_str());
    return true;
}

bool FencesWidget::RemoveIconFromFence(Fence* fence, size_t iconIndex) {
    if (!fence || iconIndex >= fence->icons.size()) {
        return false;
    }

    std::wstring filePath = fence->icons[iconIndex].filePath;

    // 清理所有快取的圖示
    if (fence->icons[iconIndex].hIcon) {
        DestroyIcon(fence->icons[iconIndex].hIcon);
    }
    if (fence->icons[iconIndex].hIcon32) {
        DestroyIcon(fence->icons[iconIndex].hIcon32);
    }
    if (fence->icons[iconIndex].hIcon48) {
        DestroyIcon(fence->icons[iconIndex].hIcon48);
    }
    if (fence->icons[iconIndex].hIcon64) {
        DestroyIcon(fence->icons[iconIndex].hIcon64);
    }

    fence->icons.erase(fence->icons.begin() + iconIndex);

    // =========================================================================
    // NEW in v4.0: Move file back to desktop using FileManager
    // =========================================================================
    if (fileManager_ && fileManager_->IsManagedFile(filePath)) {
        OutputDebugStringW((L"[FencesWidget] Moving file back to desktop: " + filePath + L"\n").c_str());

        MoveResult result = fileManager_->MoveBackToDesktop(filePath);

        if (result.success) {
            wchar_t msg[512];
            swprintf_s(msg, L"[FencesWidget] ✓ File restored to desktop: %s\n", result.newPath.c_str());
            OutputDebugStringW(msg);
        } else {
            wchar_t msg[512];
            swprintf_s(msg, L"[FencesWidget] ✗ Failed to restore file: %s (Error: %s)\n",
                       filePath.c_str(), result.errorMessage.c_str());
            OutputDebugStringW(msg);
        }
    } else {
        // Legacy path: just remove from managed set
        managedIconPaths_.erase(filePath);

        if (desktopListView_) {
            InvalidateRect(desktopListView_, nullptr, TRUE);
        }
    }

    ArrangeIcons(fence);
    InvalidateRect(fence->hwnd, nullptr, TRUE);

    OutputDebugStringW((L"[FencesWidget] Removed icon from fence: " + filePath + L"\n").c_str());
    return true;
}

void FencesWidget::ArrangeIcons(Fence* fence) {
    if (!fence || fence->icons.empty()) {
        fence->contentHeight = 0;
        return;
    }

    RECT clientRect;
    GetClientRect(fence->hwnd, &clientRect);

    // Calculate icon cell size (icon + text area + spacing)
    // Text needs at least 70px width for typical filenames
    const int textWidth = max(70, fence->iconSize + 20);
    const int iconCellWidth = max(fence->iconSize, textWidth) + fence->iconSpacing;
    const int iconCellHeight = fence->iconSize + 35 + fence->iconSpacing; // Icon + text + spacing

    const int startX = ICON_PADDING_LEFT;
    const int startY = TITLE_BAR_HEIGHT + ICON_PADDING_TOP;
    const int availableWidth = clientRect.right - ICON_PADDING_LEFT - ICON_PADDING_RIGHT;

    int iconsPerRow = max(1, availableWidth / iconCellWidth);
    int currentX = startX;
    int currentY = startY;
    int currentCol = 0;

    for (auto& icon : fence->icons) {
        // Center icon horizontally in its cell
        icon.position.x = currentX + (iconCellWidth - fence->iconSize) / 2;
        icon.position.y = currentY;

        currentCol++;
        if (currentCol >= iconsPerRow) {
            currentCol = 0;
            currentX = startX;
            currentY += iconCellHeight;
        } else {
            currentX += iconCellWidth;
        }
    }

    // Calculate total content height
    int rows = (int)fence->icons.size() / iconsPerRow;
    if (fence->icons.size() % iconsPerRow != 0) {
        rows++;
    }
    fence->contentHeight = startY + rows * iconCellHeight + ICON_PADDING_BOTTOM;
}

HICON FencesWidget::GetFileIcon(const std::wstring& filePath, int size) {
    // 嘗試使用 PrivateExtractIconsW 獲取精確尺寸的圖示
    HICON hIcon = nullptr;
    UINT extractResult = PrivateExtractIconsW(
        filePath.c_str(),
        0,          // icon index
        size, size, // desired size
        &hIcon,
        nullptr,
        1,          // number of icons
        LR_DEFAULTCOLOR
    );

    if (extractResult > 0 && hIcon) {
        return hIcon;
    }

    // 如果檔案沒有內建圖示，嘗試使用 SHGetFileInfo 獲取關聯的圖示
    SHFILEINFOW sfi = { 0 };
    DWORD_PTR result;

    // 使用 SHGFI_USEFILEATTRIBUTES 可以獲得捷徑的圖示
    UINT flags = SHGFI_ICON;

    // 根據大小選擇圖示類型
    if (size <= 16) {
        flags |= SHGFI_SMALLICON;
    } else if (size <= 32) {
        flags |= SHGFI_LARGEICON;
    } else {
        // 對於 64px，使用 SHIL_EXTRALARGE (48px) 並放大
        flags = SHGFI_SYSICONINDEX;
        result = SHGetFileInfoW(filePath.c_str(), 0, &sfi, sizeof(sfi), flags);

        if (result) {
            IImageList* imageList = nullptr;
            // 使用 EXTRALARGE 獲得較好的 48px 圖示
            HRESULT hr = SHGetImageList(SHIL_EXTRALARGE, IID_PPV_ARGS(&imageList));

            if (SUCCEEDED(hr) && imageList) {
                HICON hIcon48 = nullptr;
                hr = imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon48);
                imageList->Release();

                if (SUCCEEDED(hr) && hIcon48) {
                    // 使用高品質方式將 48px 放大到 64px
                    HDC hdcScreen = GetDC(nullptr);
                    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
                    HDC hdcDst = CreateCompatibleDC(hdcScreen);

                    HBITMAP hbmDst = CreateCompatibleBitmap(hdcScreen, size, size);
                    HBITMAP hbmOldDst = (HBITMAP)SelectObject(hdcDst, hbmDst);

                    // 設置高品質縮放模式
                    SetStretchBltMode(hdcDst, HALFTONE);
                    SetBrushOrgEx(hdcDst, 0, 0, nullptr);

                    // 繪製透明背景
                    BLENDFUNCTION blend = { 0 };
                    blend.BlendOp = AC_SRC_OVER;
                    blend.SourceConstantAlpha = 255;
                    blend.AlphaFormat = AC_SRC_ALPHA;

                    // 直接用 DrawIconEx 高品質繪製
                    DrawIconEx(hdcDst, 0, 0, hIcon48, size, size, 0, nullptr, DI_NORMAL);

                    // 創建新圖示
                    ICONINFO iconInfo = {0};
                    iconInfo.fIcon = TRUE;
                    iconInfo.hbmColor = hbmDst;
                    iconInfo.hbmMask = CreateBitmap(size, size, 1, 1, nullptr);

                    HICON hResizedIcon = CreateIconIndirect(&iconInfo);

                    DeleteObject(iconInfo.hbmMask);
                    SelectObject(hdcDst, hbmOldDst);
                    DeleteObject(hbmDst);
                    DeleteDC(hdcDst);
                    DeleteDC(hdcSrc);
                    ReleaseDC(nullptr, hdcScreen);
                    DestroyIcon(hIcon48);

                    if (hResizedIcon) {
                        return hResizedIcon;
                    }
                }
            }
        }

        // Fallback
        flags = SHGFI_ICON | SHGFI_LARGEICON;
    }

    result = SHGetFileInfoW(
        filePath.c_str(),
        0,
        &sfi,
        sizeof(sfi),
        flags
    );

    if (result && sfi.hIcon) {
        return sfi.hIcon;
    }

    return LoadIcon(nullptr, IDI_APPLICATION);
}

void FencesWidget::DrawIcon(HDC hdc, DesktopIcon& icon, int x, int y, int iconSize) {
    // Calculate text area width - ensure enough space to avoid overlap
    const int textWidth = max(70, iconSize + 20);
    const int textLeft = x - (textWidth - iconSize) / 2;
    const int textRight = textLeft + textWidth;

    // Draw selection background if selected
    if (icon.selected) {
        RECT selRect = { textLeft - 2, y - 2, textRight + 2, y + iconSize + 35 };
        HBRUSH selBrush = CreateSolidBrush(RGB(173, 216, 230));
        FillRect(hdc, &selRect, selBrush);
        DeleteObject(selBrush);
    }

    // 延遲載入：如果需要的大小尚未載入，則現在載入
    HICON* hIconCache = nullptr;
    if (iconSize == 32) {
        hIconCache = &icon.hIcon32;
    } else if (iconSize == 48) {
        hIconCache = &icon.hIcon48;
    } else if (iconSize == 64) {
        hIconCache = &icon.hIcon64;
    }

    if (hIconCache && !*hIconCache) {
        *hIconCache = GetFileIcon(icon.filePath, iconSize);
    }

    // 使用快取的圖示
    HICON hIconToUse = nullptr;
    if (iconSize == 32 && icon.hIcon32) {
        hIconToUse = icon.hIcon32;
    } else if (iconSize == 48 && icon.hIcon48) {
        hIconToUse = icon.hIcon48;
    } else if (iconSize == 64 && icon.hIcon64) {
        hIconToUse = icon.hIcon64;
    } else if (icon.hIcon) {
        hIconToUse = icon.hIcon;
    }

    // Draw icon (centered)
    if (hIconToUse) {
        DrawIconEx(hdc, x, y, hIconToUse, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    // Draw display name with proper width
    RECT textRect = { textLeft, y + iconSize + 2, textRight, y + iconSize + 40 };
    SetBkMode(hdc, TRANSPARENT);

    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微軟正黑體");

    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    // Draw text with shadow for better visibility
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT shadowRect = textRect;
    OffsetRect(&shadowRect, 1, 1);
    DrawTextW(hdc, icon.displayName.c_str(), -1, &shadowRect,
        DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

    SetTextColor(hdc, RGB(0, 0, 0));
    DrawTextW(hdc, icon.displayName.c_str(), -1, &textRect,
        DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

void FencesWidget::ShowFenceContextMenu(Fence* fence, int x, int y) {
    // 設定滑鼠游標為箭頭
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }

    AppendMenuW(hMenu, MF_STRING, IDM_RENAME_FENCE, L"重新命名柵欄");
    AppendMenuW(hMenu, MF_STRING, IDM_CHANGE_COLOR, L"變更背景顏色...");
    AppendMenuW(hMenu, MF_STRING, IDM_CHANGE_TITLE_COLOR, L"變更標題顏色...");
    AppendMenuW(hMenu, MF_STRING, IDM_CHANGE_TRANSPARENCY, L"調整透明度");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 圖示大小子選單
    HMENU hSizeMenu = CreatePopupMenu();
    AppendMenuW(hSizeMenu, MF_STRING | (fence->iconSize == 32 ? MF_CHECKED : 0), IDM_ICON_SIZE_32, L"小 (32px)");
    AppendMenuW(hSizeMenu, MF_STRING | (fence->iconSize == 48 ? MF_CHECKED : 0), IDM_ICON_SIZE_48, L"中 (48px)");
    AppendMenuW(hSizeMenu, MF_STRING | (fence->iconSize == 64 ? MF_CHECKED : 0), IDM_ICON_SIZE_64, L"大 (64px)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSizeMenu, L"圖示大小");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_AUTO_CATEGORIZE, L"自動分類桌面圖示");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_CREATE_FENCE, L"建立新柵欄");
    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_FENCE, L"刪除柵欄");

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, fence->hwnd, nullptr);
    DestroyMenu(hMenu);
}

HWND FencesWidget::GetDesktopListView() {
    if (desktopListView_) {
        return desktopListView_;
    }

    // Find the desktop window
    HWND hProgman = FindWindowW(L"Progman", nullptr);
    if (!hProgman) {
        return nullptr;
    }

    // Find the SHELLDLL_DefView window
    HWND hShellViewWin = FindWindowExW(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!hShellViewWin) {
        // Try WorkerW windows (for Windows 10+)
        HWND hWorkerW = nullptr;
        while ((hWorkerW = FindWindowExW(nullptr, hWorkerW, L"WorkerW", nullptr)) != nullptr) {
            hShellViewWin = FindWindowExW(hWorkerW, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hShellViewWin) {
                break;
            }
        }
    }

    if (!hShellViewWin) {
        return nullptr;
    }

    // Find the SysListView32 window (the actual desktop icon container)
    desktopListView_ = FindWindowExW(hShellViewWin, nullptr, L"SysListView32", nullptr);
    return desktopListView_;
}

POINT FencesWidget::GetDesktopIconPosition(int iconIndex) {
    HWND hListView = GetDesktopListView();
    if (!hListView || iconIndex < 0) {
        return { -1, -1 };
    }

    // Get the process ID of the desktop window
    DWORD processId = 0;
    GetWindowThreadProcessId(hListView, &processId);

    // Open the process with read/write access
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                                   FALSE, processId);
    if (!hProcess) {
        return { -1, -1 };
    }

    // Allocate memory for the POINT structure in remote process
    POINT* pRemotePos = (POINT*)VirtualAllocEx(hProcess, nullptr, sizeof(POINT),
                                                MEM_COMMIT, PAGE_READWRITE);
    if (!pRemotePos) {
        CloseHandle(hProcess);
        return { -1, -1 };
    }

    // Get icon position
    SendMessageW(hListView, LVM_GETITEMPOSITION, iconIndex, (LPARAM)pRemotePos);

    // Read back the position
    POINT localPos = { -1, -1 };
    ReadProcessMemory(hProcess, pRemotePos, &localPos, sizeof(POINT), nullptr);

    VirtualFreeEx(hProcess, pRemotePos, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return localPos;
}

int FencesWidget::FindDesktopIconIndex(const std::wstring& filePath) {
    HWND hListView = GetDesktopListView();
    if (!hListView) {
        return -1;
    }

    // Get the number of items in the list view
    int itemCount = (int)SendMessageW(hListView, LVM_GETITEMCOUNT, 0, 0);

    // Extract filename from full path
    size_t lastSlash = filePath.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash != std::wstring::npos)
        ? filePath.substr(lastSlash + 1)
        : filePath;

    // 對於捷徑檔案，也嘗試不帶副檔名的版本
    std::wstring fileNameWithoutExt = fileName;
    size_t lastDot = fileNameWithoutExt.find_last_of(L'.');
    if (lastDot != std::wstring::npos && lastDot > 0) {
        fileNameWithoutExt = fileNameWithoutExt.substr(0, lastDot);
    }

    // Get the process ID of the desktop window
    DWORD processId = 0;
    GetWindowThreadProcessId(hListView, &processId);

    // Open the process with read/write access
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                                   FALSE, processId);
    if (!hProcess) {
        return -1;
    }

    // Allocate memory in the remote process for the item text
    wchar_t* pRemoteBuffer = (wchar_t*)VirtualAllocEx(hProcess, nullptr, MAX_PATH * sizeof(wchar_t),
                                                       MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuffer) {
        CloseHandle(hProcess);
        return -1;
    }

    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.pszText = pRemoteBuffer;
    lvi.cchTextMax = MAX_PATH;

    // Allocate memory for the LVITEM structure
    LVITEMW* pRemoteLvi = (LVITEMW*)VirtualAllocEx(hProcess, nullptr, sizeof(LVITEMW),
                                                    MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteLvi) {
        VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return -1;
    }

    wchar_t localBuffer[MAX_PATH] = { 0 };

    // Iterate through all items and compare names
    for (int i = 0; i < itemCount; i++) {
        lvi.iItem = i;
        lvi.iSubItem = 0;

        // Write the LVITEM structure to remote process
        WriteProcessMemory(hProcess, pRemoteLvi, &lvi, sizeof(LVITEMW), nullptr);

        // Get the item text
        SendMessageW(hListView, LVM_GETITEMTEXTW, i, (LPARAM)pRemoteLvi);

        // Read the text back
        ReadProcessMemory(hProcess, pRemoteBuffer, localBuffer, MAX_PATH * sizeof(wchar_t), nullptr);

        // Compare with the file name (case-insensitive)
        // 嘗試完整檔名和不帶副檔名的檔名
        if (_wcsicmp(localBuffer, fileName.c_str()) == 0 ||
            _wcsicmp(localBuffer, fileNameWithoutExt.c_str()) == 0) {
            VirtualFreeEx(hProcess, pRemoteLvi, 0, MEM_RELEASE);
            VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return i;
        }
    }

    VirtualFreeEx(hProcess, pRemoteLvi, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, pRemoteBuffer, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return -1;
}

// ============================================================================
// DEPRECATED METHODS REMOVED IN v3.0
// The following methods have been replaced by Custom Draw rendering suppression:
// - HideDesktopIcon() → managedIconPaths_.insert()
// - ShowDesktopIcon() → managedIconPaths_.erase()
// - ShowDesktopIconAtPosition() → managedIconPaths_.erase()
// - HideDesktopIconsBatch() → batch insert to managedIconPaths_
// - ShowDesktopIconsBatch() → batch erase from managedIconPaths_
// - RestoreDesktopIconsBatch() → managedIconPaths_.clear()
// ============================================================================

int FencesWidget::FindIconAtPosition(Fence* fence, int x, int y) {
    if (!fence) {
        return -1;
    }

    for (size_t i = 0; i < fence->icons.size(); i++) {
        const auto& icon = fence->icons[i];

        // Apply scroll offset to icon position
        int adjustedY = icon.position.y - fence->scrollOffset;

        // Calculate icon bounding box
        RECT iconRect = {
            icon.position.x - 5,
            adjustedY - 5,
            icon.position.x + fence->iconSize + 15,
            adjustedY + fence->iconSize + 35
        };

        // Check if click is within icon bounds
        if (x >= iconRect.left && x <= iconRect.right &&
            y >= iconRect.top && y <= iconRect.bottom) {
            return (int)i;
        }
    }

    return -1;
}

void FencesWidget::ShowIconContextMenu(Fence* fence, int iconIndex, int x, int y) {
    if (!fence || iconIndex < 0 || iconIndex >= (int)fence->icons.size()) {
        return;
    }

    // 設定滑鼠游標為箭頭
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    // 儲存選取的圖示供命令處理器使用
    selectedFence_ = fence;
    selectedIconIndex_ = iconIndex;

    const std::wstring& filePath = fence->icons[iconIndex].filePath;

    // 初始化 COM
    HRESULT hr = CoInitialize(nullptr);
    bool comInitialized = SUCCEEDED(hr);

    // 獲取檔案的父資料夾和檔案名稱
    wchar_t folderPath[MAX_PATH] = { 0 };
    wchar_t fileName[MAX_PATH] = { 0 };
    wcscpy_s(folderPath, filePath.c_str());
    PathRemoveFileSpecW(folderPath);
    wcscpy_s(fileName, PathFindFileNameW(filePath.c_str()));

    IShellFolder* pDesktopFolder = nullptr;
    IShellFolder* pParentFolder = nullptr;
    IContextMenu* pContextMenu = nullptr;
    LPITEMIDLIST pidlParent = nullptr;
    LPITEMIDLIST pidlItem = nullptr;

    do {
        // 獲取桌面 Shell Folder
        hr = SHGetDesktopFolder(&pDesktopFolder);
        if (FAILED(hr)) break;

        // 獲取父資料夾的 PIDL
        hr = pDesktopFolder->ParseDisplayName(nullptr, nullptr, folderPath, nullptr, &pidlParent, nullptr);
        if (FAILED(hr)) break;

        // 綁定到父資料夾
        hr = pDesktopFolder->BindToObject(pidlParent, nullptr, IID_IShellFolder, (void**)&pParentFolder);
        if (FAILED(hr)) break;

        // 獲取檔案的 PIDL
        hr = pParentFolder->ParseDisplayName(nullptr, nullptr, fileName, nullptr, &pidlItem, nullptr);
        if (FAILED(hr)) break;

        // 獲取檔案的 Context Menu
        hr = pParentFolder->GetUIObjectOf(fence->hwnd, 1, (LPCITEMIDLIST*)&pidlItem,
                                          IID_IContextMenu, nullptr, (void**)&pContextMenu);
        if (FAILED(hr)) break;

        // 創建彈出選單
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) break;

        // 查詢 Context Menu
        hr = pContextMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
        if (FAILED(hr)) {
            DestroyMenu(hMenu);
            break;
        }

        // 添加自訂選項：從柵欄移除
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_REMOVE_ICON, L"從柵欄移除");

        // 顯示選單
        UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, fence->hwnd, nullptr);

        if (cmd > 0) {
            if (cmd == IDM_REMOVE_ICON) {
                // 處理我們自訂的命令
                RemoveIconFromFence(selectedFence_, selectedIconIndex_);
                selectedIconIndex_ = -1;
                selectedFence_ = nullptr;
            } else {
                // 執行 Shell Context Menu 的命令
                CMINVOKECOMMANDINFO ici = { 0 };
                ici.cbSize = sizeof(CMINVOKECOMMANDINFO);
                ici.fMask = 0;
                ici.hwnd = fence->hwnd;
                ici.lpVerb = MAKEINTRESOURCEA(cmd - 1);
                ici.nShow = SW_SHOWNORMAL;
                pContextMenu->InvokeCommand(&ici);
            }
        }

        DestroyMenu(hMenu);

    } while (false);

    // 清理資源
    if (pContextMenu) pContextMenu->Release();
    if (pidlItem) CoTaskMemFree(pidlItem);
    if (pParentFolder) pParentFolder->Release();
    if (pidlParent) CoTaskMemFree(pidlParent);
    if (pDesktopFolder) pDesktopFolder->Release();

    if (comInitialized) {
        CoUninitialize();
    }
}

// ============================================================================
// Phase 1: ListView Subclassing & Custom Draw Implementation (NEW in v3.0)
// ============================================================================

bool FencesWidget::SetupDesktopSubclass() {
    // Find desktop ListView window
    desktopListView_ = FindDesktopListView();
    if (!desktopListView_) {
        OutputDebugStringW(L"[FencesWidget] Failed to find desktop ListView\n");
        return false;
    }

    // =========================================================================
    // FIX: Get parent window (SHELLDLL_DefView) to receive WM_NOTIFY
    // Custom Draw notifications are sent to the parent, not the ListView itself
    // =========================================================================
    desktopShellView_ = GetParent(desktopListView_);
    if (!desktopShellView_) {
        OutputDebugStringW(L"[FencesWidget] Failed to get ShellView parent\n");
        return false;
    }

    // Debug output
    wchar_t debugMsg[256];
    swprintf_s(debugMsg, L"[FencesWidget] ListView=0x%p, ShellView=0x%p\n",
               desktopListView_, desktopShellView_);
    OutputDebugStringW(debugMsg);

    // Subclass the ShellView parent (NOT the ListView)
    BOOL result = SetWindowSubclass(
        desktopShellView_,  // ← Changed: subclass parent, not ListView
        ListViewSubclassProc,
        DESKTOP_SUBCLASS_ID,
        reinterpret_cast<DWORD_PTR>(this)
    );

    if (result) {
        OutputDebugStringW(L"[FencesWidget] Successfully subclassed ShellView parent\n");
        return true;
    }

    // SetWindowSubclass failed - try alternative method
    DWORD lastError = GetLastError();
    wchar_t errorMsg[256];
    swprintf_s(errorMsg, L"[FencesWidget] SetWindowSubclass failed with error: %d\n", lastError);
    OutputDebugStringW(errorMsg);

    // BACKUP PLAN: Use SetWindowLongPtr instead
    OutputDebugStringW(L"[FencesWidget] Trying backup method: SetWindowLongPtr...\n");

    // Store original WndProc
    LONG_PTR originalProc = SetWindowLongPtrW(desktopShellView_, GWLP_WNDPROC,
                                               reinterpret_cast<LONG_PTR>(ListViewSubclassProcLegacy));

    if (originalProc == 0) {
        DWORD error = GetLastError();
        swprintf_s(errorMsg, L"[FencesWidget] SetWindowLongPtr also failed with error: %d\n", error);
        OutputDebugStringW(errorMsg);
        return false;
    }

    // Store original proc and instance pointer in window data
    SetWindowLongPtrW(desktopShellView_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    originalShellViewProc_ = reinterpret_cast<WNDPROC>(originalProc);

    OutputDebugStringW(L"[FencesWidget] Successfully subclassed using SetWindowLongPtr (legacy method)\n");
    return true;
}

void FencesWidget::RemoveDesktopSubclass() {
    if (desktopShellView_) {
        if (originalShellViewProc_) {
            // Legacy method: restore original WndProc
            SetWindowLongPtrW(desktopShellView_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalShellViewProc_));
            SetWindowLongPtrW(desktopShellView_, GWLP_USERDATA, 0);
            originalShellViewProc_ = nullptr;
            OutputDebugStringW(L"[FencesWidget] Removed legacy ShellView subclass\n");
        } else {
            // Modern method: remove SetWindowSubclass
            RemoveWindowSubclass(desktopShellView_, ListViewSubclassProc, DESKTOP_SUBCLASS_ID);
            OutputDebugStringW(L"[FencesWidget] Removed ShellView subclass\n");
        }
        desktopShellView_ = nullptr;
    }
    desktopListView_ = nullptr;
}

HWND FencesWidget::FindDesktopListView() {
    // Method 1: Try Progman window
    HWND hwndProgman = FindWindowW(L"Progman", nullptr);
    if (hwndProgman) {
        HWND hwndShellView = FindWindowExW(hwndProgman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (hwndShellView) {
            HWND hwndListView = FindWindowExW(hwndShellView, nullptr, L"SysListView32", nullptr);
            if (hwndListView) {
                return hwndListView;
            }
        }
    }

    // Method 2: Try WorkerW windows (Windows 10/11)
    HWND hwndShellView = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassNameW(hwnd, className, 256);

        if (wcscmp(className, L"WorkerW") == 0) {
            HWND hwndDefView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hwndDefView) {
                *reinterpret_cast<HWND*>(lParam) = hwndDefView;
                return FALSE;  // Stop enumeration
            }
        }
        return TRUE;  // Continue enumeration
    }, reinterpret_cast<LPARAM>(&hwndShellView));

    if (hwndShellView) {
        HWND hwndListView = FindWindowExW(hwndShellView, nullptr, L"SysListView32", nullptr);
        if (hwndListView) {
            return hwndListView;
        }
    }

    return nullptr;
}

// Legacy subclass proc (using SetWindowLongPtr instead of SetWindowSubclass)
LRESULT CALLBACK FencesWidget::ListViewSubclassProcLegacy(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Get instance pointer from GWLP_USERDATA
    FencesWidget* pThis = reinterpret_cast<FencesWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (pThis) {
        // Debug: Log first WM_NOTIFY to confirm subclass is working
        static bool loggedFirstNotify = false;
        if (msg == WM_NOTIFY && !loggedFirstNotify) {
            OutputDebugStringW(L"[Subclass:Legacy] First WM_NOTIFY received - subclass is working!\n");
            loggedFirstNotify = true;
        }

        // Intercept WM_NOTIFY for Custom Draw
        if (msg == WM_NOTIFY) {
            NMHDR* pNMHDR = reinterpret_cast<NMHDR*>(lParam);

            // Debug: Log notification details
            static int notifyCount = 0;
            if (notifyCount < 5) {
                wchar_t debugMsg[256];
                swprintf_s(debugMsg, L"[Subclass:Legacy] WM_NOTIFY code=%d, hwndFrom=0x%p (expected 0x%p)\n",
                           pNMHDR->code, pNMHDR->hwndFrom, pThis->desktopListView_);
                OutputDebugStringW(debugMsg);
                notifyCount++;
            }

            if (pNMHDR->hwndFrom == pThis->desktopListView_ &&
                pNMHDR->code == NM_CUSTOMDRAW) {

                NMLVCUSTOMDRAW* pCD = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);

                // Debug: First time Custom Draw notification
                static bool firstTime = true;
                if (firstTime) {
                    OutputDebugStringW(L"[Subclass:Legacy] ✓✓✓ Custom Draw notification MATCHED! ✓✓✓\n");
                    firstTime = false;
                }

                return pThis->OnCustomDraw(pCD);
            }
        }
    }

    // Call original WndProc
    if (pThis && pThis->originalShellViewProc_) {
        return CallWindowProcW(pThis->originalShellViewProc_, hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FencesWidget::ListViewSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    FencesWidget* pThis = reinterpret_cast<FencesWidget*>(dwRefData);

    // Debug: Log first WM_NOTIFY to confirm subclass is working
    static bool loggedFirstNotify = false;
    if (msg == WM_NOTIFY && !loggedFirstNotify) {
        OutputDebugStringW(L"[Subclass] First WM_NOTIFY received - subclass is working!\n");
        loggedFirstNotify = true;
    }

    // Intercept WM_NOTIFY for Custom Draw
    if (msg == WM_NOTIFY) {
        NMHDR* pNMHDR = reinterpret_cast<NMHDR*>(lParam);

        // Debug: Log notification details
        static int notifyCount = 0;
        if (notifyCount < 5) {
            wchar_t debugMsg[256];
            swprintf_s(debugMsg, L"[Subclass] WM_NOTIFY code=%d, hwndFrom=0x%p (expected 0x%p)\n",
                       pNMHDR->code, pNMHDR->hwndFrom, pThis->desktopListView_);
            OutputDebugStringW(debugMsg);
            notifyCount++;
        }

        // =====================================================================
        // FIX: Verify notification is from our ListView
        // Parent windows can receive notifications from multiple children
        // =====================================================================
        if (pNMHDR->hwndFrom == pThis->desktopListView_ &&
            pNMHDR->code == NM_CUSTOMDRAW) {

            NMLVCUSTOMDRAW* pCD = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);

            // Debug: First time Custom Draw notification
            static bool firstTime = true;
            if (firstTime) {
                OutputDebugStringW(L"[Subclass] ✓✓✓ Custom Draw notification MATCHED! ✓✓✓\n");
                firstTime = false;
            }

            return pThis->OnCustomDraw(pCD);
        }
    }

    // Call default subclass procedure for other messages
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT FencesWidget::OnCustomDraw(NMLVCUSTOMDRAW* pCD) {
    // Request notification for each item
    if (pCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
        static int prepaintCount = 0;
        if (prepaintCount < 3) {
            OutputDebugStringW(L"[CustomDraw] CDDS_PREPAINT - requesting item notifications\n");
            prepaintCount++;
        }
        return CDRF_NOTIFYITEMDRAW;
    }

    // Check each item before drawing
    if (pCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        int itemIndex = static_cast<int>(pCD->nmcd.dwItemSpec);

        // Get the full path of this desktop item
        std::wstring itemPath = GetDesktopItemPath(itemIndex);

        // Debug output for EVERY item (temporarily)
        static int debugCount = 0;
        if (debugCount < 20) {  // Only first 20 items to avoid spam
            wchar_t msg[512];
            swprintf_s(msg, L"[CustomDraw] Item %d: %s (managed=%d)\n",
                       itemIndex,
                       itemPath.empty() ? L"<empty>" : itemPath.c_str(),
                       IsIconManagedByFence(itemPath));
            OutputDebugStringW(msg);
            debugCount++;
        }

        // If this icon is managed by a fence, skip default drawing
        if (!itemPath.empty() && IsIconManagedByFence(itemPath)) {
            // Debug output for first skip
            static int skipCount = 0;
            if (skipCount < 3) {
                OutputDebugStringW((L"[FencesWidget] ✓ SKIP rendering: " + itemPath + L"\n").c_str());
                skipCount++;
            }

            // This is the key: tell Explorer NOT to draw this icon
            return CDRF_SKIPDEFAULT;
        }
    }

    // Let Explorer draw other icons normally
    return CDRF_DODEFAULT;
}

std::wstring FencesWidget::GetDesktopItemPath(int itemIndex) {
    if (!desktopListView_ || itemIndex < 0) {
        return L"";
    }

    // Get item text (filename)
    wchar_t buffer[MAX_PATH] = {0};
    LVITEMW item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = itemIndex;
    item.pszText = buffer;
    item.cchTextMax = MAX_PATH;

    if (!SendMessageW(desktopListView_, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) {
        return L"";
    }

    // Get desktop path
    wchar_t desktopPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPath))) {
        return L"";
    }

    // Combine to full path
    std::wstring fullPath = std::wstring(desktopPath) + L"\\" + buffer;

    // Check if file exists (might be from Public Desktop)
    if (GetFileAttributesW(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Try Public Desktop
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, desktopPath))) {
            fullPath = std::wstring(desktopPath) + L"\\" + buffer;
        }
    }

    return fullPath;
}

bool FencesWidget::IsIconManagedByFence(const std::wstring& path) {
    // O(1) lookup in unordered_set
    return managedIconPaths_.find(path) != managedIconPaths_.end();
}

// ============================================================================
// Shell Notification Handlers (NEW in v3.0)
// ============================================================================

void FencesWidget::OnDesktopItemCreated(const std::wstring& path) {
    // Check if this file should be auto-categorized
    std::wstring category = GetFileCategory(path);

    // Find fence with matching title
    Fence* targetFence = nullptr;
    for (auto& fence : fences_) {
        if (fence.title == category) {
            targetFence = &fence;
            break;
        }
    }

    // If auto-categorization is enabled and fence exists, add icon
    if (targetFence) {
        AddIconToFence(targetFence, path);
        OutputDebugStringW((L"[FencesWidget] Auto-categorized: " + path + L"\n").c_str());
    }
}

void FencesWidget::OnDesktopItemDeleted(const std::wstring& path) {
    // Remove from all fences
    for (auto& fence : fences_) {
        auto it = std::find_if(fence.icons.begin(), fence.icons.end(),
            [&](const DesktopIcon& icon) { return icon.filePath == path; });

        if (it != fence.icons.end()) {
            // Clean up icon handles
            if (it->hIcon32) DestroyIcon(it->hIcon32);
            if (it->hIcon48) DestroyIcon(it->hIcon48);
            if (it->hIcon64) DestroyIcon(it->hIcon64);
            if (it->hIcon) DestroyIcon(it->hIcon);

            fence.icons.erase(it);
            managedIconPaths_.erase(path);

            // Rearrange and redraw
            ArrangeIcons(&fence);
            InvalidateRect(fence.hwnd, nullptr, TRUE);

            OutputDebugStringW((L"[FencesWidget] Removed deleted icon: " + path + L"\n").c_str());
            break;
        }
    }
}

void FencesWidget::OnDesktopItemRenamed(const std::wstring& oldPath, const std::wstring& newPath) {
    // Update in all fences
    for (auto& fence : fences_) {
        auto it = std::find_if(fence.icons.begin(), fence.icons.end(),
            [&](const DesktopIcon& icon) { return icon.filePath == oldPath; });

        if (it != fence.icons.end()) {
            // Update file path
            it->filePath = newPath;

            // Update display name
            size_t lastSlash = newPath.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                it->displayName = newPath.substr(lastSlash + 1);
            } else {
                it->displayName = newPath;
            }

            // Remove extension
            size_t lastDot = it->displayName.find_last_of(L'.');
            if (lastDot != std::wstring::npos && lastDot > 0) {
                it->displayName = it->displayName.substr(0, lastDot);
            }

            // Update managed paths
            managedIconPaths_.erase(oldPath);
            managedIconPaths_.insert(newPath);

            // Redraw fence
            InvalidateRect(fence.hwnd, nullptr, TRUE);

            OutputDebugStringW((L"[FencesWidget] Renamed: " + oldPath + L" -> " + newPath + L"\n").c_str());
            break;
        }
    }
}

// ==================== DLL 導出函式 ====================

extern "C" {
    WIDGET_API IWidget* CreateWidget(void* params) {
        (void)params;  // FencesWidget 不需要 HINSTANCE 參數
        return new FencesWidget();
    }

    WIDGET_API void DestroyWidget(IWidget* widget) {
        delete widget;
    }

    WIDGET_API const wchar_t* GetWidgetName() {
        return L"FencesWidget";
    }

    WIDGET_API const wchar_t* GetWidgetVersion() {
        return L"1.0.0";
    }

    WIDGET_API void ExecuteCommand(IWidget* widget, int commandId) {
        if (!widget) return;

        FencesWidget* fencesWidget = dynamic_cast<FencesWidget*>(widget);
        if (!fencesWidget) return;

        switch (commandId) {
            case WIDGET_CMD_CREATE_NEW:
                // 建立新柵欄（在螢幕中央）
                fencesWidget->CreateFence(100, 100, 300, 400, L"新柵欄");
                break;

            case WIDGET_CMD_CLEAR_ALL_DATA:
                // 清除所有資料
                fencesWidget->ClearAllData();
                break;

            default:
                break;
        }
    }
}
