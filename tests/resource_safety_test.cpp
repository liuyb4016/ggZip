#include "preview_fixtures.h"
#include "SevenZipEngine.h"
#include "FileTransaction.h"
#include "ggzip/OperationError.h"
#include <chrono>
#include <thread>
#include <functional>
#include <iterator>

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
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        const std::wstring command = argv[1];
        std::wstring input;
        fs::path output;
        for (int index = 2; index < argc; ++index) {
            const std::wstring argument = argv[index];
            if (argument.rfind(L"-o", 0) == 0) output = argument.substr(2);
            if (index && std::wstring(argv[index - 1]) == L"--") input = argument;
        }
        if (command == L"l") {
            const bool oversized = fs::path(input).filename() == L"budget.zip";
            std::printf("--\nPath = archive.zip\nType = zip\nPhysical Size = 1\n----------\n"
                "Path = file.txt\nSize = %s\nCRC = %08X\n", oversized ? "18446744073709551615" : "4",
                previewtest::crc32("DONE"));
            return 0;
        }
        if (command == L"x") {
            wchar_t marker[32768]{};
            if (GetEnvironmentVariableW(L"GGZIP_RESOURCE_TEST_MARKER", marker, 32768))
                previewtest::write(marker, "DECODE");
            previewtest::write(output / "file.txt", std::string(4096, 'X'));
            return 0;
        }
        return 1;
    }
    try {
        previewtest::Directory fixture;
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        ggzip::SevenZipEngine engine(executable);
        std::atomic_bool cancel{false};
        const auto marker = fixture.path / "decode-probe.txt";
        _wputenv_s(L"GGZIP_RESOURCE_TEST_MARKER", marker.c_str());
        const auto budget = fixture.path / "budget.zip";
        previewtest::write(budget, "X");
        const auto output = fixture.path / "output";
        bool spaceFailure = false;
        try { engine.extract(budget.wstring(), output.wstring(), {}, {}, {}, cancel); }
        catch (const ggzip::ArchiveError& error) {
            spaceFailure = std::string(error.what()).find("磁盘空间不足") != std::string::npos;
        }
        check(spaceFailure && !fs::exists(marker), "insufficient staging capacity is rejected before decoding or publishing");
        check(!fs::exists(output), "capacity rejection leaves destination untouched");
        const auto excessive = fixture.path / "excessive.zip";
        previewtest::write(excessive, "X");
        check(throws([&] { engine.extract(excessive.wstring(), output.wstring(), {}, {}, {}, cancel); }) &&
              fs::exists(marker) && !fs::exists(output / "file.txt"),
              "actual output larger than metadata is stopped and never published");
        ggzip::InspectOptions listing;
        listing.maxListingBytes = 16;
        check(throws([&] { engine.list(excessive.wstring(), listing); }), "listing resource ceiling terminates oversized metadata");
        const auto large = fixture.path / "large.txt";
        const std::string data(32 * 1024 * 1024, 'C');
        previewtest::write(large, data);
        ggzip::Entry entry;
        entry.size = data.size();
        entry.sizeKnown = true;
        entry.crcKnown = true;
        entry.crc32 = previewtest::crc32(data);
        check(ggzip::files::verified(large, entry, true), "large file checksum verifies through a locked read handle");
        const auto started = GetTickCount64();
        std::thread stop([&] { Sleep(5); cancel.store(true); });
        const bool completed = ggzip::files::verified(large, entry, true, &cancel);
        stop.join();
        check(!completed && GetTickCount64() - started < 1000, "CRC verification responds to cancellation between read chunks");
        cancel.store(false);
        const auto linked = fixture.path / "linked.txt";
        check(CreateHardLinkW(linked.c_str(), large.c_str(), nullptr) != 0, "prepare isolated hard-linked checksum fixture");
        check(!ggzip::files::verified(large, entry, true), "publication verifier refuses multiply-linked staging files");
        fs::remove(linked);
        ggzip::files::Workspace workspace(fixture.path);
        const auto staged = workspace.snapshot(large, L"staged.txt", cancel);
        const auto existing = fixture.path / "existing.txt";
        previewtest::write(existing, "ORIGINAL");
        const auto expected = ggzip::files::identity(existing);
        const HANDLE occupied = CreateFileW(existing.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(throws([&] { ggzip::files::commitFamily(workspace, {{staged, existing}}, {existing}, {expected}); }) &&
              bytes(existing) == "ORIGINAL", "busy-file fault preserves existing bytes before rollback begins");
        if (occupied != INVALID_HANDLE_VALUE) CloseHandle(occupied);
        ggzip::files::Identity verifiedState;
        check(ggzip::files::verified(staged, entry, true, nullptr, &verifiedState),
              "checksum result records the identity from its read handle");
        previewtest::write(staged, "CHANGED AFTER VERIFICATION");
        check(throws([&] {
            ggzip::files::commitFamily(workspace, {{staged, existing}}, {existing}, {expected}, {verifiedState});
        }) && bytes(existing) == "ORIGINAL", "changed verified data is rejected and the old target is rolled back");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
