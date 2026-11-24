#include "preview_fixtures.h"
#include "ggzip/SecureDirectory.h"
#include "ggzip/EngineFactory.h"
#include "FileTransaction.h"
#include <aclapi.h>
#include <sddl.h>
#include <functional>
#include <winioctl.h>
#include <cstring>

namespace fs = std::filesystem;
namespace {
int checks = 0;
int failures = 0;
void check(bool result, const char* label) {
    ++checks;
    if (!result) ++failures;
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
}
bool throws(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
bool privateAcl(const fs::path& path, bool protectedRoot) {
    const HANDLE handle = CreateFileW(ggzip::win32Path(path).c_str(), READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    PSECURITY_DESCRIPTOR security = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    const auto error = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &security);
    CloseHandle(handle);
    if (error != ERROR_SUCCESS || !dacl || !owner) { if (security) LocalFree(security); return false; }
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    bool result = GetSecurityDescriptorControl(security, &control, &revision) != 0 &&
        (!protectedRoot || (control & SE_DACL_PROTECTED)) && dacl->AceCount == 2;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* entry = nullptr;
        if (!GetAce(dacl, index, &entry)) { result = false; break; }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
        const auto sid = const_cast<DWORD*>(&ace->SidStart);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (!EqualSid(sid, owner) && !IsWellKnownSid(sid, WinLocalSystemSid))) result = false;
    }
    LocalFree(security);
    return result;
}

bool setJunction(const fs::path& path, const fs::path& target) {
    const auto print = fs::absolute(target).wstring();
    const auto substitute = L"\\??\\" + print;
    struct Header {
        DWORD tag;
        WORD length;
        WORD reserved;
        WORD substituteOffset;
        WORD substituteLength;
        WORD printOffset;
        WORD printLength;
    };
    Header header{};
    header.tag = IO_REPARSE_TAG_MOUNT_POINT;
    const auto stringBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
    header.length = static_cast<WORD>(sizeof(Header) - 8 + stringBytes);
    header.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    header.printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    header.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    std::vector<unsigned char> bytes(sizeof(Header) + stringBytes, 0);
    std::memcpy(bytes.data(), &header, sizeof(Header));
    std::memcpy(bytes.data() + sizeof(Header), substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(bytes.data() + sizeof(Header) + header.printOffset, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    const HANDLE handle = CreateFileW(ggzip::win32Path(path).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    const auto changed = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(),
        static_cast<DWORD>(bytes.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    return changed != 0;
}
}

int wmain() {
    try {
        previewtest::Directory fixture;
        const auto shared = fixture.path / "shared";
        fs::create_directory(shared);
        PSECURITY_DESCRIPTOR permission = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;OICI;FA;;;WD)", SDDL_REVISION_1,
            &permission, nullptr)) throw std::runtime_error("Cannot configure isolated shared-parent fixture");
        const auto set = SetFileSecurityW(shared.c_str(), DACL_SECURITY_INFORMATION, permission);
        LocalFree(permission);
        check(set != 0, "isolated parent intentionally grants inherited Everyone access");
        fs::path root;
        std::atomic_bool cancel{false};
        {
            ggzip::files::Workspace workspace(shared);
            root = workspace.root;
            check(privateAcl(root, true), "workspace blocks inherited broad access at creation");
            const auto secret = workspace.write(L"nested/secret.txt", "DECRYPTED SECRET");
            check(privateAcl(secret, false), "new plaintext files inherit only user and SYSTEM grants");
            check(!MoveFileExW(root.c_str(), (shared / "replaced-root").c_str(), 0),
                  "active workspace cannot be renamed by another handle");
            const HANDLE mutation = CreateFileW(ggzip::win32Path(root).c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            check(mutation != INVALID_HANDLE_VALUE, "private workspace allows owner metadata operations");
            if (mutation != INVALID_HANDLE_VALUE) CloseHandle(mutation);
            check(throws([&] { workspace.write(L"../escape.txt", "BAD"); }) &&
                  !fs::exists(shared / "escape.txt"), "workspace write rejects relative escapes");
            const auto input = fixture.path / "source.txt";
            previewtest::write(input, "SOURCE");
            check(throws([&] { workspace.snapshot(input, L"../outside.txt", cancel); }) &&
                  !fs::exists(shared / "outside.txt"), "snapshot rejects escaping destinations before writing");
        }
        check(!fs::exists(root), "private workspace locks do not prevent owned cleanup");
        const auto tree = shared / "nested-tree";
        {
            ggzip::DirectoryLock directories(tree / "child", true);
            check(fs::is_directory(tree / "child"), "directory creation and validation hold ancestor locks");
            check(!MoveFileExW(tree.c_str(), (shared / "moved-tree").c_str(), 0),
                  "destination ancestors cannot be exchanged during commit");
        }
        check(MoveFileExW(tree.c_str(), (shared / "moved-tree").c_str(), 0) != 0,
              "ordinary directory operations resume when the lock is released");
        const auto redirect = shared / "redirect";
        const auto foreign = fixture.path / "foreign";
        fs::create_directory(redirect);
        fs::create_directory(foreign);
        previewtest::write(foreign / "keep.txt", "FOREIGN");
        const auto staged = fixture.path / "staged.txt";
        previewtest::write(staged, "PRIVATE");
        {
            ggzip::DirectoryLock target(redirect);
            const HANDLE input = CreateFileW(staged.c_str(), DELETE | GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            check(input != INVALID_HANDLE_VALUE, "open a staged file for handle-relative publication");
            const bool changed = setJunction(redirect, foreign);
            check(changed, "prepare in-place directory redirection in an isolated fixture");
            check(!target.intact() && throws([&] { target.renameFile(input, L"new.txt"); }) &&
                  !fs::exists(foreign / "new.txt") && fs::exists(staged),
                  "in-place reparse mutation is detected without publishing into a foreign directory");
            if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        }
        check(RemoveDirectoryW(redirect.c_str()) != 0, "remove test junction without traversing its target");
        check(fs::exists(foreign / "keep.txt"), "reparse cleanup preserves foreign directory contents");
        const auto pinnedOutput = shared / "pinned-output";
        const auto movedOutput = shared / "moved-pinned-output";
        {
            ggzip::files::Workspace workspace(shared);
            const auto firstPin = workspace.directoryLock(pinnedOutput, true);
            check(firstPin == workspace.directoryLock(pinnedOutput),
                  "workspace reuses the same pinned directory for repeated commits");
            const auto stagedFile = workspace.write(L"cached-source.txt", "CACHED DATA");
            ggzip::files::commitFamily(workspace, {{stagedFile, pinnedOutput / "result.txt"}}, {}, {});
            check(!MoveFileExW(pinnedOutput.c_str(), movedOutput.c_str(), 0),
                  "published destination directories stay pinned until the operation ends");
            DWORD securityBytes = 0;
            GetFileSecurityW(pinnedOutput.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &securityBytes);
            std::vector<unsigned char> originalSecurity(securityBytes);
            if (!securityBytes || !GetFileSecurityW(pinnedOutput.c_str(), DACL_SECURITY_INFORMATION,
                originalSecurity.data(), securityBytes, &securityBytes))
                throw std::runtime_error("Cannot capture cached directory permissions");
            PSECURITY_DESCRIPTOR denied = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(D;;0x00000156;;;WD)(A;;FA;;;OW)(A;;FA;;;SY)", SDDL_REVISION_1, &denied, nullptr))
                throw std::runtime_error("Cannot construct cached permission fixture");
            const bool changed = SetFileSecurityW(pinnedOutput.c_str(), DACL_SECURITY_INFORMATION, denied) != 0;
            LocalFree(denied);
            const auto replacement = workspace.write(L"cached-denied-source.txt", "REPLACEMENT");
            const auto oldState = ggzip::files::identity(pinnedOutput / "result.txt");
            const bool rejected = changed && throws([&] {
                ggzip::files::commitFamily(workspace, {{replacement, pinnedOutput / "result.txt"}},
                    {pinnedOutput / "result.txt"}, {oldState});
            }) && ggzip::files::same(ggzip::files::identity(pinnedOutput / "result.txt"), oldState);
            check(SetFileSecurityW(pinnedOutput.c_str(), DACL_SECURITY_INFORMATION, originalSecurity.data()) != 0,
                  "restore isolated cached directory permissions");
            check(rejected, "cached directory reuse checks current write permissions before moving old data");
        }
        check(MoveFileExW(pinnedOutput.c_str(), movedOutput.c_str(), 0) != 0 &&
              fs::exists(movedOutput / "result.txt"), "cached directory pins release without deleting published files");
        const auto cachedRedirect = shared / "cached-redirect";
        {
            ggzip::files::Workspace workspace(shared);
            workspace.directoryLock(cachedRedirect, true);
            const auto stagedFile = workspace.write(L"cached-reparse-source.txt", "PRIVATE");
            check(setJunction(cachedRedirect, foreign), "prepare in-place cached directory redirection");
            check(throws([&] {
                ggzip::files::commitFamily(workspace, {{stagedFile, cachedRedirect / "cached-new.txt"}}, {}, {});
            }) && !fs::exists(foreign / "cached-new.txt") && fs::exists(stagedFile),
                  "cached directory reuse still rejects in-place reparse changes");
        }
        check(RemoveDirectoryW(cachedRedirect.c_str()) != 0 && fs::exists(foreign / "keep.txt"),
              "cached reparse fixture is removed without traversing foreign contents");
        {
            ggzip::files::Workspace workspace(shared);
            fs::path deep = workspace.root;
            for (int index = 0; index < 34; ++index) deep /= L"d";
            const auto deepFirst = workspace.directoryLock(deep, true);
            const auto deepSecond = workspace.directoryLock(deep);
            workspace.adopt(deep);
            check(deepFirst != deepSecond, "deep paths are not retained in the bounded directory cache");
        }
        {
            ggzip::files::Workspace workspace(shared);
            std::shared_ptr<ggzip::DirectoryLock> last;
            fs::path lastPath;
            for (int index = 0; index < 65; ++index) {
                lastPath = workspace.root / (L"cache-" + std::to_wstring(index));
                last = workspace.directoryLock(lastPath, true);
                workspace.adopt(lastPath);
            }
            check(last != workspace.directoryLock(lastPath), "directory cache stops retaining entries at its fixed capacity");
        }
        auto engine = ggzip::createEngine();
        const auto encrypted = fixture.path / "encrypted.7z";
        const auto source = fixture.path / "private.txt";
        previewtest::write(source, "CONFIDENTIAL");
        ggzip::CreateOptions creation;
        creation.password = L"test-password";
        engine->create(encrypted.wstring(), {source.wstring()}, creation, {}, cancel);
        const auto output = shared / "published";
        ggzip::ExtractOptions extraction;
        extraction.password = creation.password;
        bool sawPrivate = false;
        engine->extract(encrypted.wstring(), output.wstring(), {}, extraction,
            [&](const ggzip::Progress&) {
                for (const auto& item : fs::directory_iterator(shared))
                    if (item.path().filename().wstring().rfind(L"ggZip-job-", 0) == 0)
                        sawPrivate = privateAcl(item.path(), true);
            }, cancel);
        check(sawPrivate, "encrypted extraction uses protected staging under a shared destination");
        check(privateAcl(output / "private.txt", false), "published extraction does not broaden plaintext access");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
