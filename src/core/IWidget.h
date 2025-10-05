#pragma once

#include <string>
#include <windows.h>

// Widget interface base class
// All desktop widgets must implement this interface
class IWidget {
public:
    virtual ~IWidget() = default;

    // Initialize widget
    // Returns true on success, false on failure
    virtual bool Initialize() = 0;

    // Start widget
    // Returns true on success, false on failure
    virtual bool Start() = 0;

    // Stop widget
    virtual void Stop() = 0;

    // Shutdown widget
    virtual void Shutdown() = 0;

    // Get widget name
    virtual std::wstring GetName() const = 0;

    // Get widget description
    virtual std::wstring GetDescription() const = 0;

    // Check if widget is running
    virtual bool IsRunning() const = 0;

    // Get widget version
    virtual std::wstring GetWidgetVersion() const = 0;
};
