#pragma once

#include "IWidget.h"
#include <vector>
#include <memory>
#include <map>
#include <mutex>

/**
 * @brief Widget 管理器
 * 負責管理所有桌面 Widget 的生命週期
 */
class WidgetManager {
public:
    static WidgetManager& GetInstance();

    // 禁止複製和賦值
    WidgetManager(const WidgetManager&) = delete;
    WidgetManager& operator=(const WidgetManager&) = delete;

    /**
     * @brief 初始化管理器
     * @return 成功返回 true
     */
    bool Initialize();

    /**
     * @brief 註冊 Widget
     * @param widget Widget 智能指針
     * @return 成功返回 true
     */
    bool RegisterWidget(std::shared_ptr<IWidget> widget);

    /**
     * @brief 反註冊 Widget
     * @param widgetName Widget 名稱
     * @return 成功返回 true
     */
    bool UnregisterWidget(const std::wstring& widgetName);

    /**
     * @brief 啟用 Widget
     * @param widgetName Widget 名稱
     * @return 成功返回 true
     */
    bool EnableWidget(const std::wstring& widgetName);

    /**
     * @brief 停用 Widget
     * @param widgetName Widget 名稱
     * @return 成功返回 true
     */
    bool DisableWidget(const std::wstring& widgetName);

    /**
     * @brief 獲取 Widget
     * @param widgetName Widget 名稱
     * @return Widget 智能指針，未找到返回 nullptr
     */
    std::shared_ptr<IWidget> GetWidget(const std::wstring& widgetName) const;

    /**
     * @brief 獲取所有 Widget
     * @return Widget 列表
     */
    std::vector<std::shared_ptr<IWidget>> GetAllWidgets() const;

    /**
     * @brief 檢查 Widget 是否已啟用
     * @param widgetName Widget 名稱
     * @return 已啟用返回 true
     */
    bool IsWidgetEnabled(const std::wstring& widgetName) const;

    /**
     * @brief 關閉所有 Widget 並清理資源
     */
    void Shutdown();

private:
    WidgetManager() = default;
    ~WidgetManager();

    struct WidgetInfo {
        std::shared_ptr<IWidget> widget;
        bool enabled;
        bool initialized;
    };

    std::map<std::wstring, WidgetInfo> widgets_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};
