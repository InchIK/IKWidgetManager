# 桌面 Widget 管理器

一個用 C++ 編寫的 Windows 桌面 Widget 管理系統，第一個 Widget 是類似 Stardock Fences 的桌面柵欄工具。

## 功能特點

### Widget 管理器
- 統一管理多個桌面 Widget
- 支援動態啟用/停用 Widget
- 系統托盤控制介面
- 插件式架構，易於擴展

### Fences 桌面柵欄 Widget
- 創建透明的桌面柵欄區域
- 可拖動移動柵欄位置
- 右下角拖動調整大小
- 自定義標題和外觀
- 半透明效果

## 項目結構

```
Fences_claude/
├── CMakeLists.txt              # 主 CMake 配置
├── README.md                   # 本文件
├── src/
│   ├── CMakeLists.txt          # 源碼 CMake 配置
│   ├── main.cpp                # 主程序入口
│   ├── core/                   # Widget 管理器核心
│   │   ├── IWidget.h           # Widget 介面定義
│   │   ├── WidgetManager.h     # Widget 管理器頭文件
│   │   └── WidgetManager.cpp   # Widget 管理器實現
│   └── widgets/                # Widget 實現
│       ├── FencesWidget.h      # Fences Widget 頭文件
│       └── FencesWidget.cpp    # Fences Widget 實現
└── build/                      # 編譯輸出目錄（自動生成）
```

## 編譯要求

### 系統要求
- Windows 10 或更高版本
- Visual Studio 2019 或更高版本（推薦使用 MSVC 編譯器）
- CMake 3.15 或更高版本

### 依賴項
- Windows SDK（包含在 Visual Studio 中）
- C++17 標準庫

## 編譯步驟

### 使用 CMake GUI

1. 打開 CMake GUI
2. 設置 "Where is the source code" 為項目根目錄
3. 設置 "Where to build the binaries" 為 `build` 目錄
4. 點擊 "Configure"，選擇 Visual Studio 生成器
5. 點擊 "Generate"
6. 點擊 "Open Project" 打開 Visual Studio
7. 在 Visual Studio 中按 F7 編譯

### 使用命令行

```bash
# 創建編譯目錄
mkdir build
cd build

# 配置 CMake（使用 Visual Studio 2019）
cmake .. -G "Visual Studio 16 2019" -A x64

# 編譯（Release 版本）
cmake --build . --config Release

# 或者編譯 Debug 版本
cmake --build . --config Debug
```

### 使用 Visual Studio Developer Command Prompt

```bash
# 創建編譯目錄
mkdir build
cd build

# 配置並編譯
cmake .. -G "Visual Studio 16 2019" -A x64
msbuild DesktopWidgetManager.sln /p:Configuration=Release
```

## 運行程序

編譯完成後，可執行文件位於：
```
build/bin/Release/DesktopWidgetManager.exe
```

或者 Debug 版本：
```
build/bin/Debug/DesktopWidgetManager.exe
```

雙擊運行即可。

## 使用說明

### 啟動程序

1. 運行 `DesktopWidgetManager.exe`
2. 程序會在系統托盤顯示圖標
3. 默認會啟動一個桌面柵欄

### 控制 Widget

- **右鍵點擊托盤圖標**：打開控制菜單
- **切換 Widget**：點擊菜單中的 "桌面柵欄 Widget" 來啟用/停用
- **退出程序**：選擇 "退出"

### 操作桌面柵欄

- **移動柵欄**：在柵欄任意位置按住左鍵拖動
- **調整大小**：拖動柵欄右下角的灰色區域
- **最小尺寸**：100x100 像素

## 架構設計

### 核心組件

#### IWidget 介面
所有 Widget 必須實現的介面，定義了基本的生命週期方法：
- `Initialize()`: 初始化 Widget
- `Start()`: 啟動 Widget
- `Stop()`: 停止 Widget
- `Shutdown()`: 清理資源

#### WidgetManager
單例模式的管理器，負責：
- Widget 註冊和反註冊
- Widget 生命週期管理
- 啟用/停用控制
- 線程安全操作

#### FencesWidget
實現桌面柵欄功能：
- 使用分層窗口（Layered Window）實現半透明效果
- 雙緩衝繪圖避免閃爍
- 支持拖動和調整大小
- 可擴展的柵欄管理

## 擴展開發

### 創建新的 Widget

1. 創建新的類繼承 `IWidget`
2. 實現所有虛函數
3. 在 `src/widgets/` 目錄下添加源文件
4. 修改 `src/CMakeLists.txt` 添加新的庫
5. 在 `main.cpp` 中註冊新 Widget

示例：
```cpp
class MyWidget : public IWidget {
public:
    bool Initialize() override {
        // 初始化代碼
        return true;
    }

    bool Start() override {
        // 啟動代碼
        return true;
    }

    void Stop() override {
        // 停止代碼
    }

    void Shutdown() override {
        // 清理代碼
    }

    // ... 實現其他方法
};
```

## 已知限制

1. 目前僅支持 Windows 平台
2. 柵欄不會自動捕獲桌面圖標（需要手動移動圖標）
3. 柵欄配置不會持久化（重啟後恢復默認）

## 未來計劃

- [ ] 添加柵欄配置保存/加載功能
- [ ] 實現桌面圖標自動整理到柵欄
- [ ] 添加更多自定義選項（顏色、透明度等）
- [ ] 實現多柵欄管理 UI
- [ ] 添加更多 Widget（時鐘、天氣、系統監控等）
- [ ] 支持 Widget 熱重載

## 許可證

本項目僅供學習和研究使用。

## 作者

由 Claude 協助開發
