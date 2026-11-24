#include "preview_fixtures.h"
#include "FileTransaction.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/SourceMark.h"
#include <functional>
#include <iterator>
#include <thread>
#include <shlobj.h>
#include <algorithm>

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
std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        previewtest::Directory fixture;
        fs::path destination = fixture.path;
        bool diskPressure = false;
        if ((argc == 3 || argc == 4) && std::wstring(argv[1]) == L"--target-root") {
            destination = fs::absolute(argv[2]);
            if (argc == 4) {
                if (std::wstring(argv[3]) != L"--disk-pressure") throw std::runtime_error("Unexpected test argument");
                diskPressure = true;
            }
        }
        else if (argc != 1) throw std::runtime_error("Expected --target-root <existing isolated volume root>");
        ggzip::DirectoryLock targetRoot(destination);
        ggzip::files::Workspace target(destination, ggzip::files::Workspace::Purpose::Publication);
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        fs::path staging = fs::path(executable).parent_path();
        if (ggzip::sameVolume(staging, target.root)) {
            PWSTR local = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)) && local)
                staging = local;
            if (local) CoTaskMemFree(local);
        }
        if (ggzip::sameVolume(staging, target.root)) {
            std::printf("SKIP: no distinct volume; run with --target-root on an isolated second volume\n");
            return 77;
        }
        ggzip::files::Workspace source(staging);
        std::atomic_bool cancel{false};
        check(ggzip::fileSystemCapabilities(source.root).persistentAcls,
              "cross-volume source staging remains on a protected-ACL filesystem");
        const auto first = source.write(L"first.txt", "VERIFIED FIRST");
        const auto firstState = ggzip::files::identity(first);
        const auto output = target.root / "output.txt";
        ggzip::files::commitFamily(source, {{first, output}}, {}, {}, {firstState}, &cancel);
        target.adopt(output);
        check(bytes(output) == "VERIFIED FIRST" && fs::exists(first),
              "cross-volume publication copies verified bytes without moving private source data");
        const auto second = source.write(L"second.txt", "REPLACEMENT");
        auto expected = ggzip::files::identity(output);
        ggzip::files::commitFamily(source, {{second, output}}, {output}, {expected}, {}, &cancel);
        target.adoptOutput(output);
        check(bytes(output) == "REPLACEMENT", "authorized cross-volume overwrite commits successfully");
        const auto foreign = target.write(L"foreign.txt", "FOREIGN");
        expected = ggzip::files::identity(output);
        check(throws([&] {
            ggzip::files::commitFamily(source, {{first, output}, {second, foreign}},
                {output}, {expected}, {}, &cancel);
        }) && bytes(output) == "REPLACEMENT" && bytes(foreign) == "FOREIGN",
              "a later family collision rolls back earlier publication and restores the old target");
        previewtest::write(first, "CHANGED");
        check(throws([&] {
            ggzip::files::commitFamily(source, {{first, output}}, {output}, {expected}, {firstState}, &cancel);
        }) && bytes(output) == "REPLACEMENT", "changed verified source is rejected before moving old files");
        const HANDLE busy = CreateFileW(output.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(busy != INVALID_HANDLE_VALUE && throws([&] {
            ggzip::files::commitFamily(source, {{second, output}}, {output}, {expected}, {}, &cancel);
        }) && bytes(output) == "REPLACEMENT", "occupied cross-volume target preserves its original content");
        if (busy != INVALID_HANDLE_VALUE) CloseHandle(busy);
        cancel.store(true);
        check(throws([&] {
            ggzip::files::commitFamily(source, {{second, output}}, {output}, {expected}, {}, &cancel);
        }) && bytes(output) == "REPLACEMENT", "pre-cancelled cross-volume transaction never replaces old data");
        cancel.store(false);
        const auto large = source.write(L"large.bin", std::string(32 * 1024 * 1024, 'B'));
        const auto started = GetTickCount64();
        std::thread stop([&] { Sleep(5); cancel.store(true); });
        const bool stopped = throws([&] {
            ggzip::files::commitFamily(source, {{large, output}}, {output}, {expected}, {}, &cancel);
        });
        stop.join();
        check(stopped && bytes(output) == "REPLACEMENT" && GetTickCount64() - started < 2000,
              "cancellation during a large cross-volume copy stops promptly and keeps old data");
        cancel.store(false);
        const auto marked = source.write(L"marked.txt", "DOWNLOADED");
        ggzip::applySourceZone(marked.wstring(), 3);
        const auto markedOutput = target.root / "marked.txt";
        const auto capabilities = ggzip::fileSystemCapabilities(target.root);
        if (diskPressure) {
            wchar_t runner[32]{};
            wchar_t disposable[32768]{};
            GetEnvironmentVariableW(L"GITHUB_ACTIONS", runner, 32);
            GetEnvironmentVariableW(L"GGZIP_TEST_DISPOSABLE_VOLUME", disposable, 32768);
            if (_wcsicmp(runner, L"true") != 0 || !*disposable ||
                _wcsicmp(ggzip::win32Path(destination).c_str(), ggzip::win32Path(disposable).c_str()) != 0 ||
                capabilities.persistentAcls || fs::space(target.root).capacity > 300ull * 1024 * 1024)
                throw std::runtime_error("Disk pressure requires an explicitly guarded small disposable CI volume");
        }
        if (capabilities.namedStreams) {
            ggzip::files::commitFamily(source, {{marked, markedOutput}}, {}, {}, {}, &cancel);
            target.adopt(markedOutput);
            check(bytes(markedOutput) == "DOWNLOADED" && ggzip::sourceZone(markedOutput.wstring()) == 3,
                  "cross-volume copy retains the download source zone");
        } else {
            check(throws([&] {
                ggzip::files::commitFamily(source, {{marked, markedOutput}}, {}, {}, {}, &cancel);
            }) && !fs::exists(markedOutput), "non-ADS destination refuses marked data rather than silently dropping MOTW");
        }
        check(std::none_of(fs::directory_iterator(target.root), fs::directory_iterator{},
            [](const auto& item) { return item.path().filename().wstring().rfind(L"ggZip-publish-", 0) == 0; }),
              "success, collision and cancellation clean all owned target-side transaction directories");
        if (!capabilities.persistentAcls) {
            ggzip::files::Workspace fallback(target.root);
            check(ggzip::fileSystemCapabilities(fallback.root).persistentAcls &&
                  !ggzip::sameVolume(fallback.root, target.root),
                  "non-ACL destination falls back to private local staging");
            auto engine = ggzip::createEngine();
            const auto archive = target.root / "archive.zip";
            engine->create(archive.wstring(), {second.wstring()}, {}, {}, cancel);
            target.adopt(archive);
            check(!engine->list(archive.wstring()).empty(), "archive creation on a non-ACL target uses safe cross-volume publication");
            const auto extracted = target.root / "extracted";
            engine->extract(archive.wstring(), extracted.wstring(), {}, {}, {}, cancel);
            target.adopt(extracted / "second.txt");
            check(bytes(extracted / "second.txt") == "REPLACEMENT", "unmarked extraction to a non-ACL volume succeeds");
            ggzip::EditOptions edit;
            edit.rename.push_back({L"second.txt", L"renamed.txt"});
            engine->edit(archive.wstring(), edit, {}, cancel);
            target.adoptOutput(archive);
            check(engine->list(archive.wstring()).front().path == L"renamed.txt",
                  "ZIP editing on a non-ACL target preserves transactional replacement");
            const auto selectedZip = source.root / "selection.zip";
            previewtest::zip(selectedZip, {{"selected.txt", "SELECTED"}, {"secret.txt", "UNSELECTED SECRET"}});
            source.adopt(selectedZip);
            const auto selectedOutput = target.root / "selected";
            engine->extract(selectedZip.wstring(), selectedOutput.wstring(), {L"selected.txt"}, {}, {}, cancel);
            target.adopt(selectedOutput / "selected.txt");
            check(bytes(selectedOutput / "selected.txt") == "SELECTED" && !fs::exists(selectedOutput / "secret.txt"),
                  "non-ACL publication contains only selected verified members, never full decoded staging");
            ggzip::applySourceZone(selectedZip.wstring(), 3);
            const auto refused = target.root / "refused";
            check(throws([&] {
                engine->extract(selectedZip.wstring(), refused.wstring(), {}, {}, {}, cancel);
            }) && !fs::exists(refused), "marked extraction is refused before creating non-ADS destination files");
            if (diskPressure) {
                const auto filler = target.root / "owned-capacity-fixture.bin";
                const HANDLE filling = CreateFileW(filler.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (filling == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create owned disk pressure fixture");
                target.adopt(filler);
                const std::string block(1024 * 1024, 'F');
                bool filled = true;
                const auto reserve = 4ull * 1024 * 1024;
                for (;;) {
                    const auto available = fs::space(target.root).available;
                    if (available <= reserve) break;
                    const auto count = static_cast<DWORD>(std::min<std::uint64_t>(block.size(), available - reserve));
                    DWORD written = 0;
                    if (!WriteFile(filling, block.data(), count, &written, nullptr) || written != count) { filled = false; break; }
                }
                filled = FlushFileBuffers(filling) && filled;
                CloseHandle(filling);
                if (!filled) throw std::runtime_error("Cannot prepare isolated disk pressure fixture");
                const auto pressureInput = source.write(L"pressure.bin", std::string(16 * 1024 * 1024, 'P'));
                const auto oldArchive = target.write(L"pressure.zip", "ORIGINAL ARCHIVE");
                ggzip::CreateOptions creation;
                creation.conflict = ggzip::ConflictPolicy::Overwrite;
                creation.level = 0;
                check(throws([&] {
                    engine->create(oldArchive.wstring(), {pressureInput.wstring()}, creation, {}, cancel);
                }) && bytes(oldArchive) == "ORIGINAL ARCHIVE",
                      "real disk-full failure during target-side copy leaves the old archive untouched");
                check(std::none_of(fs::directory_iterator(target.root), fs::directory_iterator{},
                    [](const auto& item) { return item.path().filename().wstring().rfind(L"ggZip-publish-", 0) == 0; }),
                      "real disk-full failure removes owned incomplete copies and transaction directories");
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
