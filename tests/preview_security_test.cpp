#include "preview_fixtures.h"
#include "ggzip/ArchivePath.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/Text.h"
#include "ProcessUtil.h"
#include <atomic>
#include <cstdio>
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

}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--echo-arguments") {
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex) {
            const std::string value = ggzip::utf8(argv[argumentIndex]);
            std::printf("%zu:", value.size());
            std::fwrite(value.data(), 1, value.size(), stdout);
        }
        return 0;
    }
    try {
        const std::vector<std::wstring> unsafe = {
            L"", L"/root.txt", L"\\root.txt", L"C:\\file.txt", L"C:relative.txt",
            L"\\\\server\\share\\file.txt", L"//server/share/file.txt",
            L"\\\\?\\C:\\file.txt", L"../outside.txt", L"dir/../../outside.txt",
            L"dir\\..\\outside.txt", L"./file.txt", L"dir/./file.txt", L"dir//file.txt",
            L"dir/", L"file.txt:stream", L"CON", L"con .txt", L"dir/NUL.txt", L"PRN",
            L"AUX.dat", L"COM1 .txt", L"LPT9", L"COM\u00b9.txt", L"CONIN$",
            L"file.", L"dir /file.txt", L"file ", L"wild*.txt", L"wild?.txt",
            L"quote\".txt", L"pipe|.txt", L"line\nbreak.txt", std::wstring(L"nul\0byte", 8)
        };
        for (const auto& path : unsafe) {
            check(throws([&] { ggzip::validatedPreviewPath(path); }), "unsafe member path rejected");
        }
        check(ggzip::validatedPreviewPath(L"目录\\中文 文件.txt") == L"目录/中文 文件.txt",
              "Unicode and separator normalization");
        for (const auto& path : { L"-oescape.txt", L"-i!file.txt", L"--", L"@list.txt",
                                 L".hidden", L"COM10.txt", L"emoji-\U0001f600.txt" }) {
            check(!throws([&] { ggzip::validatedPreviewPath(path); }), "literal safe member path accepted");
        }

        std::atomic_bool cancel{ false };
        wchar_t executable[32768]{};
        check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "test executable path");
        const std::vector<std::wstring> original = {
            L"", L"C:\\directory with spaces\\", L"before\\\"after", L"tail\\\\",
            L"plain\"quote", L"中文 参数", L"--", L"-pnot-a-secret"
        };
        std::vector<std::wstring> arguments{ L"--echo-arguments" };
        std::string expected;
        for (const auto& argument : original) {
            arguments.push_back(argument);
            const std::string encoded = ggzip::utf8(argument);
            expected += std::to_string(encoded.size()) + ":" + encoded;
        }
        std::string echoed;
        const auto result = ggzip::proc::run(executable, arguments,
            [&](std::string_view chunk) { echoed.append(chunk); return true; }, cancel);
        check(result.exitCode == 0 && echoed == expected, "Windows argument quoting preserves exact boundaries");

        previewtest::Directory temporary;
        auto engine = ggzip::createEngine();
        if (!engine->available()) throw std::runtime_error("Pinned 7-Zip engine is required");
        const auto archive = temporary.path / L"中文 archive.zip";
        previewtest::zip(archive, {
            {"same.txt", "ROOT"}, {"sub/same.txt", "SUB"}, {"-oescape.txt", "OPTION"},
            {"-i!file.txt", "INCLUDE"}, {"--", "DASH"}, {"@list.txt", "AT"},
            {"a.txt", "LOWER"}, {"A.txt", "UPPER"}, {"nested/test.txt", "NESTED"},
            {"目录/中文 文件.txt", "中文内容"}, {"empty.txt", ""},
            {"binary.bin", std::string("a\0b\r\n", 5)},
            {"../outside.txt", "DO NOT WRITE"}, {"C:/outside.txt", "DO NOT WRITE"},
            {"link.txt", "../../outside.txt", 0xa1ff0000},
            {"directory/", "", 0x41ed0010}
        });
        auto read = [&](const std::wstring& member, const std::wstring& from) {
            std::string data;
            engine->extractToStream(from, member, {}, [&](std::string_view chunk) { data.append(chunk); }, cancel);
            return data;
        };
        for (const auto& item : std::vector<std::pair<std::wstring, std::string>>{
                {L"same.txt", "ROOT"}, {L"sub/same.txt", "SUB"}, {L"-oescape.txt", "OPTION"},
                {L"-i!file.txt", "INCLUDE"}, {L"--", "DASH"}, {L"@list.txt", "AT"},
                {L"a.txt", "LOWER"}, {L"A.txt", "UPPER"}, {L"nested\\test.txt", "NESTED"},
                {L"目录/中文 文件.txt", "中文内容"}, {L"empty.txt", ""},
                {L"binary.bin", std::string("a\0b\r\n", 5)} }) {
            check(read(item.first, archive.wstring()) == item.second, "stream selects exact literal member bytes");
        }
        check(!std::filesystem::exists(temporary.path / "outside.txt"), "preview streaming writes no archive paths");
        bool called = false;
        for (const auto& member : { L"../outside.txt", L"C:/outside.txt", L"link.txt", L"directory", L"missing.txt" }) {
            called = false;
            check(throws([&] {
                engine->extractToStream(archive.wstring(), member, {},
                    [&](std::string_view) { called = true; }, cancel);
            }) && !called, "unsafe, linked, directory or absent member produces no data");
        }
        const auto duplicate = temporary.path / "duplicate.zip";
        previewtest::zip(duplicate, { {"same.txt", "FIRST"}, {"same.txt", "SECOND"} });
        check(throws([&] { read(L"same.txt", duplicate.wstring()); }), "duplicate member names rejected");
        const auto damaged = temporary.path / "damaged.zip";
        previewtest::zip(damaged, { {"file.txt", "BAD CRC", 0x81a40000, true} });
        check(throws([&] { read(L"file.txt", damaged.wstring()); }), "CRC failure is not reported as a successful preview");
        const auto forged = temporary.path / "forged-link.zip";
        previewtest::zip(forged, {
            {"link.txt", "../../outside.txt", 0xa1ff0000, false, "forged\nAttributes =  01800000"}
        });
        called = false;
        check(throws([&] {
            engine->extractToStream(forged.wstring(), L"link.txt", {},
                [&](std::string_view) { called = true; }, cancel);
        }) && !called, "archive comments cannot forge preview link metadata");
        const auto linkedTar = temporary.path / "links.tar";
        previewtest::tarLinks(linkedTar);
        for (const auto& member : { L"symbolic.txt", L"hard.txt" }) {
            called = false;
            check(throws([&] {
                engine->extractToStream(linkedTar.wstring(), member, {},
                    [&](std::string_view) { called = true; }, cancel);
            }) && !called, "tar symbolic and hard links rejected before streaming");
        }

        const auto text = temporary.path / L"密码.txt";
        previewtest::write(text, "ENCRYPTED");
        const auto encrypted = temporary.path / "encrypted.7z";
        ggzip::CreateOptions creation;
        creation.password = L"密码 \"quoted\" \\tail\\";
        creation.level = 1;
        engine->create(encrypted.wstring(), { text.wstring() }, creation, {}, cancel);
        check(throws([&] { read(L"密码.txt", encrypted.wstring()); }), "encrypted preview without password fails");
        ggzip::ExtractOptions extraction;
        extraction.password = creation.password;
        std::string decrypted;
        engine->extractToStream(encrypted.wstring(), L"密码.txt", extraction,
            [&](std::string_view chunk) { decrypted.append(chunk); }, cancel);
        check(decrypted == "ENCRYPTED", "streaming supports correct passwords");
        check(throws([&] {
            engine->extractToStream(archive.wstring(), L"same.txt", {},
                [](std::string_view) { throw std::runtime_error("sink failure"); }, cancel);
        }), "sink failure propagates instead of publishing a preview");
        check(throws([&] {
            engine->extractToStream(archive.wstring(), L"same.txt", {},
                [&](std::string_view) { cancel.store(true); }, cancel);
        }), "cancellation during streaming cannot publish a preview");
        cancel.store(true);
        check(throws([&] { read(L"same.txt", archive.wstring()); }), "cancelled preview cannot succeed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
