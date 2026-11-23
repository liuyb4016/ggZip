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
        if (mode == L"--handle" && argc > 2) {
            DWORD flags = 0;
            return GetHandleInformation(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)), &flags) ? 9 : 0;
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
        ggzip::proc::RunOptions limits;
        limits.deadlineTick = GetTickCount64() + 150;
        auto started = GetTickCount64();
        check(throws([&] { ggzip::proc::run(executable, {L"--sleep"}, {}, cancel, limits); }) &&
              GetTickCount64() - started < 5000, "deadline terminates helper promptly");
        limits = {};
        limits.memoryLimit = 32ull * 1024 * 1024;
        check(ggzip::proc::run(executable, {L"--memory"}, {}, cancel, limits).exitCode == 0,
              "job object enforces memory allocation ceiling");
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
        check(ggzip::proc::run(executable, {L"--handle", value}, {}, cancel).exitCode == 0,
              "unrelated inheritable handles are not exposed");
        CloseHandle(event);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return failures ? 1 : 0;
}
