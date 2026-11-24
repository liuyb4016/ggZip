#include "preview_fixtures.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/ArchivePath.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/OperationError.h"
#include "ggzip/PasswordError.h"
#include "ggzip/Text.h"
#include "ProcessUtil.h"
#include "SevenZipEngine.h"
#include <functional>
#include <iterator>
#include <regex>
#include <set>
#include <thread>
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
std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
bool nonAscii(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) { return character >= 128; });
}
std::string unquote(std::string_view token) {
    std::string result;
    for (std::size_t index = 1; index + 1 < token.size(); ++index) {
        char character = token[index];
        if (character == '\\') {
            character = token[++index];
            if (character == 'n') character = '\n';
            else if (character == 'r') character = '\r';
            else if (character == 't') character = '\t';
            else if (character != '\\' && character != '"')
                throw std::runtime_error("Unsupported diagnostic source escape");
        }
        result += character;
    }
    return result;
}
bool catalogCoverage(const fs::path& path, bool allowUsage = false) {
    const auto text = bytes(path);
    if (text.empty()) return false;
    const std::regex tokens(R"token(//[^\r\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')token");
    const std::regex call(R"(diagnostic\(\s*$)");
    bool covered = true;
    for (std::sregex_iterator iterator(text.begin(), text.end(), tokens), end; iterator != end; ++iterator) {
        const auto token = iterator->str();
        if (token.front() != '"' || !nonAscii(token)) continue;
        const auto offset = static_cast<std::size_t>(iterator->position());
        if (allowUsage && offset > text.find("const char* kUsage =") &&
            offset < text.find("const char* kUsageEnglish =")) continue;
        const auto preceding = text.substr(offset > 40 ? offset - 40 : 0, std::min<std::size_t>(40, offset));
        const auto source = unquote(token);
        const auto catalog = ggzip::diagnosticCatalog();
        if (!std::regex_search(preceding, call) ||
            std::none_of(catalog.begin(), catalog.end(), [&](const auto& entry) { return entry.source == source; })) {
            covered = false;
            std::fprintf(stderr, "Unlocalized diagnostic in %s: %s\n", ggzip::utf8(path.wstring()).c_str(), token.c_str());
        }
    }
    return covered;
}
struct CliResult {
    int exitCode;
    std::string output;
    std::string error;
};
CliResult runCli(const fs::path& executable, const std::vector<std::wstring>& arguments, const fs::path& directory) {
    std::atomic_bool cancel{false};
    ggzip::proc::RunOptions options;
    options.workingDirectory = directory.wstring();
    options.deadlineTick = GetTickCount64() + 15000;
    std::string output;
    const auto result = ggzip::proc::run(executable.wstring(), arguments,
        [&](std::string_view chunk) { output.append(chunk); return true; }, cancel, options);
    return {result.exitCode, std::move(output), result.stderrTail};
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"l") {
        std::fputs("第三方提示: 目标已存在; do not translate", stderr);
        return 2;
    }
    try {
        if (argc != 3) throw std::runtime_error("Expected CLI path and repository root");
        const fs::path cli = fs::absolute(argv[1]);
        const fs::path repository = fs::absolute(argv[2]);
        const auto catalog = ggzip::diagnosticCatalog();
        std::set<std::string_view> keys;
        bool valid = catalog.size() >= 200;
        bool roundtrip = true;
        for (const auto& entry : catalog) {
            valid = valid && !entry.source.empty() && !entry.english.empty() &&
                !nonAscii(entry.english) && keys.insert(entry.source).second;
            ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
            roundtrip = roundtrip && ggzip::diagnostic(entry.source.data()) == entry.english;
            ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::SimplifiedChinese);
            roundtrip = roundtrip && ggzip::diagnostic(entry.source.data()) == entry.source;
        }
        check(valid, "diagnostic catalog has unique keys and complete nonempty English messages");
        check(roundtrip, "every catalog message roundtrips between Chinese and English");
        bool coverage = true;
        for (const auto& item : fs::directory_iterator(repository / "core/src")) {
            if (item.path().extension() != L".cpp" || item.path().filename() == L"Diagnostics.cpp") continue;
            coverage = catalogCoverage(item.path()) && coverage;
        }
        coverage = catalogCoverage(repository / "gui/src/PreviewImage.cpp") && coverage;
        coverage = catalogCoverage(repository / "gui/src/PreviewSession.cpp") && coverage;
        coverage = catalogCoverage(repository / "cli/src/main.cpp", true) && coverage;
        check(coverage, "all core, CLI and preview-owned diagnostic strings are catalogued before interpolation");
        ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
        const std::string unknown = "目标已存在 - USER CONTENT";
        check(ggzip::diagnostic(unknown.c_str()) == unknown, "unknown text is unchanged rather than translated by prefix");
        check(std::string(ggzip::diagnostic(nullptr)).empty(), "null source is handled safely");
        bool inherited = false;
        std::thread background([&] { inherited = std::string(ggzip::diagnostic("操作已取消")) == "Operation cancelled"; });
        background.join();
        check(inherited, "background threads use the selected application diagnostic language");
        const ggzip::ArchiveError ordinary(ggzip::diagnostic("操作已取消"));
        const ggzip::ConflictError conflict(ggzip::diagnostic("目标压缩包或分卷已存在"));
        const ggzip::PasswordError password(ggzip::diagnostic("预览需要正确的密码"));
        check(std::string(ordinary.code()) == "GGZIP_OPERATION_FAILED" &&
              std::string(conflict.code()) == "GGZIP_CONFLICT" &&
              std::string(password.code()) == "GGZIP_PASSWORD_REQUIRED",
              "ordinary, conflict and password error codes do not depend on language");
        bool unsafe = false;
        try { ggzip::validatedPreviewPath(L"../escape.txt"); }
        catch (const ggzip::ArchiveError& error) { unsafe = std::string(error.what()) == "The preview entry contains unsafe path components"; }
        check(unsafe, "real unsafe-path rejection returns an English diagnostic");
        previewtest::Directory fixture;
        const auto input = fixture.path / L"目标已存在 中文 文件.txt";
        previewtest::write(input, "PAYLOAD");
        auto engine = ggzip::createEngine();
        std::atomic_bool cancel{false};
        bool preserved = false;
        const auto invalidArchive = fixture.path / L"目标已存在.unsupported";
        try { engine->create(invalidArchive.wstring(), {input.wstring()}, {}, {}, cancel); }
        catch (const ggzip::ArchiveError& error) {
            const std::string text = error.what();
            preserved = text.rfind("Unsupported compression format: ", 0) == 0 &&
                text.find(ggzip::utf8(invalidArchive.filename().wstring())) != std::string::npos;
        }
        check(preserved, "translated details preserve Chinese paths and message-like filenames verbatim");
        const auto archive = fixture.path / "archive.zip";
        engine->create(archive.wstring(), {input.wstring()}, {}, {}, cancel);
        const auto original = bytes(archive);
        bool conflictEnglish = false;
        try { engine->create(archive.wstring(), {input.wstring()}, {}, {}, cancel); }
        catch (const ggzip::ConflictError& error) {
            conflictEnglish = std::string(error.what()) == "The destination archive or one of its volumes already exists";
        }
        check(conflictEnglish && bytes(archive) == original, "real English conflict rejection preserves the original archive");
        ggzip::OperationSummary failed;
        failed.diagnostic = std::string(ggzip::diagnostic("提交或恢复失败，原文件备份保留于: ")) + ggzip::utf8(input.wstring());
        failed.recoveryPath = input.wstring();
        const ggzip::OperationError recovery(failed);
        check(std::string(recovery.what()).find(ggzip::utf8(input.wstring())) != std::string::npos &&
              recovery.summary.recoveryPath == input.wstring(), "recovery paths are retained unchanged in localized errors");
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        ggzip::InspectOptions inspection;
        ggzip::SevenZipEngine fake(executable);
        bool rawPreserved = false;
        try { fake.inspect(archive.wstring(), nullptr, inspection); }
        catch (const ggzip::ArchiveError& error) {
            const std::string text = error.what();
            rawPreserved = text.rfind("Failed to read the archive (exit ", 0) == 0 &&
                text.find("第三方提示: 目标已存在; do not translate") != std::string::npos;
        }
        check(rawPreserved, "raw third-party engine diagnostics are not rewritten");
        const auto englishHelp = runCli(cli, {L"--lang", L"en", L"--help"}, fixture.path);
        check(englishHelp.exitCode == 0 && englishHelp.output.find("Usage:") != std::string::npos &&
              !nonAscii(englishHelp.output), "English CLI help is complete and exits successfully");
        const auto chineseHelp = runCli(cli, {L"--help"}, fixture.path);
        check(chineseHelp.exitCode == 0 && chineseHelp.output.find("用法:") != std::string::npos,
              "CLI retains Chinese as its default language");
        const auto suffixHelp = runCli(cli, {L"help", L"--lang=en"}, fixture.path);
        check(suffixHelp.exitCode == 0 && suffixHelp.output == englishHelp.output,
              "language option supports command suffix and equals syntax");
        const auto badLanguage = runCli(cli, {L"--lang", L"en", L"--lang", L"invalid", L"version"}, fixture.path);
        check(badLanguage.exitCode == 2 && badLanguage.error.find("Invalid language option") != std::string::npos,
              "invalid language is rejected without echoing its value");
        const auto missingLanguage = runCli(cli, {L"--lang", L"en", L"version", L"--lang"}, fixture.path);
        check(missingLanguage.exitCode == 2 && missingLanguage.error.find("language option needs a value") != std::string::npos,
              "missing language value gives a localized argument diagnostic");
        const auto englishList = runCli(cli, {L"list", archive.wstring(), L"--lang", L"en"}, fixture.path);
        check(englishList.exitCode == 0 && englishList.output.find("Archive: ") != std::string::npos &&
              englishList.output.find(ggzip::utf8(input.filename().wstring())) != std::string::npos,
              "English CLI listing translates labels but preserves entry names");
        const auto englishConflict = runCli(cli, {L"--lang", L"en", L"create", archive.wstring(), input.wstring()}, fixture.path);
        const auto chineseConflict = runCli(cli, {L"create", archive.wstring(), input.wstring(), L"--lang", L"zh_CN"}, fixture.path);
        check(englishConflict.exitCode == 4 && chineseConflict.exitCode == 4 &&
              englishConflict.error.find("[GGZIP_CONFLICT]") != std::string::npos &&
              chineseConflict.error.find("[GGZIP_CONFLICT]") != std::string::npos &&
              englishConflict.error.find("already exists") != std::string::npos &&
              chineseConflict.error.find("已存在") != std::string::npos,
              "Chinese and English CLI conflict exit codes and error codes match");
        const auto policy = runCli(cli, {L"create", archive.wstring(), input.wstring(),
            L"--on-conflict", L"invalid", L"--lang", L"en"}, fixture.path);
        check(policy.exitCode == 4 && policy.error.find("Invalid conflict policy") != std::string::npos,
              "suffix language selection also applies to earlier argument validation failures");
        ggzip::CreateOptions creation;
        creation.password = L"--lang";
        const auto encrypted = fixture.path / "encrypted.7z";
        engine->create(encrypted.wstring(), {input.wstring()}, creation, {}, cancel);
        const auto encryptedList = runCli(cli, {L"list", encrypted.wstring(), L"-p", L"--lang", L"--lang", L"en"}, fixture.path);
        check(encryptedList.exitCode == 0 && encryptedList.output.find("Archive: ") != std::string::npos,
              "password values that equal --lang are not treated as language flags");
        const auto wrongPassword = runCli(cli, {L"--lang", L"en", L"list", encrypted.wstring(),
            L"-p", L"SECRET_INVALID_PASSWORD"}, fixture.path);
        check(wrongPassword.exitCode == 2 && wrongPassword.error.find("[GGZIP_PASSWORD_REQUIRED]") != std::string::npos &&
              wrongPassword.error.find("Failed to read the archive") != std::string::npos &&
              wrongPassword.error.find("SECRET_INVALID_PASSWORD") == std::string::npos,
              "English wrong-password diagnostics preserve their error code and do not reveal the password");
        const auto outputValue = runCli(cli, {L"extract", archive.wstring(), L"-o", L"--lang", L"--lang", L"en"}, fixture.path);
        check(outputValue.exitCode == 0 && fs::exists(fixture.path / "--lang" / input.filename()),
              "output directory values that equal --lang remain literal");
        const auto literalArchive = fixture.path / "literal.zip";
        previewtest::zip(literalArchive, {{"--lang", "FIRST"}, {"en", "SECOND"}, {"keep.txt", "KEEP"}});
        const auto literalDelete = runCli(cli, {L"--lang", L"en", L"delete", literalArchive.wstring(),
            L"--", L"--lang", L"en"}, fixture.path);
        check(literalDelete.exitCode == 0 && engine->list(literalArchive.wstring()).size() == 1 &&
              engine->list(literalArchive.wstring()).front().path == L"keep.txt",
              "arguments after -- remain literal even when they look like language options");
        previewtest::zip(fixture.path / "--help", {{"keep.txt", "KEEP"}});
        const auto literalHelp = runCli(cli, {L"--lang", L"en", L"list", L"--", L"--help"}, fixture.path);
        check(literalHelp.exitCode == 0 && literalHelp.output.find("Archive: ") != std::string::npos &&
              literalHelp.output.find("Usage:") == std::string::npos, "literal --help filename does not trigger help");
        ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::SimplifiedChinese);
        check(std::string(ggzip::diagnostic("操作已取消")) == "操作已取消", "Chinese diagnostics can be restored explicitly");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures; %zu catalog messages\n", checks, failures, ggzip::diagnosticCatalog().size());
    return failures ? 1 : 0;
}
