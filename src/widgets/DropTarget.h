#pragma once

#include <windows.h>
#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>
#include <string>
#include <vector>

// Forward declaration
class FencesWidget;
struct Fence;

/**
 * FenceDropTarget - IDropTarget implementation for Fence windows
 *
 * This enables proper drag-and-drop support with MOVE semantics:
 * - When dragging from desktop to fence: DROPEFFECT_MOVE (icon disappears from desktop)
 * - When holding Ctrl key: DROPEFFECT_COPY (icon stays on desktop)
 *
 * This replaces the simple DragAcceptFiles/WM_DROPFILES mechanism which only
 * supports COPY operations and cannot make icons disappear from desktop.
 */
class FenceDropTarget : public IDropTarget {
public:
    FenceDropTarget(FencesWidget* widget, Fence* fence);
    virtual ~FenceDropTarget();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget methods
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState,
                          POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState,
                     POINTL pt, DWORD* pdwEffect) override;

private:
    LONG refCount_;
    FencesWidget* widget_;
    Fence* fence_;

    // Helper: Extract file paths from IDataObject
    bool GetDroppedFiles(IDataObject* pDataObj, std::vector<std::wstring>& files);

    // Helper: Check if file is from desktop
    bool IsDesktopFile(const std::wstring& filePath);
};
