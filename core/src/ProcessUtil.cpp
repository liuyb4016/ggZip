#include "ProcessUtil.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <exception>
#include <thread>

namespace ggzip::proc {
namespace {

std::wstring quoteArg(const std::wstring& a) {
    std::wstring s;
    s.reserve(a.size() + 2);
    s += L'"';
    for (wchar_t c : a) {
        if (c == L'"') s += L"\\\"";
        else s += c;
    }
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
        throw ArchiveError("CreatePipe 失败");
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
              const std::atomic_bool& cancel) {
    std::wstring cmdline = quoteArg(exe);
    for (const auto& a : args) {
        cmdline += L' ';
        cmdline += quoteArg(a);
    }

    Pipe out = makePipe();
    Pipe err = makePipe();

    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE stdinNul = CreateFileW(L"NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = stdinNul ? stdinNul : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out.write;
    si.hStdError  = err.write;

    std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    DWORD createError = GetLastError();
    if (stdinNul) CloseHandle(stdinNul);
    // 父进程必须关闭自己的写端，否则子进程退出后读端收不到 EOF
    closeHandleSafe(out.write);
    closeHandleSafe(err.write);
    if (!ok) {
        closeHandleSafe(out.read);
        closeHandleSafe(err.read);
        throw ArchiveError("无法启动进程: " + utf8(exe) + " (Win32 错误码 " +
                    std::to_string(createError) + ")");
    }

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
                         [&](std::string_view c) { errAcc.append(c); return true; }));

    // 等待子进程退出或取消（每 50ms 轮询一次取消标记）
    for (;;) {
        DWORD w = WaitForSingleObject(pi.hProcess, 50);
        if (w == WAIT_OBJECT_0) break;
        if (cancel.load(std::memory_order_relaxed) ||
            stopRequested.load(std::memory_order_relaxed)) {
            TerminateProcess(pi.hProcess, (UINT)-1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            break;
        }
    }

    tOut.join();
    tErr.join();

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    closeHandleSafe(pi.hThread);
    closeHandleSafe(pi.hProcess);

    if (pending) std::rethrow_exception(pending);

    RunResult r;
    bool cancelled = cancel.load() || stopRequested.load();
    r.exitCode = cancelled ? -1 : (int)code;
    r.stderrTail = tailOf(errAcc, 8192);
    return r;
}

} // namespace ggzip::proc
