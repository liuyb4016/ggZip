#include "PreviewSession.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/ArchivePath.h"
#include "ggzip/SecureDirectory.h"
#include <QDir>
#include <QFileInfo>
#include <qt_windows.h>
#include <algorithm>
#include <vector>

namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (valid()) CloseHandle(value); }
    bool valid() const { return value != INVALID_HANDLE_VALUE; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& first,
                  const BY_HANDLE_FILE_INFORMATION& second) {
    return first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh &&
           first.nFileIndexLow == second.nFileIndexLow;
}

Handle openAttributes(const QString& path, DWORD access = FILE_READ_ATTRIBUTES,
                      DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) {
    return Handle(CreateFileW(ggzip::win32Path(path.toStdWString()).c_str(), access, sharing, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
}

bool information(HANDLE handle, BY_HANDLE_FILE_INFORMATION& result) {
    return handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &result) &&
           !(result.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

struct OwnedDirectory {
    std::unique_ptr<ggzip::DirectoryLock> lock;
    QDir directory;
    BY_HANDLE_FILE_INFORMATION identity{};

    explicit OwnedDirectory(const QString& pattern)
        : directory(QString::fromStdWString(ggzip::createStagingDirectory(
            QFileInfo(pattern).absolutePath().toStdWString(),
            QFileInfo(pattern).fileName().chopped(6).toStdWString(), lock).wstring())) {
        const Handle handle = openAttributes(directory.path());
        if (!information(handle.value, identity) ||
            !(identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw ggzip::ArchiveError(ggzip::diagnostic("预览临时目录状态不安全"));
    }

    bool unchanged() const {
        if (!lock || !lock->intact()) return false;
        const Handle handle = openAttributes(directory.path());
        BY_HANDLE_FILE_INFORMATION current{};
        return information(handle.value, current) && sameIdentity(identity, current) &&
               (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    ~OwnedDirectory() {
        if (!unchanged()) return;
        lock.reset();
        const Handle handle = openAttributes(directory.path(), DELETE | FILE_READ_ATTRIBUTES,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE);
        BY_HANDLE_FILE_INFORMATION current{};
        if (information(handle.value, current) && sameIdentity(identity, current) &&
            (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            FILE_DISPOSITION_INFO disposition{ TRUE };
            SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition));
        }
    }
};

struct PreviewFile {
    OwnedDirectory directory;
    QString path;
    BY_HANDLE_FILE_INFORMATION identity{};
    bool created = false;

    PreviewFile(const QString& root, const QString& name)
        : directory(QDir(root).filePath("entry-XXXXXX")),
          path(directory.directory.filePath(name)) {}

    bool unchanged() const {
        if (!directory.unchanged()) return false;
        const Handle handle = openAttributes(path);
        BY_HANDLE_FILE_INFORMATION current{};
        return information(handle.value, current) && sameIdentity(identity, current) &&
               !(current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
               current.nNumberOfLinks == 1;
    }

    ~PreviewFile() {
        if (!created || !directory.unchanged()) return;
        const Handle handle = openAttributes(path, DELETE | FILE_READ_ATTRIBUTES,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE);
        BY_HANDLE_FILE_INFORMATION current{};
        if (!information(handle.value, current) || !sameIdentity(identity, current) ||
            current.nNumberOfLinks != 1 || (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return;
        FILE_DISPOSITION_INFO disposition{ TRUE };
        SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition));
    }
};

}

struct PreviewSession::Impl {
    OwnedDirectory root;
    std::vector<std::unique_ptr<PreviewFile>> files;

    explicit Impl(const QString& parent)
        : root(QDir(parent.isEmpty() ? QDir::tempPath() : parent).filePath("ggZip-preview-XXXXXX")) {}
};

PreviewSession::PreviewSession(const QString& temporaryParent)
    : impl_(std::make_unique<Impl>(temporaryParent)) {}

PreviewSession::~PreviewSession() = default;

QString PreviewSession::rootPath() const {
    return impl_->root.directory.path();
}

QString PreviewSession::extract(const std::wstring& member, const Reader& reader,
                               std::uint64_t maximumBytes) {
    const std::wstring normalized = ggzip::validatedPreviewPath(member);
    if (!reader || !impl_->root.unchanged())
        throw ggzip::ArchiveError(ggzip::diagnostic("预览临时目录不可用"));
    const QString name = QString::fromStdWString(normalized).section('/', -1);
    auto file = std::make_unique<PreviewFile>(rootPath(), name);
    if (!impl_->root.unchanged() || !file->directory.unchanged())
        throw ggzip::ArchiveError(ggzip::diagnostic("预览临时目录发生变化"));
    {
        const Handle output(CreateFileW(ggzip::win32Path(file->path.toStdWString()).c_str(),
            GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!output.valid() || !information(output.value, file->identity))
            throw ggzip::ArchiveError(ggzip::diagnostic("无法创建安全的预览文件"));
        file->created = true;
        std::uint64_t written = 0;
        reader([&](std::string_view chunk) {
            if (chunk.size() > maximumBytes - written)
                throw ggzip::ArchiveError(ggzip::diagnostic("预览文件超过大小限制"));
            std::size_t offset = 0;
            while (offset < chunk.size()) {
                const DWORD amount = static_cast<DWORD>(std::min<std::size_t>(chunk.size() - offset, 65536));
                DWORD completed = 0;
                if (!WriteFile(output.value, chunk.data() + offset, amount, &completed, nullptr) || !completed)
                    throw ggzip::ArchiveError(ggzip::diagnostic("写入预览文件失败"));
                offset += completed;
            }
            written += chunk.size();
        });
        if (!FlushFileBuffers(output.value))
            throw ggzip::ArchiveError(ggzip::diagnostic("保存预览文件失败"));
    }
    if (!impl_->root.unchanged() || !file->unchanged())
        throw ggzip::ArchiveError(ggzip::diagnostic("预览文件发生变化或不是普通文件"));
    const QString result = file->path;
    impl_->files.push_back(std::move(file));
    return result;
}

bool PreviewSession::discard(const QString& filePath) {
    const auto found = std::find_if(impl_->files.begin(), impl_->files.end(),
        [&](const auto& file) { return file->path == filePath; });
    if (found == impl_->files.end()) return false;
    impl_->files.erase(found);
    return true;
}
