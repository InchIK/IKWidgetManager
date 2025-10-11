#pragma once

#include "core/IWidget.h"
#include "ShellIntegration.h"
#include "FileManager.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <ole2.h>
#include <vector>
#include <string>
#include <unordered_set>

// Forward declaration
class FenceDropTarget;

// Desktop icon information
struct DesktopIcon {
    std::wstring filePath;        // Full path to file/folder
    std::wstring displayName;     // Display name
    HICON hIcon;                  // Icon handle (cached)
    HICON hIcon32;                // 32px cached icon
    HICON hIcon48;                // 48px cached icon
    HICON hIcon64;                // 64px cached icon
    int cachedIconSize;           // Currently cached icon size
    POINT position;               // Position within fence
    bool selected;                // Selection state
    // Note: originalDesktopPos and originalDesktopIndex removed in v3.0
    // Icons never leave Desktop folder, so no need to track original position
};

// Desktop fence structure
struct Fence {
    HWND hwnd;                    // Fence window handle
    RECT rect;                    // Fence position and size
    std::wstring title;           // Fence title
    COLORREF backgroundColor;     // Background color
    COLORREF borderColor;         // Border color
    COLORREF titleColor;          // Title text color
    int borderWidth;              // Border width
    int alpha;                    // Transparency (0-255)
    bool isResizing;              // Is resizing
    bool isDragging;              // Is dragging (fence itself)
    POINT dragOffset;             // Drag offset
    std::vector<DesktopIcon> icons; // Icons in this fence
    int iconSpacing;              // Spacing between icons
    int iconSize;                 // Icon size (32, 48, etc)

    // Icon dragging state
    bool isDraggingIcon;          // Is dragging an icon
    int draggingIconIndex;        // Index of icon being dragged
    POINT iconDragStart;          // Starting position of icon drag

    // Collapse and pin state
    bool isCollapsed;             // Is fence collapsed (only title visible)
    bool isPinned;                // Is fence pinned (cannot move)
    int expandedHeight;           // Height when expanded

    // Scroll state
    int scrollOffset;             // Current vertical scroll offset
    int contentHeight;            // Total content height (all icons)
    bool isDraggingScrollbar;     // Is dragging scrollbar
    int scrollbarDragStartY;      // Starting Y position of scrollbar drag
    int scrollOffsetAtDragStart;  // Scroll offset when drag started

    // IDropTarget for proper drag-and-drop support (NEW in v3.1)
    FenceDropTarget* pDropTarget; // COM drop target interface
};

// Fences desktop fence widget
// Provides Stardock Fences-like desktop icon organization
class FencesWidget : public IWidget {
    // Friend class for drag-drop support
    friend class FenceDropTarget;

public:
    FencesWidget();
    virtual ~FencesWidget();

    // IWidget interface implementation
    bool Initialize() override;
    bool Start() override;
    void Stop() override;
    void Shutdown() override;
    std::wstring GetName() const override;
    std::wstring GetDescription() const override;
    bool IsRunning() const override;
    std::wstring GetWidgetVersion() const override;

    // Create a new fence
    bool CreateFence(int x, int y, int width, int height, const std::wstring& title);

    // Remove a fence
    bool RemoveFence(size_t index);

    // Get fence count
    size_t GetFenceCount() const;

    // Update fence title
    bool UpdateFenceTitle(size_t index, const std::wstring& newTitle);

    // Update fence style
    bool UpdateFenceStyle(size_t index, COLORREF bgColor, COLORREF borderColor, int alpha);

    // Save configuration to JSON file
    bool SaveConfiguration(const std::wstring& filePath);

    // Load configuration from JSON file
    bool LoadConfiguration(const std::wstring& filePath);

    // Restore all desktop icons to original positions
    void RestoreAllDesktopIcons();

    // Auto-categorize desktop icons
    void AutoCategorizeDesktopIcons();

    // Clear all data and configuration
    void ClearAllData();

private:
    // ========================================================================
    // Phase 1: ListView Subclassing & Custom Draw (NEW in v3.0)
    // ========================================================================

    // Setup desktop ListView subclass
    bool SetupDesktopSubclass();

    // Remove desktop ListView subclass
    void RemoveDesktopSubclass();

    // Find desktop ListView window
    HWND FindDesktopListView();

    // Subclass procedure for desktop ListView
    static LRESULT CALLBACK ListViewSubclassProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    // Handle Custom Draw notification
    LRESULT OnCustomDraw(NMLVCUSTOMDRAW* pCD);

    // Get desktop item path by ListView index
    std::wstring GetDesktopItemPath(int itemIndex);

    // Check if icon is managed by any fence
    bool IsIconManagedByFence(const std::wstring& path);

    // ========================================================================
    // Shell Notification Handlers (NEW in v3.0)
    // ========================================================================

    // Handle desktop item created
    void OnDesktopItemCreated(const std::wstring& path);

    // Handle desktop item deleted
    void OnDesktopItemDeleted(const std::wstring& path);

    // Handle desktop item renamed
    void OnDesktopItemRenamed(const std::wstring& oldPath, const std::wstring& newPath);

    // ========================================================================
    // Utility Methods
    // ========================================================================

    // Get category for file
    std::wstring GetFileCategory(const std::wstring& filePath);

    // Get file extension
    std::wstring GetFileExtension(const std::wstring& filePath);

    // Register window class
    bool RegisterWindowClass();

    // Unregister window class
    void UnregisterWindowClass();

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Legacy subclass proc (backup method)
    static LRESULT CALLBACK ListViewSubclassProcLegacy(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Handle window messages
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Paint fence
    void PaintFence(HWND hwnd, HDC hdc);

    // Find fence by window handle
    Fence* FindFence(HWND hwnd);

    // Handle left button down
    void OnLButtonDown(Fence* fence, int x, int y);

    // Handle mouse move
    void OnMouseMove(Fence* fence, int x, int y);

    // Handle left button up
    void OnLButtonUp(Fence* fence);

    // Handle right button down (context menu)
    void OnRButtonDown(Fence* fence, int x, int y);

    // NOTE: OnDropFiles removed in v3.1, replaced by IDropTarget (see DropTarget.h)

    // Check if in resize area
    bool IsInResizeArea(const RECT& rect, int x, int y) const;

    // Check if in title bar area
    bool IsInTitleArea(const RECT& rect, int x, int y) const;

    // Check if in scrollbar area
    bool IsInScrollbarArea(Fence* fence, int x, int y, RECT* outThumbRect = nullptr) const;

    // Add icon to fence
    bool AddIconToFence(Fence* fence, const std::wstring& filePath);

    // Remove icon from fence
    bool RemoveIconFromFence(Fence* fence, size_t iconIndex);

    // Arrange icons in fence
    void ArrangeIcons(Fence* fence);

    // Get icon from file
    HICON GetFileIcon(const std::wstring& filePath, int size);

    // Draw icon with text
    void DrawIcon(HDC hdc, DesktopIcon& icon, int x, int y, int iconSize);

    // Show fence context menu
    void ShowFenceContextMenu(Fence* fence, int x, int y);

    // ========================================================================
    // DEPRECATED: Old icon hiding methods (to be removed)
    // These methods are replaced by Custom Draw rendering suppression in v3.0
    // ========================================================================
    // bool HideDesktopIcon(const std::wstring& filePath);
    // bool ShowDesktopIcon(const std::wstring& filePath);
    // bool ShowDesktopIconAtPosition(const std::wstring& filePath, int x, int y);
    // int FindDesktopIconIndex(const std::wstring& filePath);
    // POINT GetDesktopIconPosition(int iconIndex);
    // void HideDesktopIconsBatch(const std::vector<std::wstring>& filePaths);
    // void ShowDesktopIconsBatch(const std::vector<std::wstring>& filePaths);
    // void RestoreDesktopIconsBatch(const std::vector<std::pair<std::wstring, POINT>>& iconData);

    // Temporary compatibility methods (will be fully removed later)
    HWND GetDesktopListView();
    int FindDesktopIconIndex(const std::wstring& filePath);
    POINT GetDesktopIconPosition(int iconIndex);

    // Icon interaction
    int FindIconAtPosition(Fence* fence, int x, int y);
    void ShowIconContextMenu(Fence* fence, int iconIndex, int x, int y);

    // ========================================================================
    // Member Variables
    // ========================================================================

    std::vector<Fence> fences_;
    bool running_;
    bool shutdownCalled_;  // Prevent duplicate Shutdown calls
    HINSTANCE hInstance_;
    const wchar_t* windowClassName_;
    bool classRegistered_;
    HWND desktopWindow_;
    HWND desktopListView_;      // Desktop ListView (SysListView32)
    HWND desktopShellView_;     // Desktop ShellView parent (SHELLDLL_DefView)
    WNDPROC originalShellViewProc_;  // Original WndProc (for legacy subclass)
    int selectedIconIndex_;
    Fence* selectedFence_;

    // NEW in v3.0: Managed icon paths (for Custom Draw suppression)
    std::unordered_set<std::wstring> managedIconPaths_;

    // NEW in v3.0: Subclass ID for desktop ListView
    static const UINT_PTR DESKTOP_SUBCLASS_ID = 9001;

    // NEW in v3.0: Shell notification listener
    ShellNotifyListener* shellNotifyListener_;

    // NEW in v3.0: Desktop enumerator
    DesktopEnumerator* desktopEnumerator_;

    // NEW in v4.0: File manager for safe file operations
    FileManager* fileManager_;
};
