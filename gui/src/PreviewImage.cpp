#include "PreviewImage.h"
#include "ProcessUtil.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include <QCoreApplication>
#include <QDir>
#include <qt_windows.h>
#include <cstring>
#include <algorithm>

QImage decodePreviewImage(const QString& path, std::uint64_t pixels, std::uint64_t deadline,
                         const std::atomic_bool& cancel) {
    if (!pixels || pixels > 256000000) throw ggzip::ArchiveError(ggzip::diagnostic("预览像素设置无效"));
    const QString executable = QDir(QCoreApplication::applicationDirPath()).filePath("ggzip_preview_worker.exe");
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) throw ggzip::ArchiveError(ggzip::diagnostic("无法检查可用内存"));
    ggzip::proc::RunOptions options;
    options.deadlineTick = deadline;
    options.memoryLimit = std::min<std::uint64_t>(1024ull * 1024 * 1024, memory.ullAvailPhys / 2);
    options.inputFile = path.toStdWString();
    const std::uint64_t maximum = 12 + pixels * 4;
    std::string output;
    std::vector<std::wstring> arguments{std::to_wstring(pixels)};
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    if (QDir(applicationDirectory + "/imageformats").exists()) arguments.push_back(applicationDirectory.toStdWString());
    const auto result = ggzip::proc::run(executable.toStdWString(), arguments,
        [&](std::string_view chunk) {
            if (chunk.size() > maximum - output.size()) throw ggzip::ArchiveError(ggzip::diagnostic("解码结果超过限制"));
            output.append(chunk);
            return true;
        }, cancel, options);
    if (cancel.load()) throw ggzip::ArchiveError(ggzip::diagnostic("预览已取消"));
    if (result.exitCode != 0 || output.size() < 12)
        throw ggzip::ArchiveError(ggzip::diagnostic("图片无法安全解码，可能超出资源限制或格式不受支持"));
    std::uint32_t header[3]{};
    std::memcpy(header, output.data(), sizeof(header));
    const std::uint64_t count = static_cast<std::uint64_t>(header[1]) * header[2];
    if (header[0] != 0x475a5056 || !header[1] || !header[2] ||
        count > pixels || output.size() - 12 != count * 4)
        throw ggzip::ArchiveError(ggzip::diagnostic("图片辅助进程返回无效数据"));
    return QImage(reinterpret_cast<const uchar*>(output.data() + 12), static_cast<int>(header[1]),
        static_cast<int>(header[2]), QImage::Format_RGBA8888).copy();
}
