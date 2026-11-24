#include "PreviewSession.h"
#include "preview_fixtures.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/EngineFactory.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <winioctl.h>
#include <aclapi.h>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
}

bool throws(const std::function<void()>& operation) {
    try { operation(); }
    catch (const std::exception&) { return true; }
    return false;
}

bool privateAcl(const QString& path) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    const DWORD error = GetNamedSecurityInfoW(const_cast<wchar_t*>(path.toStdWString().c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &descriptor);
    if (error != ERROR_SUCCESS || !dacl || !owner) { if (descriptor) LocalFree(descriptor); return false; }
    bool privatePermissions = dacl->AceCount == 2;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* entry = nullptr;
        if (!GetAce(dacl, index, &entry)) { privatePermissions = false; break; }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
        const auto sid = const_cast<DWORD*>(&ace->SidStart);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (!EqualSid(sid, owner) && !IsWellKnownSid(sid, WinLocalSystemSid))) privatePermissions = false;
    }
    LocalFree(descriptor);
    return privatePermissions;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Unable to create test file");
}

QString extract(PreviewSession& session, const char* bytes) {
    return session.extract(L"nested/same.txt", [&](const ggzip::DataFn& sink) { sink(bytes); });
}

bool junction(const QString& path, const QString& target) {
    if (!QDir().mkdir(path)) return false;
    const std::wstring print = QDir::toNativeSeparators(target).toStdWString();
    const std::wstring substitute = L"\\??\\" + print;
    struct Header {
        DWORD tag;
        WORD length;
        WORD reserved;
        WORD substituteOffset;
        WORD substituteLength;
        WORD printOffset;
        WORD printLength;
    };
    const std::size_t stringBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
    Header header{};
    header.tag = IO_REPARSE_TAG_MOUNT_POINT;
    header.length = static_cast<WORD>(sizeof(Header) - 8 + stringBytes);
    header.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    header.printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    header.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    std::vector<unsigned char> bytes(sizeof(Header) + stringBytes, 0);
    std::memcpy(bytes.data(), &header, sizeof(Header));
    std::memcpy(bytes.data() + sizeof(Header), substitute.c_str(),
                (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(bytes.data() + sizeof(Header) + header.printOffset,
                print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    const HANDLE handle = CreateFileW(path.toStdWString().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    const bool result = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT,
        bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    return result;
}

}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir parent;
        if (!parent.isValid()) throw std::runtime_error("Test parent directory unavailable");
        const QString legacy = parent.filePath("ggZip_preview");
        QDir().mkdir(legacy);
        writeFile(QDir(legacy).filePath("keep.txt"), "LEGACY");
        auto first = std::make_unique<PreviewSession>(parent.path());
        auto second = std::make_unique<PreviewSession>(parent.path());
        const QString firstRoot = first->rootPath();
        const QString secondRoot = second->rootPath();
        check(firstRoot != secondRoot, "sessions use distinct roots");
        check(privateAcl(firstRoot) && privateAcl(secondRoot), "preview roots grant access only to their owner and SYSTEM");
        const QString firstFile = extract(*first, "FIRST");
        const QString secondFile = extract(*second, "SECOND");
        check(privateAcl(firstFile) && privateAcl(secondFile), "plaintext preview files retain private inherited permissions");
        check(firstFile != secondFile && readFile(firstFile) == "FIRST" && readFile(secondFile) == "SECOND",
              "same member names are isolated across instances");
        first.reset();
        check(!QFileInfo::exists(firstRoot) && readFile(secondFile) == "SECOND",
              "closing one session only removes its own files");
        check(readFile(QDir(legacy).filePath("keep.txt")) == "LEGACY",
              "legacy shared cache is never deleted");
        const QString nextFile = extract(*second, "NEXT");
        check(nextFile != secondFile && readFile(secondFile) == "SECOND",
              "each preview uses a fresh operation directory");
        const auto beforeFailure = QDir(secondRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
        check(throws([&] {
            second->extract(L"nested/same.txt", [](const ggzip::DataFn& sink) {
                sink("PARTIAL");
                throw std::runtime_error("read failed");
            });
        }), "failed reader cannot publish a preview");
        check(QDir(secondRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == beforeFailure &&
              readFile(secondFile) == "SECOND", "failed preview removes partial data and preserves old operations");
        check(throws([&] {
            second->extract(L"too-large.txt", [](const ggzip::DataFn& sink) { sink("TOO LARGE"); }, 4);
        }), "actual streamed bytes enforce the preview size bound");
        check(QDir(secondRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == beforeFailure,
              "size limit failure leaves no operation directory");
        bool called = false;
        check(throws([&] {
            second->extract(L"../outside.txt", [&](const ggzip::DataFn&) { called = true; });
        }) && !called, "unsafe path never calls the reader");
        const QString empty = second->extract(L"empty.txt", [](const ggzip::DataFn&) {});
        check(QFileInfo::exists(empty) && QFileInfo(empty).size() == 0, "empty file is a valid isolated output");
        const QString binary = second->extract(L"中文 文件.bin",
            [](const ggzip::DataFn& sink) { sink(std::string_view("a\0b", 3)); });
        check(readFile(binary) == QByteArray("a\0b", 3), "binary bytes and Unicode leaf names preserved");
        check(!second->discard(firstFile), "foreign output cannot be discarded by another session");
        check(second->discard(empty) && !QFileInfo::exists(empty), "owned output and empty operation directory removed");
        second.reset();
        check(!QFileInfo::exists(secondRoot), "ordinary session cleanup removes only its empty root");

        const QString outside = parent.filePath("outside.txt");
        writeFile(outside, "OUTSIDE");
        {
            PreviewSession changed(parent.path());
            const QString file = extract(changed, "ORIGINAL");
            const QString saved = parent.filePath("original.txt");
            check(QFile::rename(file, saved), "prepare replaced preview output");
            check(CreateHardLinkW(file.toStdWString().c_str(), outside.toStdWString().c_str(), nullptr) != 0,
                  "prepare foreign hard link");
            changed.discard(file);
            check(QFileInfo::exists(file) && readFile(outside) == "OUTSIDE",
                  "cleanup rejects replaced files and foreign hard links");
        }
        {
            auto changed = std::make_unique<PreviewSession>(parent.path());
            const QString root = changed->rootPath();
            const QString file = extract(*changed, "MOVED");
            const QString moved = root + "-moved";
            check(!QDir().rename(root, moved), "active preview session rejects directory replacement");
            check(readFile(file) == "MOVED", "directory locking preserves active preview contents");
            changed.reset();
            check(!QFileInfo::exists(root) && !QFileInfo::exists(moved),
                  "directory locks are released before owned session cleanup");
        }
        {
            auto linked = std::make_unique<PreviewSession>(parent.path());
            const QString root = linked->rootPath();
            extract(*linked, "OWNED");
            const QString target = parent.filePath("junction-target");
            QDir().mkdir(target);
            const QString marker = QDir(target).filePath("marker.txt");
            writeFile(marker, "JUNCTION TARGET");
            const QString link = QDir(root).filePath("foreign-junction");
            if (junction(link, target)) {
                linked.reset();
                check(readFile(marker) == "JUNCTION TARGET" && QFileInfo::exists(link),
                      "cleanup never recursively follows foreign junctions");
                check(RemoveDirectoryW(link.toStdWString().c_str()) != 0, "remove test junction without traversing target");
                QDir().rmdir(root);
            } else {
                std::printf("SKIP: NTFS junction creation unavailable\n");
            }
        }

        auto engine = ggzip::createEngine();
        if (!engine->available()) throw std::runtime_error("Pinned engine required for integration tests");
        const std::filesystem::path archive(parent.filePath("preview.zip").toStdWString());
        previewtest::zip(archive, {
            {"safe.txt", "SAFE"}, {"../outside.txt", "BAD"}, {"link.txt", "../../outside.txt", 0xa1ff0000}
        });
        PreviewSession integrated(parent.path());
        std::atomic_bool cancel{ false };
        auto preview = [&](const std::wstring& member, const std::wstring& source) {
            return integrated.extract(member, [&](const ggzip::DataFn& sink) {
                engine->extractToStream(source, member, {}, sink, cancel);
            });
        };
        const QString safe = preview(L"safe.txt", archive.wstring());
        check(readFile(safe) == "SAFE" && readFile(outside) == "OUTSIDE",
              "real streaming engine writes only the owned flat output");
        check(throws([&] { preview(L"link.txt", archive.wstring()); }),
              "archive link rejected before a preview is published");
        const auto damaged = std::filesystem::path(parent.filePath("damaged.zip").toStdWString());
        previewtest::zip(damaged, { {"safe.txt", "BAD CRC", 0x81a40000, true} });
        const auto existing = QDir(integrated.rootPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
        check(throws([&] { preview(L"safe.txt", damaged.wstring()); }),
              "real CRC failure does not return a cached output");
        check(readFile(safe) == "SAFE" &&
              QDir(integrated.rootPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == existing,
              "failed real extraction removes partial output without stale-cache fallback");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
