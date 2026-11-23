#pragma once
// 子进程执行工具（Windows）。
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ggzip::proc {

struct RunResult {
    int exitCode = -1;
    std::string stderrTail;   // stderr 最后 8KB（UTF-8）
};

struct RunOptions {
    std::uint64_t deadlineTick = 0;
    std::uint64_t memoryLimit = 0;
    std::wstring inputFile;
    std::function<void()> monitor;
};

// 启动 exe，在独立读取线程上流式回调 stdout 数据块。
// onStdout 返回 false、或 cancel 置位 -> 终止子进程，此时 exitCode 为 -1。
RunResult run(const std::wstring& exe, const std::vector<std::wstring>& args,
              const std::function<bool(std::string_view chunk)>& onStdout,
              const std::atomic_bool& cancel, const RunOptions& options = {});

} // namespace ggzip::proc
