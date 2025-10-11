#pragma once

#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * FileManager - 安全的文件移動管理器
 *
 * 功能：
 * 1. 將桌面文件安全移動到 AppData 管理文件夾
 * 2. 支持崩潰恢復（事務日誌）
 * 3. 避免文件名衝突（UUID前綴）
 * 4. 完整的錯誤處理
 */

// 文件映射信息
struct FileMappingInfo {
    std::wstring originalPath;      // 原始桌面路徑
    std::wstring managedPath;       // 管理文件夾中的路徑
    std::wstring fenceName;         // 所屬柵欄名稱
    std::wstring uuid;              // 唯一標識符
    __int64 movedAt;                // 移動時間戳
};

// 移動操作結果
struct MoveResult {
    bool success;
    std::wstring newPath;           // 新路徑（如果成功）
    std::wstring errorMessage;      // 錯誤信息（如果失敗）
    int errorCode;                  // Windows 錯誤代碼
};

class FileManager {
public:
    FileManager();
    ~FileManager();

    // 初始化（創建目錄、加載映射、恢復未完成操作）
    bool Initialize();

    // 清理
    void Shutdown();

    // ========================================================================
    // 主要操作
    // ========================================================================

    // 移動文件到管理文件夾
    MoveResult MoveToManagedFolder(
        const std::wstring& sourcePath,
        const std::wstring& fenceName);

    // 移動文件回桌面
    MoveResult MoveBackToDesktop(const std::wstring& managedPath);

    // 獲取文件映射信息
    bool GetMappingInfo(const std::wstring& managedPath, FileMappingInfo& info);

    // 檢查文件是否被管理
    bool IsManagedFile(const std::wstring& path);

    // ========================================================================
    // 安全檢查
    // ========================================================================

    // 檢查路徑是否安全（不是系統文件夾）
    static bool IsPathSafe(const std::wstring& path);

    // 檢查是否是桌面文件
    static bool IsDesktopFile(const std::wstring& path);

    // 檢查磁盤空間是否足夠
    bool HasEnoughSpace(const std::wstring& filePath);

    // ========================================================================
    // 工具方法
    // ========================================================================

    // 獲取管理文件夾路徑
    static std::wstring GetManagedFolderPath();

    // 獲取文件大小
    static __int64 GetFileSize(const std::wstring& path);

    // 生成UUID
    static std::wstring GenerateUUID();

    // 從管理路徑提取原始文件名
    static std::wstring ExtractOriginalFileName(const std::wstring& managedPath);

private:
    // 映射表：managedPath -> FileMappingInfo
    std::unordered_map<std::wstring, FileMappingInfo> mappings_;

    // 映射文件路徑
    std::wstring mappingFilePath_;

    // 是否已初始化
    bool initialized_;

    // ========================================================================
    // 內部方法
    // ========================================================================

    // 加載映射文件
    bool LoadMappings();

    // 保存映射文件
    bool SaveMappings();

    // 生成唯一文件名（UUID_原始名）
    std::wstring GenerateUniqueFileName(const std::wstring& originalName);

    // 創建備份
    bool CreateBackup(const std::wstring& sourcePath, std::wstring& backupPath);

    // 刪除備份
    bool RemoveBackup(const std::wstring& backupPath);

    // 執行安全移動（帶備份）
    MoveResult SafeMove(
        const std::wstring& source,
        const std::wstring& destination);

    // 驗證已管理的文件
    void ValidateManagedFiles();

    // 恢復丟失的文件
    bool RecoverLostFile(const std::wstring& managedPath);
};
