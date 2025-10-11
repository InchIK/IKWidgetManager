#include "DropTarget.h"
#include "FencesWidget.h"
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

FenceDropTarget::FenceDropTarget(FencesWidget* widget, Fence* fence)
    : refCount_(1), widget_(widget), fence_(fence) {
}

FenceDropTarget::~FenceDropTarget() {
}

// ===========================================================================
// IUnknown Implementation
// ===========================================================================

STDMETHODIMP FenceDropTarget::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_INVALIDARG;
    }

    *ppvObject = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropTarget)) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) FenceDropTarget::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) FenceDropTarget::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

// ===========================================================================
// IDropTarget Implementation
// ===========================================================================

STDMETHODIMP FenceDropTarget::DragEnter(IDataObject* pDataObj, DWORD grfKeyState,
                                        POINTL pt, DWORD* pdwEffect) {
    if (!pDataObj || !pdwEffect) {
        return E_INVALIDARG;
    }

    // Check if data format is acceptable (CF_HDROP)
    FORMATETC fmtEtc = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (FAILED(pDataObj->QueryGetData(&fmtEtc))) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    // Determine drop effect based on keyboard state
    // Ctrl = Copy, otherwise = Move (default for desktop icons)
    if (grfKeyState & MK_CONTROL) {
        *pdwEffect = DROPEFFECT_COPY;
    } else {
        // Default to MOVE for desktop icons
        *pdwEffect = DROPEFFECT_MOVE;
    }

    OutputDebugStringW(L"[DropTarget] DragEnter\n");
    return S_OK;
}

STDMETHODIMP FenceDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    if (!pdwEffect) {
        return E_INVALIDARG;
    }

    // Update drop effect based on keyboard state
    if (grfKeyState & MK_CONTROL) {
        *pdwEffect = DROPEFFECT_COPY;
    } else {
        *pdwEffect = DROPEFFECT_MOVE;
    }

    return S_OK;
}

STDMETHODIMP FenceDropTarget::DragLeave() {
    OutputDebugStringW(L"[DropTarget] DragLeave\n");
    return S_OK;
}

STDMETHODIMP FenceDropTarget::Drop(IDataObject* pDataObj, DWORD grfKeyState,
                                   POINTL pt, DWORD* pdwEffect) {
    if (!pDataObj || !pdwEffect) {
        return E_INVALIDARG;
    }

    // Extract file paths
    std::vector<std::wstring> files;
    if (!GetDroppedFiles(pDataObj, files)) {
        *pdwEffect = DROPEFFECT_NONE;
        return E_FAIL;
    }

    // Determine if this is a MOVE or COPY operation
    bool isMove = !(grfKeyState & MK_CONTROL);

    // Check if all files are from desktop
    bool allFromDesktop = true;
    for (const auto& file : files) {
        if (!IsDesktopFile(file)) {
            allFromDesktop = false;
            break;
        }
    }

    // CRITICAL: Only use MOVE effect for desktop files
    // For files from other locations, force COPY to avoid data loss
    if (isMove && allFromDesktop) {
        *pdwEffect = DROPEFFECT_MOVE;
        OutputDebugStringW(L"[DropTarget] Drop with MOVE effect (icons will disappear from desktop)\n");
    } else {
        *pdwEffect = DROPEFFECT_COPY;
        OutputDebugStringW(L"[DropTarget] Drop with COPY effect (icons stay on desktop)\n");
    }

    // =========================================================================
    // NEW in v4.0: Use FileManager to MOVE files safely
    // Files are moved to AppData, making desktop icons truly disappear
    // =========================================================================
    if (!widget_->fileManager_) {
        OutputDebugStringW(L"[DropTarget] ERROR: FileManager not initialized!\n");
        *pdwEffect = DROPEFFECT_NONE;
        return E_FAIL;
    }

    int successCount = 0;
    int failCount = 0;

    for (const auto& file : files) {
        OutputDebugStringW((L"[DropTarget] Processing: " + file + L"\n").c_str());

        // Move file to managed folder
        MoveResult result = widget_->fileManager_->MoveToManagedFolder(file, fence_->title);

        if (result.success) {
            // Add to fence using NEW managed path
            widget_->AddIconToFence(fence_, result.newPath);

            wchar_t msg[512];
            swprintf_s(msg, L"[DropTarget] ✓ Moved successfully: %s -> %s\n",
                       file.c_str(), result.newPath.c_str());
            OutputDebugStringW(msg);

            successCount++;
        } else {
            wchar_t msg[512];
            swprintf_s(msg, L"[DropTarget] ✗ Move failed: %s (Error: %s, Code: %d)\n",
                       file.c_str(), result.errorMessage.c_str(), result.errorCode);
            OutputDebugStringW(msg);

            failCount++;
        }
    }

    wchar_t summary[256];
    swprintf_s(summary, L"[DropTarget] Move completed: %d success, %d failed\n",
               successCount, failCount);
    OutputDebugStringW(summary);

    // If at least one file moved successfully, consider it a success
    if (successCount > 0) {
        *pdwEffect = DROPEFFECT_MOVE;
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }

    return S_OK;
}

// ===========================================================================
// Helper Methods
// ===========================================================================

bool FenceDropTarget::GetDroppedFiles(IDataObject* pDataObj, std::vector<std::wstring>& files) {
    files.clear();

    // Request CF_HDROP format
    FORMATETC fmtEtc = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stgMed = {0};

    if (FAILED(pDataObj->GetData(&fmtEtc, &stgMed))) {
        return false;
    }

    // Lock global memory
    HDROP hDrop = static_cast<HDROP>(GlobalLock(stgMed.hGlobal));
    if (!hDrop) {
        ReleaseStgMedium(&stgMed);
        return false;
    }

    // Get file count
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

    // Extract each file path
    for (UINT i = 0; i < fileCount; ++i) {
        wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, i, filePath, MAX_PATH)) {
            files.push_back(filePath);
        }
    }

    GlobalUnlock(stgMed.hGlobal);
    ReleaseStgMedium(&stgMed);

    return !files.empty();
}

bool FenceDropTarget::IsDesktopFile(const std::wstring& filePath) {
    // Get user's desktop path
    wchar_t desktopPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPath))) {
        return false;
    }

    // Check if file is in desktop directory
    if (wcsstr(filePath.c_str(), desktopPath) == filePath.c_str()) {
        return true;
    }

    // Also check Public Desktop
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, desktopPath))) {
        if (wcsstr(filePath.c_str(), desktopPath) == filePath.c_str()) {
            return true;
        }
    }

    return false;
}
