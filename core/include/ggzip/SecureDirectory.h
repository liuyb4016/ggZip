#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace ggzip {

std::wstring win32Path(const std::filesystem::path& path);

struct FileSystemCapabilities {
    std::wstring volume;
    bool persistentAcls = false;
    bool namedStreams = false;
};

FileSystemCapabilities fileSystemCapabilities(const std::filesystem::path& path);
bool sameVolume(const std::filesystem::path& first, const std::filesystem::path& second);

class DirectoryLock {
public:
    explicit DirectoryLock(const std::filesystem::path& path, bool create = false);
    ~DirectoryLock();
    void requireWritable() const;
    void renameFile(void* sourceHandle, const std::wstring& name) const;
    bool intact() const;
    std::wstring volume() const;
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::filesystem::path createPrivateDirectory(const std::filesystem::path& parent, const std::wstring& prefix,
                                            std::unique_ptr<DirectoryLock>& lock);
std::filesystem::path createStagingDirectory(const std::filesystem::path& parent, const std::wstring& prefix,
                                            std::unique_ptr<DirectoryLock>& lock);
std::filesystem::path createPublicationDirectory(const std::filesystem::path& parent, const std::wstring& prefix,
                                                std::unique_ptr<DirectoryLock>& lock);

}
