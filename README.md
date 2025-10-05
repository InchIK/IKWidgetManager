# IKWidgetManager - Windows 桌面插件管理器

IKWidgetManager 是一個用 C++ 編寫的現代化 Windows 桌面小工具管理系統，採用 **DLL 插件架構**，使其輕量、靈活且易於擴展。

主程序 `DesktopWidgetManager.exe` 負責掃描並加載位於同一目錄下的所有 Widget 插件（`.dll` 檔案），並透過系統托盤圖示提供統一的管理介面。

![架構圖](https://i.imgur.com/your-architecture-diagram.png)  <!-- 建議未來可以補上架構圖 -->

## 主要功能

### 核心管理器
- **插件化架構**：每個 Widget 都是一個獨立的 DLL，可獨立開發與部署。
- **動態加載**：主程序在啟動時自動掃描並加載所有可用的 Widget 插件。
- **系統托盤控制**：透過系統托盤圖示的右鍵選單，可以啟用/停用各個 Widget，並執行 Widget 提供的自定義命令。
- **狀態持久化**：自動記錄每個 Widget 的啟用/停用狀態，下次啟動時恢復。
- **開機自動啟動**：可設定是否隨 Windows 開機啟動。

### FencesWidget (桌面柵欄插件)
本專案的核心插件，提供類似 Stardock Fences 的桌面圖示整理功能。
- **創建柵欄**：在桌面上建立半透明的容器來組織桌面圖示。
- **拖放支持**：可將桌面圖示拖入柵欄，或在柵欄之間移動。
- **外觀自定義**：可修改柵欄的背景顏色、標題顏色和透明度。
- **自動分類**：一鍵將桌面上的圖示按「文件、圖片、應用程式」等類別自動整理到對應的柵欄中。
- **滾動與收合**：當圖示過多時，柵欄支持滾動；也可將柵欄收合，只顯示標題列。
- **配置持久化**：柵欄的位置、大小、外觀和圖示內容都會自動保存。

### StickyNotesWidget (便利貼插件)
一個簡單的便利貼範例插件，用於演示插件系統的擴展能力。

## 專案結構
```
IKWidgetManager/
├── src/
│   ├── main.cpp                    # 應用程式入口點，系統托盤管理
│   ├── core/
│   │   ├── IWidget.h               # Widget 插件介面定義
│   │   ├── WidgetManager.h/cpp     # Widget 管理器
│   │   ├── WidgetExport.h          # DLL 導出宏和函數簽名
│   │   └── PluginLoader.h/cpp      # 插件動態加載器
│   └── widgets/
│       ├── FencesWidget.h/cpp      # FencesWidget 插件實現
│       └── StickyNotesWidget.h/cpp # StickyNotesWidget 插件實現
├── build/                          # CMake 構建目錄
│   └── bin/Release/
│       ├── DesktopWidgetManager.exe    # 主程序
│       ├── FencesWidget.dll            # FencesWidget 插件
│       └── StickyNotesWidget.dll       # 便利貼插件
└── README.md                       # 本文檔
```

## 編譯與執行

### 環境要求
- Windows 10 或更高版本
- Visual Studio 2022 (MSVC C++17)
- CMake 3.15 或更高版本

### 編譯步驟 (命令行)
```bash
# 1. 創建構建目錄
mkdir build
cd build

# 2. 使用 CMake 生成 Visual Studio 專案
#    請確保您的環境中 Visual Studio 2022 的生成器是可用的
cmake -G "Visual Studio 17 2022" ..

# 3. 編譯 Release 版本
cmake --build . --config Release
```

### 執行
編譯完成後，所有必要的檔案都會在 `build/bin/Release/` 目錄下。
```
build/bin/Release/
├── DesktopWidgetManager.exe    # 雙擊運行此主程序
├── FencesWidget.dll
└── StickyNotesWidget.dll
```
**重要提示**：請確保所有 `.dll` 插件檔案與 `.exe` 主程序位於同一目錄下，否則插件將無法被加載。

## 如何擴展開發：創建新的 Widget

得益於插件化架構，您可以輕鬆創建自己的 Widget：

1. **創建 Widget 類**：在 `src/widgets/` 目錄下，創建一個新類並繼承自 `IWidget` 介面。
2. **實現介面方法**：實現 `Start()`, `Stop()` 等虛函數。
3. **導出 C 接口**：在您的 Widget cpp 檔案中，導出 `CreateWidget`, `DestroyWidget` 等 C 風格的函數，作為 DLL 的入口點。
4. **更新 CMakeLists.txt**：在 `src/CMakeLists.txt` 中，為您的新 Widget 添加一個 `add_library` 規則，將其編譯為 `SHARED` 庫 (DLL)。
5. **編譯**：重新編譯專案，新的 `.dll` 檔案將會生成。將它和主程序放在一起即可被自動加載。

詳細的接口定義和導出宏請參考 `src/core/WidgetExport.h`。

## 更新日誌

### 版本 2.0 (2025-10-04) - 插件化架構
- **重大變更**：重構為 DLL 插件架構，主程序與 Widget 完全解耦。
- **新功能**：
    - `FencesWidget` 新增「自動分類桌面圖示」功能。
    - `FencesWidget` 新增「清除所有資料」功能。
    - 系統托盤選單支持子選單，可執行 Widget 的自定義命令。
    - 新增 `StickyNotesWidget` 作為第二個插件範例。
- **修復**：
    - 修復了停用再啟用 Widget 時會出現重複項的問題。
    - 修復了 Widget 啟用狀態無法保存的問題。
    - 修復了刪除柵欄後，配置文件未同步更新的問題。
- **優化**：
    - 停用 Widget 時保留其數據，再次啟用時可立即恢復，無需重新加載。

### 版本 1.0
- 基礎 `FencesWidget` 功能實現。
- 單體應用程式結構。

## 授權
本專案為開源學習範例，您可以自由修改、使用和分發。