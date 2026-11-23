#pragma once
// Bandizip 式"智能解压"决策与执行。
#include <atomic>
#include <string>
#include <vector>
#include "Entry.h"
#include "IEngine.h"

namespace ggzip {

struct SmartDecision {
    bool intoSubfolder = false;
};

// 规则：
//   - 包内只有一个顶层目录且顶层无散落文件 -> 直接解到目标目录（避免 a/a/ 双层嵌套）
//   - 其他情况（多个顶层项 / 顶层有散文件） -> 解到目标目录下以压缩包名命名的子文件夹
SmartDecision smartPlan(const std::vector<Entry>& entries);

// 执行解压：smart 时按 smartPlan 决定实际目标目录；tar.* 自动解开内层 .tar。
// 返回实际解压到的目录。
OperationSummary extractSmart(const IEngine& engine, const std::wstring& archive,
                          const std::wstring& outDir, bool smart,
                          const ExtractOptions& opt, const ProgressFn& onProgress,
                          const std::atomic_bool& cancel);

// 在 parentDir 下为 subfolderBase 找一个不冲突的名字（base、base-2、base-3...）
std::wstring dedupeFolderName(const std::wstring& parentDir,
                              const std::wstring& subfolderBase);

} // namespace ggzip
