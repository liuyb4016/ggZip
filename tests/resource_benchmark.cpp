#include "benchmark_support.h"
#include "ggzip/EngineFactory.h"

namespace fs = std::filesystem;
namespace {
std::string smallData(unsigned int index) {
    std::string data = std::to_string(index) + "\n";
    data.resize(512, static_cast<char>('A' + index % 26));
    return data;
}
fs::path smallPath(unsigned int index) {
    return fs::path("small") / std::to_wstring(index % 16) / (L"file-" + std::to_wstring(index) + L".txt");
}
bool smallFilesMatch(const fs::path& output, unsigned int count) {
    if (!fs::is_directory(output)) return false;
    unsigned int actual = 0;
    for (const auto& item : fs::recursive_directory_iterator(output))
        if (item.is_regular_file()) ++actual;
    if (actual != count) return false;
    for (unsigned int index = 0; index < count; ++index) {
        std::ifstream input(output / smallPath(index), std::ios::binary);
        const std::string bytes(std::istreambuf_iterator<char>(input), {});
        if (bytes != smallData(index)) return false;
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--allocation-denial") {
            void* allocation = VirtualAlloc(nullptr, 128ull * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (allocation) VirtualFree(allocation, 0, MEM_RELEASE);
            return allocation ? 9 : 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--cancel-extract") {
            auto engine = ggzip::createEngine();
            std::atomic_bool cancel{false};
            ggzip::ExtractOptions options;
            options.conflict = ggzip::ConflictPolicy::Overwrite;
            const auto result = engine->extract(argv[2], argv[3], {}, options,
                [&](const ggzip::Progress&) { cancel.store(true); }, cancel);
            return result.outcome == ggzip::Outcome::Cancelled ? 0 : 9;
        }
        if (argc < 2) throw std::runtime_error("Expected CLI executable, optionally --report path --large-mib N --small-files N --iterations N");
        const fs::path cli = fs::absolute(argv[1]);
        benchmark::Profile profile;
        profile.largeBytes = 8ull * 1024 * 1024;
        profile.smallFiles = 128;
        for (int index = 2; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (index + 1 == argc) throw std::runtime_error("Missing benchmark option value");
            const auto* value = argv[++index];
            if (option == L"--report") profile.report = value;
            else if (option == L"--large-mib") profile.largeBytes = benchmark::number(value, 4096) * 1024ull * 1024;
            else if (option == L"--small-files") profile.smallFiles = benchmark::number(value, 100000);
            else if (option == L"--iterations") profile.iterations = benchmark::number(value, 3);
            else throw std::runtime_error("Unknown benchmark option");
        }
        if (profile.largeBytes < 8ull * 1024 * 1024 || profile.smallFiles * profile.iterations > 100000)
            throw std::runtime_error("Benchmark needs at least 8 MiB and at most 100000 small output files");
        if (!profile.report.empty() && ggzip::files::identity(fs::absolute(profile.report)).exists)
            throw std::runtime_error("Benchmark report already exists; choose a new path");
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory)) throw std::runtime_error("Cannot inspect available memory");
        const auto needed = profile.largeBytes * (3 * profile.iterations + 1) +
            static_cast<std::uint64_t>(profile.smallFiles) * 512 * (2 * profile.iterations + 1) + 512ull * 1024 * 1024;
        if (memory.ullAvailPhys < 512ull * 1024 * 1024 || fs::space(fs::temp_directory_path()).available < needed) {
            std::puts("SKIP: insufficient safe memory or staging space for the selected benchmark");
            return 77;
        }
        const auto limit = std::min<std::uint64_t>(512ull * 1024 * 1024, memory.ullAvailPhys / 4);
        ggzip::files::Workspace workspace(fs::temp_directory_path());
        profile.startingFreeDiskBytes = fs::space(workspace.root).available;
        profile.persistentAcls = ggzip::fileSystemCapabilities(workspace.root).persistentAcls;
        if (profile.startingFreeDiskBytes < needed) { std::puts("SKIP: insufficient private staging volume space"); return 77; }
        auto engine = ggzip::createEngine();
        if (!engine->available()) throw std::runtime_error("Pinned benchmark engine is unavailable");
        profile.targetDigest = benchmark::sha256(cli);
        profile.helperDigest = benchmark::sha256(engine->enginePath());
        benchmark::writePattern(workspace, L"large.bin", profile.largeBytes);
        const auto largeInput = workspace.root / "large.bin";
        const auto sourceDigest = benchmark::sha256(largeInput);
        for (unsigned int index = 0; index < profile.smallFiles; ++index) workspace.write(smallPath(index), smallData(index));
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        std::vector<benchmark::Record> records;
        for (unsigned int iteration = 1; iteration <= profile.iterations; ++iteration) {
            const auto prefix = "iteration-" + std::to_string(iteration) + "/";
            const auto target = workspace.root / (L"iteration-" + std::to_wstring(iteration));
            ggzip::files::makeDirectories(target);
            workspace.adopt(target);
            const auto largeArchive = target / "large.zip";
            auto created = benchmark::run(prefix + "large-create", "success", cli,
                {L"--lang", L"en", L"create", largeArchive.wstring(), largeInput.wstring(), L"--level", L"0"},
                workspace, limit);
            created.verified = created.process.exitCode == 0 && fs::exists(largeArchive);
            records.push_back(std::move(created));
            if (!records.back().verified) break;
            const auto largeOutput = target / "large-output";
            auto extracted = benchmark::run(prefix + "large-extract", "success", cli,
                {L"--lang", L"en", L"extract", largeArchive.wstring(), L"-o", largeOutput.wstring()}, workspace, limit);
            extracted.verified = extracted.process.exitCode == 0 &&
                fs::is_regular_file(largeOutput / "large.bin") &&
                benchmark::sha256(largeOutput / "large.bin") == sourceDigest;
            records.push_back(std::move(extracted));
            const auto archiveDigest = benchmark::sha256(largeArchive);
            const auto archiveState = ggzip::files::identity(largeArchive);
            {
                const benchmark::Handle occupied(CreateFileW(ggzip::win32Path(largeArchive).c_str(), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (occupied.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock the occupied archive fixture");
                auto blocked = benchmark::run(prefix + "occupied-overwrite", "rejected-with-original-retained", cli,
                    {L"--lang", L"en", L"create", largeArchive.wstring(), largeInput.wstring(),
                     L"--level", L"0", L"--on-conflict", L"overwrite"}, workspace, limit);
                blocked.verified = blocked.process.exitCode != 0 && blocked.process.exitCode != -1 &&
                    ggzip::files::same(ggzip::files::identity(largeArchive), archiveState) &&
                    benchmark::sha256(largeArchive) == archiveDigest;
                records.push_back(std::move(blocked));
            }
            const auto smallArchive = target / "small.zip";
            auto smallCreated = benchmark::run(prefix + "small-create", "success", cli,
                {L"--lang", L"en", L"create", smallArchive.wstring(), (workspace.root / "small").wstring(), L"--level", L"0"},
                workspace, limit);
            smallCreated.verified = smallCreated.process.exitCode == 0 && fs::exists(smallArchive);
            records.push_back(std::move(smallCreated));
            if (!records.back().verified) break;
            const auto smallOutput = target / "small-output";
            auto smallExtracted = benchmark::run(prefix + "small-extract", "success", cli,
                {L"--lang", L"en", L"extract", smallArchive.wstring(), L"-o", smallOutput.wstring()}, workspace, limit);
            smallExtracted.verified = smallExtracted.process.exitCode == 0 && smallFilesMatch(smallOutput, profile.smallFiles);
            records.push_back(std::move(smallExtracted));
            const auto cancelledOutput = target / "cancel-output";
            const auto oldFile = workspace.write(cancelledOutput.lexically_relative(workspace.root) / "large.bin", "ORIGINAL");
            const auto oldDigest = benchmark::sha256(oldFile);
            const auto oldState = ggzip::files::identity(oldFile);
            auto cancelled = benchmark::run(prefix + "cooperative-cancel", "cancelled-with-original-retained", executable,
                {L"--cancel-extract", largeArchive.wstring(), cancelledOutput.wstring()}, workspace, limit);
            cancelled.verified = cancelled.process.exitCode == 0 &&
                ggzip::files::same(ggzip::files::identity(oldFile), oldState) && benchmark::sha256(oldFile) == oldDigest;
            records.push_back(std::move(cancelled));
        }
        auto denial = benchmark::run("allocation-denial", "allocation-rejected-without-host-exhaustion", executable,
            {L"--allocation-denial"}, workspace, 32ull * 1024 * 1024);
        denial.verified = denial.process.exitCode == 0;
        records.push_back(std::move(denial));
        return benchmark::report(profile, records) ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
