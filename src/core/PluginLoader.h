#pragma once
#include "IWidget.h"
#include "WidgetExport.h"
#include <windows.h>
#include <string>
#include <vector>
#include <memory>

struct PluginInfo {
    std::wstring dllPath;
    std::wstring name;
    std::wstring version;
    HMODULE hModule;
    CreateWidgetFunc createFunc;
    DestroyWidgetFunc destroyFunc;
    ExecuteCommandFunc executeCommandFunc;
    std::shared_ptr<IWidget> widgetInstance;
};

class PluginLoader {
public:
    // 掃描指定目錄下的所有 Widget DLL
    static std::vector<PluginInfo> ScanPlugins(const std::wstring& directory);

    // 載入單個 DLL
    static bool LoadPlugin(const std::wstring& dllPath, PluginInfo& outInfo);

    // 卸載 DLL
    static void UnloadPlugin(PluginInfo& plugin);

    // 創建 Widget 實例
    static std::shared_ptr<IWidget> CreateWidgetInstance(PluginInfo& plugin, void* params = nullptr);

    // 銷毀 Widget 實例
    static void DestroyWidgetInstance(PluginInfo& plugin);

private:
    // 檢查是否為 Widget DLL
    static bool IsWidgetDLL(const std::wstring& dllPath);
};
