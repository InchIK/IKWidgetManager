#include "FileManager.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <rpc.h>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "shlwapi.lib")

FileManager::FileManager()
    : initialized_(false) {
}

FileManager::~FileManager() {
    Shutdown();
}

bool FileManager::Initialize() {
    if (initialized_) {
        return true;
    }

    // 創建管理文件夾
    std::wstring managedFolder = GetManagedFolderPath();
    if (FAILED(SHCreateDirectoryExW(nullptr, managedFolder.c_str(), nullptr))) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
            OutputDebugStringW(L"[FileManager] Failed to create managed folder\n");
            return false;
        }
    }

    // 設置為隱藏
    SetFileAttributesW(managedFolder.c_str(),
                      GetFileAttributesW(managedFolder.c_str()) | FILE_ATTRIBUTE_HIDDEN);

    // 設置映射文件路徑
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        return false;
    }
    mappingFilePath_ = std::wstring(appData) + L"\\FencesWidget\\mappings.txt";

    // 加載現有映射
    LoadMappings();

    // 驗證已管理的文件
    ValidateManagedFiles();

    initialized_ = true;
    OutputDebugStringW(L"[FileManager] Initialized successfully\n");
    return true;
}

void FileManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    // 保存映射
    SaveMappings();

    mappings_.clear();
    initialized_ = false;
}

// ============================================================================
// 主要操作
// ============================================================================

MoveResult FileManager::MoveToManagedFolder(
    const std::wstring& sourcePath,
    const std::wstring& fenceName)
{
    MoveResult result = { false, L"", L"", 0 };

    // 安全檢查
    if (!IsPathSafe(sourcePath)) {
        result.errorMessage = L"路徑不安全，拒絕操作";
        result.errorCode = ERROR_ACCESS_DENIED;
        return result;
    }

    if (!IsDesktopFile(sourcePath)) {
        result.errorMessage = L"只能移動桌面文件";
        result.errorCode = ERROR_INVALID_PARAMETER;
        return result;
    }

    // 檢查源文件是否存在
    if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.errorMessage = L"源文件不存在";
        result.errorCode = ERROR_FILE_NOT_FOUND;
        return result;
    }

    // 檢查磁盤空間
    if (!HasEnoughSpace(sourcePath)) {
        result.errorMessage = L"磁盤空間不足";
        result.errorCode = ERROR_DISK_FULL;
        return result;
    }

    // 使用原始文件名（不添加UUID前綴）
    std::wstring fileName = sourcePath.substr(sourcePath.find_last_of(L"\\\\/") + 1);
    std::wstring destPath = GetManagedFolderPath() + L"\\" + fileName;

    // 創建備份
    std::wstring backupPath;
    if (!CreateBackup(sourcePath, backupPath)) {
        result.errorMessage = L"創建備份失敗";
        result.errorCode = GetLastError();
        return result;
    }

    wchar_t debugMsg[512];
    swprintf_s(debugMsg, L"[FileManager] Moving: %s -> %s\n", sourcePath.c_str(), destPath.c_str());
    OutputDebugStringW(debugMsg);

    // 移動文件
    if (!MoveFileW(sourcePath.c_str(), destPath.c_str())) {
        DWORD error = GetLastError();
        swprintf_s(debugMsg, L"[FileManager] Move failed with error: %d\n", error);
        OutputDebugStringW(debugMsg);

        // 從備份恢復
        MoveFileW(backupPath.c_str(), sourcePath.c_str());
        DeleteFileW(backupPath.c_str());

        result.errorMessage = L"移動文件失敗";
        result.errorCode = error;
        return result;
    }

    // 刪除備份
    DeleteFileW(backupPath.c_str());

    // 記錄映射
    FileMappingInfo info;
    info.originalPath = sourcePath;
    info.managedPath = destPath;
    info.fenceName = fenceName;
    info.uuid = GenerateUUID();  // 生成UUID用於追蹤
    info.movedAt = static_cast<__int64>(time(nullptr));

    mappings_[destPath] = info;
    SaveMappings();

    result.success = true;
    result.newPath = destPath;

    OutputDebugStringW(L"[FileManager] Move completed successfully\n");
    return result;
}

MoveResult FileManager::MoveBackToDesktop(const std::wstring& managedPath) {
    MoveResult result = { false, L"", L"", 0 };

    // 檢查映射是否存在
    auto it = mappings_.find(managedPath);
    if (it == mappings_.end()) {
        result.errorMessage = L"找不到文件映射";
        result.errorCode = ERROR_FILE_NOT_FOUND;
        return result;
    }

    FileMappingInfo& info = it->second;
    std::wstring destPath = info.originalPath;

    // 檢查目標路徑是否被占用
    if (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // 生成新文件名
        std::wstring baseName = destPath.substr(0, destPath.find_last_of(L'.'));
        std::wstring ext = destPath.substr(destPath.find_last_of(L'.'));

        int counter = 1;
        do {
            wchar_t newName[MAX_PATH];
            swprintf_s(newName, L"%s_%d%s", baseName.c_str(), counter++, ext.c_str());
            destPath = newName;
        } while (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES && counter < 100);
    }

    wchar_t debugMsg[512];
    swprintf_s(debugMsg, L"[FileManager] Moving back: %s -> %s\n",
               managedPath.c_str(), destPath.c_str());
    OutputDebugStringW(debugMsg);

    // 移動回桌面
    if (!MoveFileW(managedPath.c_str(), destPath.c_str())) {
        DWORD error = GetLastError();
        result.errorMessage = L"移動文件回桌面失敗";
        result.errorCode = error;
        return result;
    }

    // 移除映射
    mappings_.erase(managedPath);
    SaveMappings();

    result.success = true;
    result.newPath = destPath;

    OutputDebugStringW(L"[FileManager] Moved back successfully\n");
    return result;
}

bool FileManager::GetMappingInfo(const std::wstring& managedPath, FileMappingInfo& info) {
    auto it = mappings_.find(managedPath);
    if (it == mappings_.end()) {
        return false;
    }

    info = it->second;
    return true;
}

bool FileManager::IsManagedFile(const std::wstring& path) {
    return mappings_.find(path) != mappings_.end();
}

// ============================================================================
// 安全檢查
// ============================================================================

bool FileManager::IsPathSafe(const std::wstring& path) {
    // 禁止的路徑前綴
    std::vector<std::wstring> forbidden = {
        L"C:\\Windows\\",
        L"C:\\Program Files\\",
        L"C:\\Program Files (x86)\\",
        L"C:\\ProgramData\\",
        L"C:\\$"
    };

    std::wstring upperPath = path;
    for (auto& c : upperPath) c = towupper(c);

    for (const auto& prefix : forbidden) {
        if (upperPath.find(prefix) == 0) {
            return false;
        }
    }

    return true;
}

bool FileManager::IsDesktopFile(const std::wstring& path) {
    wchar_t desktop[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop))) {
        if (path.find(desktop) == 0) {
            return true;
        }
    }

    wchar_t publicDesktop[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, publicDesktop))) {
        if (path.find(publicDesktop) == 0) {
            return true;
        }
    }

    return false;
}

bool FileManager::HasEnoughSpace(const std::wstring& filePath) {
    __int64 fileSize = GetFileSize(filePath);
    if (fileSize < 0) {
        return false;
    }

    // 獲取目標驅動器
    std::wstring managedFolder = GetManagedFolderPath();
    wchar_t drive[4] = { managedFolder[0], L':', L'\\', L'\0' };

    ULARGE_INTEGER freeBytesAvailable;
    if (!GetDiskFreeSpaceExW(drive, &freeBytesAvailable, nullptr, nullptr)) {
        return false;
    }

    // 要求至少有文件大小的 2 倍空間（用於備份）
    return freeBytesAvailable.QuadPart > static_cast<ULONGLONG>(fileSize * 2);
}

// ============================================================================
// 工具方法
// ============================================================================

std::wstring FileManager::GetManagedFolderPath() {
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    return std::wstring(appData) + L"\\FencesWidget\\ManagedIcons";
}

__int64 FileManager::GetFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return -1;
    }

    LARGE_INTEGER size;
    size.HighPart = fileInfo.nFileSizeHigh;
    size.LowPart = fileInfo.nFileSizeLow;
    return size.QuadPart;
}

std::wstring FileManager::GenerateUUID() {
    UUID uuid;
    UuidCreate(&uuid);

    wchar_t* uuidStr;
    UuidToStringW(&uuid, reinterpret_cast<RPC_WSTR*>(&uuidStr));

    std::wstring result(uuidStr);
    RpcStringFreeW(reinterpret_cast<RPC_WSTR*>(&uuidStr));

    return result;
}

std::wstring FileManager::ExtractOriginalFileName(const std::wstring& managedPath) {
    std::wstring fileName = managedPath.substr(managedPath.find_last_of(L"\\\\/") + 1);
    size_t underscorePos = fileName.find(L'_');
    if (underscorePos != std::wstring::npos) {
        return fileName.substr(underscorePos + 1);
    }
    return fileName;
}

// ============================================================================
// 內部方法
// ============================================================================

bool FileManager::LoadMappings() {
    std::wifstream file(mappingFilePath_);
    if (!file.is_open()) {
        return true;  // 文件不存在是正常的
    }

    std::wstring line;
    while (std::getline(file, line)) {
        // 格式: managedPath|originalPath|fenceName|uuid|timestamp
        std::wstringstream ss(line);
        std::wstring managedPath, originalPath, fenceName, uuid, timestampStr;

        if (std::getline(ss, managedPath, L'|') &&
            std::getline(ss, originalPath, L'|') &&
            std::getline(ss, fenceName, L'|') &&
            std::getline(ss, uuid, L'|') &&
            std::getline(ss, timestampStr, L'|')) {

            FileMappingInfo info;
            info.managedPath = managedPath;
            info.originalPath = originalPath;
            info.fenceName = fenceName;
            info.uuid = uuid;
            info.movedAt = _wtoi64(timestampStr.c_str());

            mappings_[managedPath] = info;
        }
    }

    file.close();

    wchar_t msg[256];
    swprintf_s(msg, L"[FileManager] Loaded %zu mappings\n", mappings_.size());
    OutputDebugStringW(msg);

    return true;
}

bool FileManager::SaveMappings() {
    std::wofstream file(mappingFilePath_);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& [managedPath, info] : mappings_) {
        file << info.managedPath << L"|"
             << info.originalPath << L"|"
             << info.fenceName << L"|"
             << info.uuid << L"|"
             << info.movedAt << L"\n";
    }

    file.close();
    return true;
}

std::wstring FileManager::GenerateUniqueFileName(const std::wstring& originalName) {
    std::wstring uuid = GenerateUUID();
    return uuid + L"_" + originalName;
}

bool FileManager::CreateBackup(const std::wstring& sourcePath, std::wstring& backupPath) {
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);

    std::wstring backupFolder = std::wstring(appData) + L"\\FencesWidget\\Backup";
    SHCreateDirectoryExW(nullptr, backupFolder.c_str(), nullptr);

    std::wstring fileName = sourcePath.substr(sourcePath.find_last_of(L"\\\\/") + 1);
    backupPath = backupFolder + L"\\" + GenerateUUID() + L"_" + fileName;

    return CopyFileW(sourcePath.c_str(), backupPath.c_str(), FALSE) != 0;
}

bool FileManager::RemoveBackup(const std::wstring& backupPath) {
    return DeleteFileW(backupPath.c_str()) != 0;
}

MoveResult FileManager::SafeMove(
    const std::wstring& source,
    const std::wstring& destination)
{
    MoveResult result = { false, L"", L"", 0 };

    // 創建備份
    std::wstring backupPath;
    if (!CreateBackup(source, backupPath)) {
        result.errorMessage = L"創建備份失敗";
        return result;
    }

    // 移動文件
    if (!MoveFileW(source.c_str(), destination.c_str())) {
        // 失敗，從備份恢復
        MoveFileW(backupPath.c_str(), source.c_str());
        DeleteFileW(backupPath.c_str());

        result.errorMessage = L"移動失敗";
        result.errorCode = GetLastError();
        return result;
    }

    // 成功，刪除備份
    DeleteFileW(backupPath.c_str());

    result.success = true;
    result.newPath = destination;
    return result;
}

void FileManager::ValidateManagedFiles() {
    std::vector<std::wstring> toRemove;

    for (const auto& [managedPath, info] : mappings_) {
        if (GetFileAttributesW(managedPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            wchar_t msg[512];
            swprintf_s(msg, L"[FileManager] WARNING: Managed file missing: %s\n",
                       managedPath.c_str());
            OutputDebugStringW(msg);

            toRemove.push_back(managedPath);
        }
    }

    // 移除丟失的文件映射
    for (const auto& path : toRemove) {
        mappings_.erase(path);
    }

    if (!toRemove.empty()) {
        SaveMappings();
    }
}

bool FileManager::RecoverLostFile(const std::wstring& managedPath) {
    // TODO: 實現從備份恢復的邏輯
    return false;
}
