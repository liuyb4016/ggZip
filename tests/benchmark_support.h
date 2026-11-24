#pragma once
#include "FileTransaction.h"
#include "ProcessUtil.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace benchmark {
namespace fs = std::filesystem;

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE input) : value(input) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

struct Record {
    std::string name;
    std::string expected;
    ggzip::proc::RunResult process;
    bool verified = false;
    std::uint64_t memoryLimit = 0;
    std::uint64_t fixtureBeforeBytes = 0;
    std::uint64_t fixtureCheckpointMaxBytes = 0;
    std::uint64_t fixtureAfterBytes = 0;
};

struct Profile {
    std::uint64_t largeBytes = 0;
    unsigned int smallFiles = 0;
    unsigned int iterations = 1;
    unsigned int imageDimension = 0;
    std::uint64_t startingFreeDiskBytes = 0;
    bool persistentAcls = false;
    std::string targetDigest;
    std::string helperDigest;
    fs::path report;
};

inline unsigned int number(const wchar_t* text, unsigned int maximum) {
    const std::wstring value(text);
    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("Expected a positive numeric benchmark option");
    const auto parsed = std::stoull(value);
    if (!parsed || parsed > maximum) throw std::runtime_error("Benchmark option exceeds its allowed range");
    return static_cast<unsigned int>(parsed);
}

inline void writePattern(ggzip::files::Workspace& workspace, const fs::path& relative, std::uint64_t size) {
    const auto path = workspace.root / relative;
    ggzip::files::makeDirectories(path.parent_path());
    const Handle output(CreateFileW(ggzip::win32Path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create benchmark input");
    workspace.adoptOutput(path);
    std::array<unsigned char, 65536> block{};
    std::uint32_t seed = 17;
    for (auto& value : block) { seed = seed * 1664525 + 1013904223; value = static_cast<unsigned char>(seed >> 24); }
    for (std::uint64_t offset = 0; offset < size;) {
        const auto count = static_cast<DWORD>(std::min<std::uint64_t>(block.size(), size - offset));
        DWORD written = 0;
        if (!WriteFile(output.value, block.data(), count, &written, nullptr) || written != count)
            throw std::runtime_error("Cannot write benchmark input");
        offset += written;
    }
    if (!FlushFileBuffers(output.value)) throw std::runtime_error("Cannot flush benchmark input");
}

inline std::string sha256(const fs::path& path) {
    ggzip::DirectoryLock directory(path.parent_path());
    const Handle input(CreateFileW(ggzip::win32Path(path).c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION information{};
    if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &information) ||
        information.nNumberOfLinks != 1 ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Benchmark digest requires a locked regular file");
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> object;
        ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } state;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA256");
    DWORD required = 0;
    DWORD received = 0;
    if (BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&required),
        sizeof(required), &received, 0) < 0) throw std::runtime_error("Cannot query SHA256 object size");
    state.object.resize(required);
    if (BCryptCreateHash(state.algorithm, &state.hash, state.object.data(), required, nullptr, 0, 0) < 0)
        throw std::runtime_error("Cannot create SHA256 state");
    std::array<unsigned char, 65536> block{};
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(input.value, block.data(), static_cast<DWORD>(block.size()), &count, nullptr))
            throw std::runtime_error("Cannot read benchmark digest input");
        if (!count) break;
        if (BCryptHashData(state.hash, block.data(), count, 0) < 0) throw std::runtime_error("Cannot hash benchmark input");
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("Cannot finish SHA256");
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const auto value : digest) text << std::setw(2) << static_cast<unsigned int>(value);
    return text.str();
}

inline std::uint64_t logicalBytes(const fs::path& root) {
    std::uint64_t bytes = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator) {
        const auto attributes = GetFileAttributesW(ggzip::win32Path(iterator->path()).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) continue;
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Unexpected reparse point in benchmark fixture");
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const auto size = iterator->file_size(error);
        if (!error) bytes += size;
        error.clear();
    }
    return bytes;
}

inline void adoptTree(ggzip::files::Workspace& workspace) {
    try {
        for (const auto& item : fs::recursive_directory_iterator(workspace.root)) {
            const auto identity = ggzip::files::identity(item.path());
            if (identity.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) workspace.adopt(item.path());
            else workspace.adoptOutput(item.path());
        }
    } catch (...) { workspace.preserve = true; throw; }
}

inline Record run(const std::string& name, const std::string& expected, const fs::path& executable,
                  const std::vector<std::wstring>& arguments, ggzip::files::Workspace& workspace,
                  std::uint64_t memoryLimit, const std::wstring& input = {}) {
    Record record;
    record.name = name;
    record.expected = expected;
    record.memoryLimit = memoryLimit;
    record.fixtureBeforeBytes = logicalBytes(workspace.root);
    record.fixtureCheckpointMaxBytes = record.fixtureBeforeBytes;
    ggzip::proc::RunOptions options;
    options.collectMetrics = true;
    options.memoryLimit = memoryLimit;
    options.deadlineTick = GetTickCount64() + 180000;
    options.inputFile = input;
    std::atomic_bool cancel{false};
    const auto started = GetTickCount64();
    try {
        record.process = ggzip::proc::run(executable.wstring(), arguments, {}, cancel, options);
    } catch (const std::exception& error) {
        record.process.exitCode = -1;
        record.process.stderrTail = error.what();
        record.process.metrics.elapsedMs = GetTickCount64() - started;
    }
    adoptTree(workspace);
    record.fixtureAfterBytes = logicalBytes(workspace.root);
    record.fixtureCheckpointMaxBytes = std::max(record.fixtureCheckpointMaxBytes, record.fixtureAfterBytes);
    return record;
}

inline std::string json(std::string_view value) {
    std::ostringstream text;
    text << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') text << '\\' << character;
        else if (character < 32) text << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(character) << std::dec;
        else text << character;
    }
    text << '"';
    return text.str();
}

inline bool report(const Profile& profile, const std::vector<Record>& records) {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) throw std::runtime_error("Cannot inspect host memory");
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const auto getVersion = reinterpret_cast<LONG (WINAPI*)(OSVERSIONINFOW*)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (getVersion) getVersion(&version);
    const bool passed = !records.empty() && std::all_of(records.begin(), records.end(),
        [](const auto& record) { return record.verified && record.process.metrics.available; });
    std::ostringstream text;
    text << "{\"schema_version\":1,\"build_version\":" << json(GGZIP_VERSION) << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"windows_build\":" << version.dwBuildNumber << ",\"logical_processors\":" << system.dwNumberOfProcessors
         << ",\"host_total_memory_bytes\":" << memory.ullTotalPhys
         << ",\"large_input_bytes\":" << profile.largeBytes << ",\"small_file_count\":" << profile.smallFiles
         << ",\"iterations\":" << profile.iterations << ",\"image_dimension\":" << profile.imageDimension
         << ",\"starting_fixture_volume_free_bytes\":" << profile.startingFreeDiskBytes
         << ",\"fixture_persistent_acls\":" << (profile.persistentAcls ? "true" : "false")
         << ",\"target_sha256\":" << json(profile.targetDigest) << ",\"helper_sha256\":" << json(profile.helperDigest)
         << ",\"scope\":" << json("Operation subprocess job; fixture generation and parent verification excluded. Sequential warm-cache runs. Working set is sampled for the root process only. Job counters are raw Windows values; rejected allocations and OS helpers can affect peaks/counts. Fixture sizes are before/after logical-byte checkpoints, not transient peaks or physical disk usage. No directory scans run inside the timed operation.")
         << ",\"records\":[";
    bool first = true;
    for (const auto& record : records) {
        if (!first) text << ',';
        first = false;
        const auto& metrics = record.process.metrics;
        text << "{\"name\":" << json(record.name) << ",\"expected\":" << json(record.expected)
             << ",\"verified\":" << (record.verified ? "true" : "false")
             << ",\"exit_code\":" << record.process.exitCode << ",\"metrics_available\":" << (metrics.available ? "true" : "false")
             << ",\"elapsed_ms\":" << metrics.elapsedMs << ",\"job_cpu_ms\":" << metrics.cpuTimeMs
             << ",\"root_peak_working_set_sampled_bytes\":" << metrics.rootPeakWorkingSetBytes
             << ",\"job_reported_peak_commit_bytes\":" << metrics.jobPeakCommitBytes
             << ",\"job_process_count\":" << metrics.jobProcessCount
             << ",\"job_read_bytes\":" << metrics.jobReadBytes << ",\"job_write_bytes\":" << metrics.jobWriteBytes
             << ",\"configured_job_memory_limit_bytes\":" << record.memoryLimit
             << ",\"fixture_before_logical_bytes\":" << record.fixtureBeforeBytes
             << ",\"fixture_after_logical_bytes\":" << record.fixtureAfterBytes
             << ",\"fixture_checkpoint_max_logical_bytes\":" << record.fixtureCheckpointMaxBytes
             << ",\"diagnostic_tail\":" << json(record.process.stderrTail.substr(0, 2048)) << '}';
        std::printf("%s: %s, %llu ms, reported job peak %llu bytes\n", record.verified ? "PASS" : "FAIL",
            record.name.c_str(), static_cast<unsigned long long>(metrics.elapsedMs),
            static_cast<unsigned long long>(metrics.jobPeakCommitBytes));
        if (!record.verified)
            std::fprintf(stderr, "Exit %d: %s\n", record.process.exitCode, record.process.stderrTail.substr(0, 2048).c_str());
    }
    text << "]}\n";
    if (!profile.report.empty()) {
        const auto destination = fs::absolute(profile.report).lexically_normal();
        if (ggzip::files::identity(destination).exists) throw std::runtime_error("Benchmark report already exists; choose a new path");
        ggzip::files::Workspace output(destination.parent_path());
        const auto staged = output.write(L"report.json", text.str());
        ggzip::files::commitFamily(output, {{staged, destination}}, {}, {});
        std::printf("Report: %s\n", ggzip::utf8(destination.wstring()).c_str());
    }
    return passed;
}

}
