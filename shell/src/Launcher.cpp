#include "Launcher.h"
#include "Common.h"

#include <thread>
#include <cstdio>
#include <cwchar>

namespace ggshell {

namespace {

std::wstring parentOf(const std::wstring& p) {
    const size_t pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

bool fileExists(const std::wstring& p) {
    const DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// 参数按 Windows 规则加引号（路径含空格时必需）
std::wstring quote(const std::wstring& s) {
    if (!s.empty() && s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    std::wstring r = L"\"";
    for (const wchar_t c : s) {
        if (c == L'"') r += L"\\\"";
        r += c;
    }
    return r + L"\"";
}

void showError(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), L"ggZip", MB_OK | MB_ICONERROR);
}

void runOne(const std::wstring& exe, const std::vector<std::wstring>& args) {
    std::wstring cmdline = quote(exe);
    for (const auto& a : args) cmdline += L" " + quote(a);
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        showError(L"无法启动 ggz.exe（错误码 " + std::to_wstring(GetLastError()) +
                  L"）。\n路径: " + exe);
        return;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        std::wstring what = args.empty() ? L"" : L"\n对象: " + args[0];
        showError(L"ggZip 操作失败（退出码 " + std::to_wstring(code) + L"）。" + what +
                  L"\n可在命令行运行 ggz 查看详细错误。");
    }
}

void dryRun(const wchar_t* file, const std::wstring& line) {
    // 以 UTF-8 追加，一行一条命令，供测试宿主断言。
    // 先写临时名再原子改名：避免读到半截内容（测试侧以“文件出现”为完成信号）
    std::wstring tmp = std::wstring(file) + L".tmp";
    if (FILE* f = _wfopen(tmp.c_str(), L"ab")) {
        int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
        if (len > 1) {
            std::string s(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, s.data(), len,
                                nullptr, nullptr);
            fwrite(s.data(), 1, s.size(), f);
            fwrite("\n", 1, 1, f);
        }
        fclose(f);
        MoveFileExW(tmp.c_str(), file, MOVEFILE_REPLACE_EXISTING);
    }
}

} // namespace

std::wstring findGgz() {
    wchar_t buf[MAX_PATH * 2]{};
    // 1) 环境变量
    if (GetEnvironmentVariableW(L"GGZIP_GGZ", buf, MAX_PATH * 2) > 0 && buf[0])
        return buf;
    // 2) 注册表
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ggZip", L"GgzPath",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0]) {
        std::wstring p = buf;
        if (fileExists(p)) return p;
        if (!fileExists(p += L"\\ggz.exe")) p.clear();
        if (!p.empty()) return p;
    }
    // 3) DLL 同目录
    if (GetModuleFileNameW(g_hInstance, buf, MAX_PATH * 2) > 0) {
        std::wstring p = parentOf(buf) + L"\\ggz.exe";
        if (fileExists(p)) return p;
    }
    // 4) PATH
    return L"ggz.exe";
}

void launchAsync(const std::wstring& exe,
                 const std::vector<std::vector<std::wstring>>& commands) {
    std::thread([exe, commands]() {
        // 线程存续期间钉住本 DLL，避免执行中途被 Explorer 卸载
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&runOne), &pinned);
        wchar_t dry[MAX_PATH * 2]{};
        const bool isDry = GetEnvironmentVariableW(L"GGZIP_SHELL_DRYRUN", dry,
                                                   MAX_PATH * 2) > 0 && dry[0];
        for (const auto& args : commands) {
            std::wstring line = exe;
            for (const auto& a : args) line += L" " + a;
            if (isDry) dryRun(dry, line);
            else runOne(exe, args);
        }
    }).detach();
}

} // namespace ggshell
