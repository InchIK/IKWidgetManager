#include "PluginLoader.h"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<PluginInfo> PluginLoader::ScanPlugins(const std::wstring& directory) {
    std::vector<PluginInfo> plugins;

    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::wstring filePath = entry.path().wstring();
                std::wstring extension = entry.path().extension().wstring();

                // 只處理 .dll 檔案
                if (extension == L".dll" || extension == L".DLL") {
                    PluginInfo info;
                    if (LoadPlugin(filePath, info)) {
                        plugins.push_back(info);
                    }
                }
            }
        }
    } catch (...) {
        // 忽略目錄掃描錯誤
    }

    return plugins;
}

bool PluginLoader::LoadPlugin(const std::wstring& dllPath, PluginInfo& outInfo) {
    // 載入 DLL
    HMODULE hModule = LoadLibraryW(dllPath.c_str());
    if (!hModule) {
        return false;
    }

    // 取得必要的函式指標
    auto createFunc = (CreateWidgetFunc)GetProcAddress(hModule, "CreateWidget");
    auto destroyFunc = (DestroyWidgetFunc)GetProcAddress(hModule, "DestroyWidget");
    auto getNameFunc = (const wchar_t*(*)())GetProcAddress(hModule, "GetWidgetName");
    auto getVersionFunc = (const wchar_t*(*)())GetProcAddress(hModule, "GetWidgetVersion");
    auto executeCommandFunc = (ExecuteCommandFunc)GetProcAddress(hModule, "ExecuteCommand");

    // 檢查是否為有效的 Widget DLL
    if (!createFunc || !destroyFunc || !getNameFunc || !getVersionFunc) {
        FreeLibrary(hModule);
        return false;
    }

    // 填充插件資訊
    outInfo.dllPath = dllPath;
    outInfo.name = getNameFunc();
    outInfo.version = getVersionFunc();
    outInfo.hModule = hModule;
    outInfo.createFunc = createFunc;
    outInfo.destroyFunc = destroyFunc;
    outInfo.executeCommandFunc = executeCommandFunc;  // 可能為 nullptr（舊版 Widget 不支持）
    outInfo.widgetInstance = nullptr;

    return true;
}

void PluginLoader::UnloadPlugin(PluginInfo& plugin) {
    // 先銷毀 Widget 實例
    if (plugin.widgetInstance) {
        DestroyWidgetInstance(plugin);
    }

    // 卸載 DLL
    if (plugin.hModule) {
        FreeLibrary(plugin.hModule);
        plugin.hModule = nullptr;
    }
}

std::shared_ptr<IWidget> PluginLoader::CreateWidgetInstance(PluginInfo& plugin, void* params) {
    if (!plugin.createFunc) {
        return nullptr;
    }

    // 創建 Widget 實例
    IWidget* widget = plugin.createFunc(params);
    if (!widget) {
        return nullptr;
    }

    // 使用自訂刪除器，確保通過 DLL 的 DestroyWidget 函式釋放
    auto deleter = [&plugin](IWidget* w) {
        if (plugin.destroyFunc && w) {
            plugin.destroyFunc(w);
        }
    };

    plugin.widgetInstance = std::shared_ptr<IWidget>(widget, deleter);
    return plugin.widgetInstance;
}

void PluginLoader::DestroyWidgetInstance(PluginInfo& plugin) {
    if (plugin.widgetInstance) {
        plugin.widgetInstance.reset();
    }
}

bool PluginLoader::IsWidgetDLL(const std::wstring& dllPath) {
    HMODULE hModule = LoadLibraryW(dllPath.c_str());
    if (!hModule) {
        return false;
    }

    bool isValid = GetProcAddress(hModule, "CreateWidget") != nullptr &&
                   GetProcAddress(hModule, "DestroyWidget") != nullptr &&
                   GetProcAddress(hModule, "GetWidgetName") != nullptr &&
                   GetProcAddress(hModule, "GetWidgetVersion") != nullptr;

    FreeLibrary(hModule);
    return isValid;
}
