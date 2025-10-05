# FencesWidget 開發文檔

## 專案概述

FencesWidget 是一個 Windows 桌面小工具管理器，採用 **DLL 插件架構**，提供類似 Stardock Fences 的桌面圖示分類和管理功能。允許使用者創建半透明的柵欄容器來組織桌面圖示。

**最新版本特性：**
- 插件化架構：每個 Widget 獨立編譯為 DLL
- 動態載入：主程序掃描目錄自動載入插件
- 系統托盤控制：帶子選單的 Widget 管理介面
- 狀態持久化：Widget 啟用/停用狀態記憶
- 自定義命令：插件可導出自定義功能

## 專案架構

### 目錄結構
```
Fences_claude/
├── src/
│   ├── main.cpp                    # 應用程式入口點，系統托盤管理，插件載入器
│   ├── core/
│   │   ├── IWidget.h               # Widget 介面定義
│   │   ├── WidgetManager.h/cpp     # Widget 管理器
│   │   ├── WidgetExport.h          # DLL 導出宏和函數簽名
│   │   └── PluginLoader.h/cpp      # 插件動態載入器
│   └── widgets/
│       ├── FencesWidget.h          # FencesWidget 介面定義
│       ├── FencesWidget.cpp        # FencesWidget 實作 (編譯為 DLL)
│       ├── StickyNotesWidget.h     # 便簽 Widget 介面
│       └── StickyNotesWidget.cpp   # 便簽 Widget 實作 (編譯為 DLL)
├── build/                          # CMake 建置目錄
│   └── bin/Release/
│       ├── DesktopWidgetManager.exe    # 主程序 (~52 KB)
│       ├── FencesWidget.dll            # FencesWidget 插件 (~80 KB)
│       └── StickyNotesWidget.dll       # 便簽插件 (~63 KB)
├── %APPDATA%/FencesWidget/         # 配置文件目錄
│   ├── config.json                 # FencesWidget 柵欄配置
│   └── widget_states.conf          # Widget 啟用狀態記錄
└── FencesWidget.md                 # 本文檔
```

### 核心類別關係（插件架構）

```
IWidget (介面)
    ↑ 實作
    ├── FencesWidget (DLL 插件)
    │       ↓ 包含
    │   Fence (結構) - 代表單一柵欄
    │       ↓ 包含
    │   DesktopIcon (結構) - 代表桌面圖示
    │
    └── StickyNotesWidget (DLL 插件)
            ↓ 包含
        StickyNote (結構) - 代表單一便簽

WidgetManager
    ↓ 管理
多個 IWidget 實例 (動態載入自 DLL)

PluginLoader
    ↓ 負責
掃描、載入、卸載 DLL 插件
```

## 插件系統架構

### 1. DLL 導出介面 (WidgetExport.h)

每個 Widget DLL 必須導出以下函數：

```cpp
extern "C" {
    // 必需導出函數
    WIDGET_API IWidget* CreateWidget(void* params);
    WIDGET_API void DestroyWidget(IWidget* widget);
    WIDGET_API const wchar_t* GetWidgetName();
    WIDGET_API const wchar_t* GetWidgetVersion();

    // 可選導出函數（支持自定義命令）
    WIDGET_API void ExecuteCommand(IWidget* widget, int commandId);
}
```

**自定義命令 ID：**
```cpp
#define WIDGET_CMD_CREATE_NEW       1001    // 創建新項目
#define WIDGET_CMD_CLEAR_ALL_DATA   1002    // 清除所有資料
```

### 2. 插件載入流程 (PluginLoader)

```cpp
// 1. 掃描插件目錄
std::vector<PluginInfo> plugins = PluginLoader::ScanPlugins(exeDir);

// 2. 為每個插件創建實例
for (auto& plugin : plugins) {
    auto widgetInstance = PluginLoader::CreateWidgetInstance(plugin, &hInstance);
    if (widgetInstance) {
        manager.RegisterWidget(widgetInstance);
    }
}

// 3. 載入保存的狀態
LoadWidgetStates(manager);

// 4. 啟用 Widget
manager.EnableWidget(pluginName);
```

### 3. 系統托盤選單結構

```
[DesktopWidgetManager 托盤圖示]
    └── 右鍵選單
        ├── FencesWidget ▶
        │   ├── ✓ 啟用/停用
        │   ├── ────────────
        │   ├── 建立新柵欄
        │   └── 清除所有記錄
        │
        ├── StickyNotesWidget ▶
        │   └── ✓ 啟用/停用
        │
        ├── ────────────
        ├── ✓ 開機自動啟動
        ├── ────────────
        └── 退出
```

## 資料結構

### 1. DesktopIcon 結構
```cpp
struct DesktopIcon {
    std::wstring filePath;           // 檔案完整路徑
    std::wstring fileName;           // 顯示名稱
    POINT originalDesktopPos;        // 原始桌面位置
    POINT position;                  // 柵欄內位置
    HICON hIcon;                     // 圖示句柄（當前尺寸）
    HICON hIcon32;                   // 32px 快取圖示
    HICON hIcon48;                   // 48px 快取圖示
    HICON hIcon64;                   // 64px 快取圖示
};
```

**設計考量：**
- 使用多尺寸快取避免重複載入圖示（效能優化）
- 延遲載入：只在需要時載入對應尺寸的圖示
- 記錄原始位置以便還原到桌面

### 2. Fence 結構
```cpp
struct Fence {
    HWND hwnd;                        // 柵欄視窗句柄
    std::wstring title;               // 柵欄標題
    std::vector<DesktopIcon> icons;   // 柵欄內的圖示
    COLORREF backgroundColor;         // 背景顏色
    COLORREF borderColor;             // 邊框顏色
    COLORREF titleColor;              // 標題文字顏色
    int alpha;                        // 透明度 (0-255)
    int iconSize;                     // 圖示大小 (32/48/64)
    int iconSpacing;                  // 圖示間距
    bool isCollapsed;                 // 是否收合
    bool isPinned;                    // 是否釘住（不自動隱藏）
    int expandedHeight;               // 展開時高度
    int scrollOffset;                 // 滾動偏移
    int contentHeight;                // 內容總高度
};
```

**設計考量：**
- 每個柵欄都是獨立的分層視窗（WS_EX_LAYERED）
- 支援完全自訂外觀（顏色、透明度）
- 收合狀態只顯示標題列
- 支援滾動以容納大量圖示

### 3. PluginInfo 結構
```cpp
struct PluginInfo {
    std::wstring dllPath;             // DLL 完整路徑
    std::wstring name;                // Widget 名稱
    std::wstring version;             // 版本號
    HMODULE hModule;                  // DLL 模組句柄
    CreateWidgetFunc createFunc;      // 創建函數指針
    DestroyWidgetFunc destroyFunc;    // 銷毀函數指針
    ExecuteCommandFunc executeCommandFunc;  // 自定義命令函數指針
    std::shared_ptr<IWidget> widgetInstance;  // Widget 實例
};
```

## 核心功能模組

### 1. 視窗管理

#### 柵欄視窗創建
- **函式：** `CreateFence(x, y, width, height, title)`
- **視窗樣式：** `WS_POPUP | WS_VISIBLE | WS_EX_LAYERED | WS_EX_TOPMOST`
- **特性：**
  - 無邊框彈出視窗
  - 支援分層透明效果
  - 永遠置頂
  - 接受拖放操作（DragAcceptFiles）

#### 視窗類別註冊
- **類別名稱：** `DesktopFenceWidget`
- **預設游標：** `IDC_ARROW`
- **視窗過程：** `WindowProc` (靜態) → `HandleMessage` (成員函式)

### 2. Widget 生命週期管理

```cpp
// 啟動流程
bool FencesWidget::Start() {
    // 1. 檢查 fences_ 是否為空
    if (fences_.empty()) {
        // 首次啟動：載入配置
        LoadConfiguration(configPath);
        // 沒有配置則創建示範柵欄
        if (fences_.empty()) {
            CreateFence(100, 100, 300, 400, L"桌面柵欄 1");
        }
    }

    // 2. 顯示所有柵欄窗口
    for (auto& fence : fences_) {
        ShowWindow(fence.hwnd, SW_SHOW);
    }

    // 3. 重新隱藏應該在柵欄中的桌面圖示
    for (auto& fence : fences_) {
        HideDesktopIconsBatch(iconPaths);
    }

    running_ = true;
}

// 停止流程
void FencesWidget::Stop() {
    // 1. 恢復所有桌面圖示
    RestoreAllDesktopIcons();

    // 2. 隱藏柵欄窗口（不銷毀，保留數據）
    for (auto& fence : fences_) {
        ShowWindow(fence.hwnd, SW_HIDE);
    }

    running_ = false;
}
```

**關鍵設計：**
- `Stop()` 不清空 `fences_` 列表，只隱藏窗口
- `Start()` 檢查 `fences_` 是否為空，避免重複載入配置
- 狀態記憶：停用再啟用時恢復之前的柵欄狀態

### 3. 訊息處理 (HandleMessage)

主要處理的 Windows 訊息：

| 訊息 | 處理函式 | 說明 |
|------|---------|------|
| WM_PAINT | PaintFence | 繪製柵欄內容 |
| WM_LBUTTONDOWN | - | 處理圖示/標題拖曳開始 |
| WM_MOUSEMOVE | - | 處理拖曳中 |
| WM_LBUTTONUP | - | 處理拖曳結束 |
| WM_LBUTTONDBLCLK | - | 雙擊開啟檔案 |
| WM_RBUTTONDOWN | - | 顯示右鍵選單 |
| WM_DROPFILES | - | 處理檔案拖放 |
| WM_SETCURSOR | - | 設定游標樣式（箭頭而非十字） |
| WM_COMMAND | - | 處理選單命令 |
| WM_MOUSEWHEEL | - | 處理滾輪滾動 |

### 4. 繪製系統 (PaintFence)

**雙緩衝繪製流程：**
```
1. 創建記憶體 DC (CreateCompatibleDC)
2. 創建記憶體位圖 (CreateCompatibleBitmap)
3. 繪製背景（純色填充）
4. 繪製標題列
   - 背景色填充
   - 標題文字 (自訂顏色)
   - 收合/釘住圖示
5. 繪製圖示（如果未收合）
   - 計算網格佈局
   - 繪製圖示和文字標籤
   - 處理選取高亮
6. 繪製邊框
7. 更新分層視窗 (UpdateLayeredWindow)
```

**關鍵常數：**
```cpp
const int TITLE_BAR_HEIGHT = 35;           // 標題列高度
const int ICON_PADDING_LEFT = 15;          // 左側內邊距
const int ICON_PADDING_RIGHT = 15;         // 右側內邊距
const int ICON_PADDING_TOP = 15;           // 上方內邊距
const int ICON_PADDING_BOTTOM = 15;        // 下方內邊距
```

### 5. 圖示管理

#### 新增圖示到柵欄
```cpp
bool AddIconToFence(Fence* fence, const std::wstring& filePath)
```
- 獲取檔案名稱和原始桌面位置
- 延遲載入當前尺寸的圖示
- 加入 fence->icons
- 重新排列圖示
- 重繪柵欄

#### 從柵欄移除圖示
```cpp
bool RemoveIconFromFence(Fence* fence, int iconIndex)
```
- 批次還原圖示到桌面（使用原始位置）
- 清理快取的圖示資源
- 從 vector 移除
- 重新排列圖示

#### 批次操作（效能優化）
```cpp
void HideDesktopIconsBatch(const std::vector<std::wstring>& filePaths)
void ShowDesktopIconsBatch(const std::vector<std::wstring>& filePaths)
void RestoreDesktopIconsBatch(const std::vector<std::pair<std::wstring, POINT>>& iconData)
```

**批次優化原理：**
```cpp
SendMessageW(hListView, WM_SETREDRAW, FALSE, 0);  // 停止重繪
// ... 批次處理所有圖示
SendMessageW(hListView, WM_SETREDRAW, TRUE, 0);   // 恢復重繪
InvalidateRect(hListView, nullptr, TRUE);         // 一次性刷新
```
對比：100 個圖示從 100 次重繪降至 1 次

### 6. 互動功能

#### 拖曳系統
**三種拖曳模式：**
1. **標題列拖曳** - 移動整個柵欄
2. **圖示拖曳** - 在柵欄間移動圖示
3. **調整大小** - 右下角拖曳調整尺寸

**拖曳狀態管理：**
```cpp
bool isDragging;              // 正在拖曳柵欄
bool isResizing;              // 正在調整大小
bool isDraggingIcon;          // 正在拖曳圖示
int draggedIconIndex;         // 被拖曳的圖示索引
POINT dragStartPos;           // 拖曳起始位置
```

#### 右鍵選單

**柵欄選單 (ShowFenceContextMenu):**
- 重新命名柵欄
- 變更背景顏色
- 變更標題顏色
- 調整透明度
- 圖示大小（小32px / 中48px / 大64px）
- 自動分類桌面圖示 ⭐ **新增**
- 建立新柵欄
- 刪除柵欄

**圖示選單 (ShowIconContextMenu):**
- 開啟
- 從柵欄移除

### 7. 自動分類系統

#### 檔案分類邏輯
```cpp
std::wstring GetFileCategory(const std::wstring& filePath)
```

**支援的類別：**
| 類別 | 副檔名 |
|------|--------|
| 文件 | .txt, .doc, .docx, .pdf, .xls, .xlsx, .ppt, .pptx |
| 圖片 | .jpg, .jpeg, .png, .gif, .bmp, .svg, .ico, .webp |
| 影片 | .mp4, .avi, .mkv, .mov, .wmv, .flv |
| 音樂 | .mp3, .wav, .flac, .aac, .ogg, .wma |
| 壓縮檔 | .zip, .rar, .7z, .tar, .gz |
| 應用程式 | .exe, .msi, .bat, .cmd, .lnk |
| 程式碼 | .cpp, .h, .c, .java, .py, .js, .html, .css, .ts |
| 資料夾 | (目錄) |
| 其他 | (其他所有類型) |

#### 自動分類流程
```cpp
void AutoCategorizeDesktopIcons()
```
1. 掃描桌面所有檔案 (FindFirstFileW/FindNextFileW)
2. 過濾已在柵欄中的檔案
3. 依副檔名分類到 map
4. 為每個類別創建或重用柵欄
5. 批次隱藏桌面圖示
6. 自動儲存配置

### 8. 配置持久化

#### 儲存格式 (JSON-like)
```json
{
  "fences": [
    {
      "title": "文件",
      "x": 100,
      "y": 100,
      "width": 300,
      "height": 400,
      "backgroundColor": 15790320,
      "borderColor": 6579300,
      "titleColor": 3289650,
      "alpha": 230,
      "iconSize": 48,
      "iconSpacing": 10,
      "isCollapsed": false,
      "isPinned": false,
      "expandedHeight": 400,
      "icons": [
        {
          "filePath": "C:\\Users\\...\\document.pdf",
          "originalX": 50,
          "originalY": 50
        }
      ]
    }
  ]
}
```

#### 配置檔案位置
```cpp
std::wstring configPath = %APPDATA%\FencesWidget\config.json
std::wstring statesPath = %APPDATA%\FencesWidget\widget_states.conf
```

**自動儲存時機：**
- 創建/刪除柵欄 ⭐ **現在會同步刪除配置**
- 移動/調整柵欄大小
- 修改柵欄屬性
- 新增/移除圖示
- 自動分類完成

**Widget 狀態記憶：** ⭐ **新增**
```
FencesWidget=1
StickyNotesWidget=0
```

### 9. 清除所有資料功能 ⭐ **新增**

```cpp
void ClearAllData()
```

**執行步驟：**
1. 確認對話框（雙重確認）
2. 恢復所有桌面圖示
3. 銷毀所有柵欄窗口
4. 清空 `fences_` 列表
5. 刪除配置文件
6. 刪除配置目錄（如果為空）

**使用方式：**
系統托盤 → FencesWidget → 清除所有記錄

## 關鍵設計模式

### 1. 延遲載入 (Lazy Loading)
**問題：** 載入所有尺寸圖示浪費記憶體和時間
**解決方案：**
```cpp
// 只載入當前需要的尺寸
HICON GetFileIcon(const std::wstring& filePath, int size) {
    // 嘗試使用 PrivateExtractIconsW 獲取精確尺寸
    // 失敗則使用 SHGetFileInfo
    // 大圖示使用 IImageList 介面
}
```

### 2. 雙緩衝繪製
**問題：** 直接繪製導致閃爍
**解決方案：**
```cpp
HDC memDC = CreateCompatibleDC(hdc);
HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
// ... 在記憶體 DC 繪製
UpdateLayeredWindow(..., memDC, ...);  // 一次性更新
```

### 3. 批次處理
**問題：** 逐一處理圖示導致多次重繪
**解決方案：** 使用 WM_SETREDRAW 包裹批次操作

### 4. RAII 資源管理
**問題：** GDI 資源洩漏
**解決方案：**
```cpp
~FencesWidget() {
    for (auto& fence : fences_) {
        for (auto& icon : fence.icons) {
            if (icon.hIcon32) DestroyIcon(icon.hIcon32);
            if (icon.hIcon48) DestroyIcon(icon.hIcon48);
            if (icon.hIcon64) DestroyIcon(icon.hIcon64);
        }
        DestroyWindow(fence.hwnd);
    }
}
```

### 5. 插件系統 ⭐ **新增**
**問題：** 功能耦合，難以擴展
**解決方案：**
- 定義 IWidget 介面
- 每個 Widget 編譯為獨立 DLL
- 使用 PluginLoader 動態載入
- 通過 ExecuteCommand 實現自定義功能

## 互動區域定義

### 標題列功能區
```
[標題文字................][收合圖示][釘住圖示]
|<------ 拖曳區 ------>||<--5px-->||<--5px-->|
                         20x20      20x20
```

### 調整大小區域
```
右下角 8x8 像素：IDC_SIZENWSE 游標
拖曳可調整柵欄尺寸（最小 150x100）
```

### 圖示網格佈局
```
計算公式：
iconCellWidth = max(iconSize, textWidth) + iconSpacing
iconCellHeight = iconSize + 35 + iconSpacing  // Icon + text + spacing

iconsPerRow = max(1, availableWidth / iconCellWidth)

繪製位置：
x = ICON_PADDING_LEFT + col * iconCellWidth + (iconCellWidth - iconSize) / 2
y = TITLE_BAR_HEIGHT + ICON_PADDING_TOP + row * iconCellHeight
```

## 桌面圖示互動

### 取得桌面 ListView
```cpp
HWND GetDesktopListView() {
    HWND hProgman = FindWindowW(L"Progman", nullptr);
    HWND hShellViewWin = FindWindowExW(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);

    // 處理 WorkerW 情況（Windows 10+）
    if (!hShellViewWin) {
        HWND hWorkerW = nullptr;
        while ((hWorkerW = FindWindowExW(nullptr, hWorkerW, L"WorkerW", nullptr)) != nullptr) {
            hShellViewWin = FindWindowExW(hWorkerW, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hShellViewWin) break;
        }
    }

    HWND hListView = FindWindowExW(hShellViewWin, nullptr, L"SysListView32", nullptr);
    return hListView;
}
```

### 圖示位置操作
```cpp
// 隱藏：移到螢幕外
SendMessageW(hListView, LVM_SETITEMPOSITION, index, MAKELPARAM(-1000, -1000));

// 顯示：還原原位置
SendMessageW(hListView, LVM_SETITEMPOSITION, index, MAKELPARAM(x, y));

// 重新排列
SendMessageW(hListView, LVM_ARRANGE, LVA_DEFAULT, 0);
```

## 效能優化歷程

### 優化前後對比

| 操作 | 優化前 | 優化後 | 改善 |
|------|--------|--------|------|
| 載入圖示 | 3次載入(32/48/64) | 1次載入(當前尺寸) | 66% 減少 |
| 移入柵欄(100圖示) | 100次重繪 | 1次重繪 | 99% 減少 |
| 啟動載入 | 逐個顯示 | 批次處理 | 明顯加速 |
| 記憶體使用 | 所有尺寸快取 | 按需快取 | ~50% 減少 |
| 停用再啟用 | 重複創建柵欄 | 保留數據 ⭐ | 問題修復 |

### 已實施的優化

1. **圖示快取與延遲載入**
   - 只載入當前需要的圖示尺寸
   - 切換尺寸時才載入新尺寸

2. **批次桌面操作**
   - HideDesktopIconsBatch
   - ShowDesktopIconsBatch
   - RestoreDesktopIconsBatch

3. **雙緩衝繪製**
   - 使用記憶體 DC 避免閃爍
   - UpdateLayeredWindow 一次性更新

4. **Widget 狀態保留** ⭐ **新增**
   - Stop() 時不清空數據，只隱藏窗口
   - Start() 時檢查數據是否存在，避免重複載入

## 已修復的問題 ⭐

### 1. 停用再啟用柵欄重複問題
**問題描述：** 每次停用再啟用 FencesWidget 會多出一個柵欄

**根本原因：**
- `Stop()` 隱藏柵欄窗口但不清空 `fences_` 列表
- `Start()` 每次都調用 `LoadConfiguration()`
- `LoadConfiguration()` 會調用 `CreateFence()` 創建新的柵欄窗口並添加到列表
- 結果：舊柵欄數據還在，又添加了新柵欄

**解決方案：**
```cpp
bool FencesWidget::Start() {
    // 只有當 fences_ 為空時才載入配置（首次啟動或清空後）
    if (fences_.empty()) {
        LoadConfiguration(configPath);
        if (fences_.empty()) {
            CreateFence(100, 100, 300, 400, L"桌面柵欄 1");
        }
    }

    // 顯示現有柵欄窗口
    for (auto& fence : fences_) {
        ShowWindow(fence.hwnd, SW_SHOW);
    }

    // 重新隱藏圖示
    for (auto& fence : fences_) {
        HideDesktopIconsBatch(iconPaths);
    }
}
```

### 2. Widget 狀態無法記憶
**問題描述：** 系統托盤關閉/啟用無法記憶 Widget 上次狀態

**解決方案：**
- 在 `%APPDATA%\FencesWidget\widget_states.conf` 保存狀態
- `SaveWidgetStates()` - 在切換 Widget 時自動保存
- `LoadWidgetStates()` - 在啟動時自動載入

### 3. 刪除柵欄未同步刪除配置
**問題描述：** 刪除柵欄後 config.json 仍保留該柵欄資料

**解決方案：**
```cpp
bool FencesWidget::RemoveFence(size_t index) {
    // ... 移除柵欄邏輯

    // 保存配置以確保刪除操作同步到 JSON 文件
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring configPath = std::wstring(appData) + L"\\FencesWidget\\config.json";
        SaveConfiguration(configPath);
    }
}
```

## 已知限制與注意事項

### 1. Windows 版本相容性
- 需要 Windows Vista+ (分層視窗支援)
- Windows 11 圓角效果需要對應 API

### 2. 桌面圖示限制
- 只能處理檔案系統圖示（不含特殊項目如"本機"）
- 依賴桌面 ListView 結構（可能因系統更新改變）

### 3. 效能考量
- 大量圖示（>500）可能影響繪製效能
- 頻繁的透明度更新會增加 GPU 負擔

### 4. 資源管理
- 必須在移除柵欄時清理所有 HICON
- 拖曳時的 ImageList 需要手動銷毀
- DLL 插件需要正確卸載以釋放資源

## 除錯技巧

### 1. 圖示載入問題
```cpp
// 檢查圖示是否成功載入
if (!hIcon) {
    OutputDebugStringW(L"Failed to load icon for: ");
    OutputDebugStringW(filePath.c_str());
}
```

### 2. 插件載入問題
```cpp
// 檢查 DLL 是否正確載入
if (!plugin.hModule) {
    wchar_t error[256];
    swprintf_s(error, L"Failed to load plugin: %s (Error: %d)",
               dllPath.c_str(), GetLastError());
    OutputDebugStringW(error);
}
```

### 3. 配置檔案檢查
手動檢查：
- `%APPDATA%\FencesWidget\config.json`
- `%APPDATA%\FencesWidget\widget_states.conf`

### 4. 視窗訊息追蹤
```cpp
// 在 HandleMessage 開頭加入
wchar_t dbg[256];
swprintf_s(dbg, L"Fence MSG: %d\n", msg);
OutputDebugStringW(dbg);
```

## 後續開發指南

### 新增功能步驟

1. **新增資料欄位**
   - 修改 `Fence` 或 `DesktopIcon` 結構
   - 更新 `SaveConfiguration` 儲存邏輯
   - 更新 `LoadConfiguration` 載入邏輯

2. **新增選單項目**
   - 在 `ShowFenceContextMenu` 中 `AppendMenuW`
   - 定義新的 IDM_* 常數
   - 在 WM_COMMAND 處理新命令

3. **新增視覺效果**
   - 修改 `PaintFence` 繪製邏輯
   - 考慮使用雙緩衝避免閃爍
   - 使用 `InvalidateRect` 觸發重繪

4. **新增插件自定義命令** ⭐
   - 在 WidgetExport.h 定義新命令 ID
   - 在 Widget DLL 的 ExecuteCommand 實現命令
   - 在 main.cpp 的 ShowTrayMenu 添加選單項目

### 新增 Widget 插件步驟

1. **創建 Widget 類別**
   ```cpp
   class MyWidget : public IWidget {
       // 實作 IWidget 介面
   };
   ```

2. **導出 DLL 函數**
   ```cpp
   extern "C" {
       WIDGET_API IWidget* CreateWidget(void* params) {
           return new MyWidget();
       }
       WIDGET_API void DestroyWidget(IWidget* widget) {
           delete widget;
       }
       WIDGET_API const wchar_t* GetWidgetName() {
           return L"MyWidget";
       }
       WIDGET_API const wchar_t* GetWidgetVersion() {
           return L"1.0.0";
       }
       WIDGET_API void ExecuteCommand(IWidget* widget, int commandId) {
           // 處理自定義命令
       }
   }
   ```

3. **在 CMakeLists.txt 添加編譯目標**
   ```cmake
   add_library(MyWidget SHARED
       widgets/MyWidget.h
       widgets/MyWidget.cpp
   )
   target_compile_definitions(MyWidget PRIVATE WIDGET_EXPORTS)
   ```

4. **編譯後將 DLL 放置在 exe 同目錄**
   - 主程序會自動掃描並載入

## 程式碼導覽

### 重要檔案索引

| 檔案 | 功能 | 關鍵內容 |
|------|------|---------|
| main.cpp | 主程序入口 | 系統托盤、插件載入、Widget 管理 |
| core/IWidget.h | Widget 介面 | Initialize, Start, Stop, Shutdown |
| core/WidgetManager.h/cpp | Widget 管理器 | RegisterWidget, EnableWidget, DisableWidget |
| core/PluginLoader.h/cpp | 插件載入器 | ScanPlugins, LoadPlugin, CreateWidgetInstance |
| core/WidgetExport.h | DLL 導出定義 | 導出函數簽名、命令 ID |
| widgets/FencesWidget.h | FencesWidget 介面 | Fence, DesktopIcon 結構定義 |
| widgets/FencesWidget.cpp | FencesWidget 實作 | 柵欄管理、圖示操作、自動分類 |

### 重要函式索引（FencesWidget.cpp）

| 功能 | 函式名稱 | 位置 (約略行數) |
|------|---------|----------------|
| 初始化 | FencesWidget() | ~65 |
| 啟動 | Start() | ~90 |
| 停止 | Stop() | ~355 |
| 建立柵欄 | CreateFence | ~650 |
| 刪除柵欄 | RemoveFence | ~742 |
| 繪製柵欄 | PaintFence | ~1400 |
| 滑鼠處理 | HandleMessage | ~858 |
| 新增圖示 | AddIconToFence | ~2250 |
| 移除圖示 | RemoveIconFromFence | ~2380 |
| 批次隱藏 | HideDesktopIconsBatch | ~2750 |
| 批次還原 | RestoreDesktopIconsBatch | ~2850 |
| 柵欄選單 | ShowFenceContextMenu | ~2632 |
| 自動分類 | AutoCategorizeDesktopIcons | ~202 |
| 清除資料 | ClearAllData ⭐ | ~315 |
| 儲存配置 | SaveConfiguration | ~373 |
| 載入配置 | LoadConfiguration | ~453 |
| DLL 導出 | CreateWidget, ExecuteCommand ⭐ | ~3162 |

## 測試建議

### 功能測試
- [x] 建立/刪除柵欄
- [x] 拖放檔案到柵欄
- [x] 柵欄間移動圖示
- [x] 收合/展開
- [x] 調整大小
- [x] 自訂顏色
- [x] 調整透明度
- [x] 自動分類 ⭐
- [x] 配置儲存/載入
- [x] Widget 停用/啟用狀態記憶 ⭐
- [x] 刪除柵欄同步刪除配置 ⭐
- [x] 清除所有資料 ⭐
- [x] DLL 插件動態載入 ⭐

### 效能測試
- [ ] 100+ 圖示載入速度
- [ ] 批次操作流暢度
- [ ] 記憶體使用量
- [ ] 拖曳回應速度
- [ ] 插件載入時間

### 相容性測試
- [x] Windows 10
- [ ] Windows 11
- [ ] 多螢幕環境
- [ ] 高 DPI 設定

## 常見問題排解

### Q: 柵欄透明度無效
A: 檢查 UpdateLayeredWindow 是否正確設定 ULW_ALPHA

### Q: 圖示顯示空白
A: 檢查 GetFileIcon 回傳值，確認檔案路徑正確

### Q: 桌面圖示找不到
A: 檢查 GetDesktopListView 是否正確取得 ListView，嘗試 WorkerW 分支

### Q: 拖曳沒有視覺效果
A: 確認 ImageList_BeginDrag 成功，檢查快取的 hIcon 是否存在

### Q: 配置無法載入
A: 檢查 JSON 格式，路徑是否存在，解析邏輯是否正確

### Q: 插件無法載入 ⭐
A: 檢查 DLL 是否在 exe 同目錄，是否導出了必需的函數

### Q: 停用再啟用出現重複柵欄 ⭐
A: 已修復，確保使用最新版本

## 建置與部署

### 建置步驟
```bash
cd E:\Code\c++\Fences_claude
mkdir build2
cd build2
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

### 執行檔位置
```
build2/bin/Release/
├── DesktopWidgetManager.exe    # 主程序 (~52 KB)
├── FencesWidget.dll             # FencesWidget 插件 (~80 KB)
└── StickyNotesWidget.dll        # 便簽插件 (~63 KB)
```

### 部署要求
- 將 exe 和所有 DLL 放在同一目錄
- 主程序會自動掃描目錄載入所有 Widget DLL
- 配置文件會自動創建在 `%APPDATA%\FencesWidget\`

### 依賴項
- Windows SDK
- C++17 或更新
- CMake 3.10+
- Visual Studio 2022 (建議)

## 更新日誌

### 版本 2.0 (2025-10-04) ⭐ 插件化架構
**重大變更：**
- [x] 採用 DLL 插件架構
- [x] 實現插件動態載入系統
- [x] 系統托盤選單改為帶子選單結構
- [x] Widget 狀態持久化

**新功能：**
- [x] FencesWidget 右鍵選單增加「自動分類桌面圖示」
- [x] 系統托盤 FencesWidget 子選單：建立新柵欄、清除所有記錄
- [x] 清除所有資料功能（ClearAllData）
- [x] 自定義命令系統（ExecuteCommand）

**問題修復：**
- [x] 修復停用再啟用出現重複柵欄的問題
- [x] 修復 Widget 狀態無法記憶的問題
- [x] 修復刪除柵欄未同步刪除配置的問題

**效能優化：**
- [x] Widget 停用時保留數據，避免重複載入

### 版本 1.0 (之前)
- [x] 基礎柵欄功能
- [x] 圖示管理
- [x] 自動分類
- [x] 配置持久化
- [x] 批次操作優化

## 授權與貢獻

本專案為桌面工具開發學習範例。

建議的貢獻方向：
1. 效能優化
2. 新增佈局選項
3. 改進視覺效果
4. 增強鍵盤支援
5. 多語言介面
6. 開發新的 Widget 插件 ⭐

---

**文檔版本：** 2.0
**最後更新：** 2025-10-04
**維護者：** FencesWidget 開發團隊
