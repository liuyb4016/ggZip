#include "ProcessUtil.h"
#include "preview_fixtures.h"
#include <cstdio>
#include <functional>

namespace {
int failures = 0;
void check(bool result, const char* label) {
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
bool throws(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    if (argc > 1) {
        const std::wstring mode = argv[1];
        if (mode == L"--sleep") { Sleep(60000); return 0; }
        if (mode == L"--stderr") {
            const std::string data(32768, 'X');
            std::fwrite(data.data(), 1, data.size(), stderr);
            return 0;
        }
        if (mode == L"--stdout") {
            const std::string data(32768, 'Y');
            for (int index = 0; index < 1000; ++index) std::fwrite(data.data(), 1, data.size(), stdout);
            return 0;
        }
        if (mode == L"--memory") {
            void* allocation = VirtualAlloc(nullptr, 128ull * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (allocation) VirtualFree(allocation, 0, MEM_RELEASE);
            return allocation ? 9 : 0;
        }
        if (mode == L"--memory-probe") {
            void* allocation = VirtualAlloc(nullptr, 32ull * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (allocation) VirtualFree(allocation, 0, MEM_RELEASE);
            return allocation ? 9 : 0;
        }
        if (mode == L"--memory-tree") {
            void* allocation = VirtualAlloc(nullptr, 24ull * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!allocation) return 9;
            auto* pages = static_cast<volatile unsigned char*>(allocation);
            for (std::size_t offset = 0; offset < 24ull * 1024 * 1024; offset += 4096) pages[offset] = 1;
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --memory-probe";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &process)) { VirtualFree(allocation, 0, MEM_RELEASE); return 9; }
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD code = 9;
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            VirtualFree(allocation, 0, MEM_RELEASE);
            return static_cast<int>(code);
        }
        if (mode == L"--handle" && argc > 2) {
            SetEvent(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)));
            return 0;
        }
        if (mode == L"--descendant") {
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --sleep";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &process)) return 9;
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 0;
        }
    }
    try {
        std::atomic_bool cancel{false};
        check(throws([&] {
            ggzip::proc::run(executable, {std::wstring(32768, L'X')}, {}, cancel);
        }), "excessive Windows command lines are rejected before launch");
        check(throws([&] {
            ggzip::proc::run(executable, {std::wstring(L"pwd\0suffix", 10)}, {}, cancel);
        }), "embedded NUL cannot truncate process arguments or passwords");
        ggzip::proc::RunOptions limits;
        limits.deadlineTick = GetTickCount64() + 150;
        auto started = GetTickCount64();
        check(throws([&] { ggzip::proc::run(executable, {L"--sleep"}, {}, cancel, limits); }) &&
              GetTickCount64() - started < 5000, "deadline terminates helper promptly");
        limits = {};
        limits.memoryLimit = 32ull * 1024 * 1024;
        check(ggzip::proc::run(executable, {L"--memory"}, {}, cancel, limits).exitCode == 0,
              "job object enforces memory allocation ceiling");
        limits.memoryLimit = 48ull * 1024 * 1024;
        limits.collectMetrics = true;
        const auto aggregate = ggzip::proc::run(executable, {L"--memory-tree"}, {}, cancel, limits);
        check(aggregate.exitCode == 0, "descendant allocations cannot bypass the aggregate job memory ceiling");
        std::printf("Job metrics: available=%d processes=%u peak_commit=%llu root_peak_ws=%llu bytes\n",
            aggregate.metrics.available, aggregate.metrics.jobProcessCount,
            static_cast<unsigned long long>(aggregate.metrics.jobPeakCommitBytes),
            static_cast<unsigned long long>(aggregate.metrics.rootPeakWorkingSetBytes));
        check(aggregate.metrics.available && aggregate.metrics.jobProcessCount >= 2 &&
              aggregate.metrics.jobPeakCommitBytes >= 24ull * 1024 * 1024,
              "job metrics retain Windows-reported descendant and peak commit counters");
        limits = {};
        check(ggzip::proc::run(executable, {L"--stderr"}, {}, cancel).stderrTail.size() == 8192,
              "stderr diagnostic memory is bounded");
        started = GetTickCount64();
        check(ggzip::proc::run(executable, {L"--descendant"}, {}, cancel).exitCode == 0 &&
              GetTickCount64() - started < 5000, "descendants cannot retain inherited pipes after helper exits");
        check(throws([&] {
            ggzip::proc::run(executable, {L"--stdout"},
                [](std::string_view) -> bool { throw std::runtime_error("sink limit"); }, cancel);
        }), "output limit failure terminates helper and propagates");
        check(ggzip::proc::run(executable, {L"--stdout"},
            [&](std::string_view) { cancel.store(true); return false; }, cancel).exitCode == -1,
            "cancellation terminates output producer");
        cancel.store(false);
        limits = {};
        limits.monitor = [] { throw std::runtime_error("disk limit"); };
        check(throws([&] { ggzip::proc::run(executable, {L"--sleep"}, {}, cancel, limits); }),
              "resource monitor stops otherwise silent helper");
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE event = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        const auto value = std::to_wstring(reinterpret_cast<std::uintptr_t>(event));
        check(event && ggzip::proc::run(executable, {L"--handle", value}, {}, cancel).exitCode == 0 &&
              WaitForSingleObject(event, 0) == WAIT_TIMEOUT,
              "unrelated inheritable handles are not exposed");
        CloseHandle(event);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return failures ? 1 : 0;
}
