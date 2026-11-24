#include "ProcessUtil.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <psapi.h>
#include <exception>
#include <thread>
#include <algorithm>

namespace ggzip::proc {
namespace {

std::wstring quoteArg(const std::wstring& a) {
    std::wstring s;
    s.reserve(a.size() + 2);
    s += L'"';
    std::size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        s.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        s += c;
    }
    s.append(backslashes * 2, L'\\');
    s += L'"';
    return s;
}

std::string tailOf(const std::string& s, size_t n) {
    return s.size() <= n ? s : s.substr(s.size() - n);
}

struct Pipe {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
};

Pipe makePipe() {
    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    Pipe p;
    if (!CreatePipe(&p.read, &p.write, &sa, 0))
        throw ArchiveError(diagnostic("CreatePipe 失败"));
    return p;
}

void closeHandleSafe(HANDLE& h) {
    if (h) { CloseHandle(h); h = nullptr; }
}

// 读到 EOF 或 sink 返回 false；结束时关闭句柄。
void readAll(HANDLE file, const std::function<bool(std::string_view)>& sink) {
    char buf[65536];
    for (;;) {
        DWORD got = 0;
        BOOL ok = ReadFile(file, buf, sizeof(buf), &got, nullptr);
        if (!ok || got == 0) break;
        if (!sink(std::string_view(buf, (size_t)got))) break;
    }
    CloseHandle(file);
}

} // namespace

RunResult run(const std::wstring& exe, const std::vector<std::wstring>& args,
              const std::function<bool(std::string_view chunk)>& onStdout,
              const std::atomic_bool& cancel, const RunOptions& options) {
    if (exe.find(L'\0') != std::wstring::npos || options.inputFile.find(L'\0') != std::wstring::npos ||
        options.workingDirectory.find(L'\0') != std::wstring::npos ||
        std::any_of(args.begin(), args.end(), [](const auto& argument) { return argument.find(L'\0') != std::wstring::npos; }))
        throw ArchiveError(diagnostic("进程参数包含非法空字符"));
    std::wstring cmdline = quoteArg(exe);
    for (const auto& a : args) {
        cmdline += L' ';
        cmdline += quoteArg(a);
    }
    if (cmdline.size() >= 32767) throw ArchiveError(diagnostic("命令行超过 Windows 长度限制，请减少本次输入数量或缩短路径"));

    Pipe out = makePipe();
    Pipe err = makePipe();

    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    const std::wstring input = options.inputFile.empty() ? L"NUL" : options.inputFile;
    HANDLE stdinNul = CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  &sa, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (stdinNul == INVALID_HANDLE_VALUE) {
        closeHandleSafe(out.read); closeHandleSafe(out.write);
        closeHandleSafe(err.read); closeHandleSafe(err.write);
        throw ArchiveError(diagnostic("无法创建只读输入句柄"));
    }
    if (!options.inputFile.empty()) {
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(stdinNul, &information) ||
            (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(stdinNul);
            closeHandleSafe(out.read); closeHandleSafe(out.write);
            closeHandleSafe(err.read); closeHandleSafe(err.write);
            throw ArchiveError(diagnostic("辅助进程输入必须为普通文件"));
        }
    }
    STARTUPINFOEXW startup{};
    auto& si = startup.StartupInfo;
    si.cb = sizeof(startup);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinNul;
    si.hStdOutput = out.write;
    si.hStdError  = err.write;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributeBuffer(attributeBytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
    HANDLE inherited[] = {stdinNul, out.write, err.write};
    const bool attributesInitialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes);
    const bool attributesReady = attributesInitialized &&
        UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr);
    if (!attributesReady) {
        if (attributesInitialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(stdinNul);
        closeHandleSafe(out.read); closeHandleSafe(out.write);
        closeHandleSafe(err.read); closeHandleSafe(err.write);
        throw ArchiveError(diagnostic("无法设置辅助进程句柄隔离"));
    }

    std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (options.memoryLimit) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(options.memoryLimit);
        limits.JobMemoryLimit = static_cast<SIZE_T>(options.memoryLimit);
    }
    const BOOL configured = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    BOOL ok = configured && CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str(), &si, &pi);
    DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (stdinNul) CloseHandle(stdinNul);
    // 父进程必须关闭自己的写端，否则子进程退出后读端收不到 EOF
    closeHandleSafe(out.write);
    closeHandleSafe(err.write);
    if (!ok) {
        closeHandleSafe(job);
        closeHandleSafe(out.read);
        closeHandleSafe(err.read);
        throw ArchiveError(diagnostic("无法启动进程: ") + utf8(exe) + diagnostic(" (Win32 错误码 ") +
                    std::to_string(createError) + ")");
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        closeHandleSafe(out.read); closeHandleSafe(err.read); closeHandleSafe(job);
        throw ArchiveError(diagnostic("无法隔离辅助进程"));
    }
    const auto started = GetTickCount64();
    ResumeThread(pi.hThread);
    RunMetrics metrics;
    auto sampleWorkingSet = [&] {
        if (!options.collectMetrics) return;
        PROCESS_MEMORY_COUNTERS information{};
        information.cb = sizeof(information);
        if (GetProcessMemoryInfo(pi.hProcess, &information, sizeof(information)))
            metrics.rootPeakWorkingSetBytes = std::max(metrics.rootPeakWorkingSetBytes,
                static_cast<std::uint64_t>(information.PeakWorkingSetSize));
    };

    std::string errAcc;
    std::atomic_bool stopRequested{ false };
    std::exception_ptr pending;

    auto sinkWrap = [&](std::string_view chunk) -> bool {
        if (pending) return false;
        try {
            if (onStdout && !onStdout(chunk)) {
                stopRequested.store(true);
                return false;
            }
        } catch (...) {
            pending = std::current_exception();
            stopRequested.store(true);
            return false;
        }
        return true;
    };

    std::thread tOut(readAll, out.read,
                     std::function<bool(std::string_view)>(sinkWrap));
    std::thread tErr(readAll, err.read,
                     std::function<bool(std::string_view)>(
                         [&](std::string_view c) {
                             errAcc.append(c);
                             if (errAcc.size() > 8192) errAcc.erase(0, errAcc.size() - 8192);
                             return true;
                         }));

    bool timedOut = false;
    std::exception_ptr monitorError;
    for (;;) {
        DWORD w = WaitForSingleObject(pi.hProcess, 50);
        sampleWorkingSet();
        if (w == WAIT_OBJECT_0) break;
        try {
            if (w == WAIT_FAILED) throw ArchiveError(diagnostic("等待辅助进程失败"));
            if (options.monitor) options.monitor();
        } catch (...) { monitorError = std::current_exception(); }
        timedOut = options.deadlineTick && GetTickCount64() >= options.deadlineTick;
        if (timedOut || monitorError || cancel.load(std::memory_order_relaxed) ||
            stopRequested.load(std::memory_order_relaxed)) {
            TerminateJobObject(job, static_cast<UINT>(-1));
            WaitForSingleObject(pi.hProcess, INFINITE);
            break;
        }
    }

    if (options.collectMetrics) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
        JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION accounting{};
        metrics.available = QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
            &memory, sizeof(memory), nullptr) &&
            QueryInformationJobObject(job, JobObjectBasicAndIoAccountingInformation,
                &accounting, sizeof(accounting), nullptr);
        metrics.elapsedMs = GetTickCount64() - started;
        metrics.jobPeakCommitBytes = memory.PeakJobMemoryUsed;
        metrics.cpuTimeMs = static_cast<std::uint64_t>(
            accounting.BasicInfo.TotalUserTime.QuadPart + accounting.BasicInfo.TotalKernelTime.QuadPart) / 10000;
        metrics.jobReadBytes = accounting.IoInfo.ReadTransferCount;
        metrics.jobWriteBytes = accounting.IoInfo.WriteTransferCount;
        metrics.jobProcessCount = accounting.BasicInfo.TotalProcesses;
    }
    closeHandleSafe(job);
    tOut.join();
    tErr.join();

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    closeHandleSafe(pi.hThread);
    closeHandleSafe(pi.hProcess);
    closeHandleSafe(job);

    if (pending) std::rethrow_exception(pending);
    if (monitorError) std::rethrow_exception(monitorError);
    if (timedOut) throw ArchiveError(diagnostic("操作超时，辅助进程已停止"));

    RunResult r;
    bool cancelled = cancel.load() || stopRequested.load();
    r.exitCode = cancelled ? -1 : (int)code;
    r.stderrTail = tailOf(errAcc, 8192);
    r.metrics = metrics;
    return r;
}

} // namespace ggzip::proc
