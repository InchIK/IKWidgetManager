#include "WidgetManager.h"
#include <algorithm>

WidgetManager& WidgetManager::GetInstance() {
    static WidgetManager instance;
    return instance;
}

WidgetManager::~WidgetManager() {
    Shutdown();
}

bool WidgetManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    initialized_ = true;
    return true;
}

bool WidgetManager::RegisterWidget(std::shared_ptr<IWidget> widget) {
    if (!widget) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::wstring name = widget->GetName();

    // Check if already registered
    if (widgets_.find(name) != widgets_.end()) {
        return false;
    }

    // Initialize widget
    if (!widget->Initialize()) {
        return false;
    }

    // Add to manager
    WidgetInfo info;
    info.widget = widget;
    info.enabled = false;
    info.initialized = true;

    widgets_[name] = info;
    return true;
}

bool WidgetManager::UnregisterWidget(const std::wstring& widgetName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = widgets_.find(widgetName);
    if (it == widgets_.end()) {
        return false;
    }

    // Stop if running
    if (it->second.enabled) {
        it->second.widget->Stop();
    }

    // Cleanup resources
    it->second.widget->Shutdown();
    widgets_.erase(it);

    return true;
}

bool WidgetManager::EnableWidget(const std::wstring& widgetName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = widgets_.find(widgetName);
    if (it == widgets_.end()) {
        return false;
    }

    // Already enabled, return directly
    if (it->second.enabled) {
        return true;
    }

    // Start widget
    if (!it->second.widget->Start()) {
        return false;
    }

    it->second.enabled = true;
    return true;
}

bool WidgetManager::DisableWidget(const std::wstring& widgetName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = widgets_.find(widgetName);
    if (it == widgets_.end()) {
        return false;
    }

    // Already disabled, return directly
    if (!it->second.enabled) {
        return true;
    }

    // Stop widget
    it->second.widget->Stop();
    it->second.enabled = false;

    return true;
}

std::shared_ptr<IWidget> WidgetManager::GetWidget(const std::wstring& widgetName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = widgets_.find(widgetName);
    if (it == widgets_.end()) {
        return nullptr;
    }

    return it->second.widget;
}

std::vector<std::shared_ptr<IWidget>> WidgetManager::GetAllWidgets() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<IWidget>> result;
    result.reserve(widgets_.size());

    for (const auto& pair : widgets_) {
        result.push_back(pair.second.widget);
    }

    return result;
}

bool WidgetManager::IsWidgetEnabled(const std::wstring& widgetName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = widgets_.find(widgetName);
    if (it == widgets_.end()) {
        return false;
    }

    return it->second.enabled;
}

void WidgetManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Stop and cleanup all widgets
    for (auto& pair : widgets_) {
        if (pair.second.enabled) {
            pair.second.widget->Stop();
        }
        pair.second.widget->Shutdown();
    }

    widgets_.clear();
    initialized_ = false;
}
