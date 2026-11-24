#include "benchmark_support.h"
#include "preview_fixtures.h"
#include "PreviewImage.h"
#include <QCoreApplication>
#include <QColor>
#include <QDir>

namespace fs = std::filesystem;
namespace {

fs::path bitmap(ggzip::files::Workspace& workspace, const fs::path& relative, unsigned int dimension, bool body) {
    const unsigned int stride = (dimension * 3 + 3) & ~3u;
    std::string header;
    previewtest::append16(header, 0x4d42);
    previewtest::append32(header, 54 + stride * dimension);
    previewtest::append32(header, 0);
    previewtest::append32(header, 54);
    previewtest::append32(header, 40);
    previewtest::append32(header, dimension);
    previewtest::append32(header, dimension);
    previewtest::append16(header, 1);
    previewtest::append16(header, 24);
    previewtest::append32(header, 0);
    previewtest::append32(header, stride * dimension);
    for (int index = 0; index < 4; ++index) previewtest::append32(header, 0);
    const auto path = workspace.write(relative, header);
    if (!body) return path;
    const benchmark::Handle output(CreateFileW(ggzip::win32Path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER end{};
    if (output.value == INVALID_HANDLE_VALUE || !SetFilePointerEx(output.value, end, nullptr, FILE_END))
        throw std::runtime_error("Cannot append bitmap fixture data");
    std::vector<unsigned char> row(stride, 0);
    for (unsigned int pixel = 0; pixel < dimension; ++pixel) {
        row[pixel * 3] = 11;
        row[pixel * 3 + 1] = 22;
        row[pixel * 3 + 2] = 33;
    }
    for (unsigned int index = 0; index < dimension; ++index) {
        DWORD written = 0;
        if (!WriteFile(output.value, row.data(), stride, &written, nullptr) || written != stride)
            throw std::runtime_error("Cannot write bitmap fixture data");
    }
    if (!FlushFileBuffers(output.value)) throw std::runtime_error("Cannot flush bitmap fixture data");
    return path;
}

}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        const auto arguments = application.arguments();
        if (arguments.size() == 4 && arguments[1] == "--decode") {
            const auto dimension = arguments[3].toUInt();
            std::atomic_bool cancel{false};
            const auto image = decodePreviewImage(arguments[2], 64000000, GetTickCount64() + 90000, cancel);
            return !image.isNull() && image.width() == static_cast<int>(dimension) &&
                image.height() == static_cast<int>(dimension) &&
                image.pixelColor(0, 0) == QColor(33, 22, 11) &&
                image.pixelColor(image.width() - 1, image.height() - 1) == QColor(33, 22, 11) ? 0 : 9;
        }
        benchmark::Profile profile;
        profile.imageDimension = 2048;
        for (int index = 1; index < arguments.size(); ++index) {
            const auto option = arguments[index];
            if (index + 1 == arguments.size()) throw std::runtime_error("Missing image benchmark option value");
            const auto value = arguments[++index];
            if (option == "--report") profile.report = value.toStdWString();
            else if (option == "--dimension") profile.imageDimension = benchmark::number(value.toStdWString().c_str(), 8000);
            else if (option == "--iterations") profile.iterations = benchmark::number(value.toStdWString().c_str(), 3);
            else throw std::runtime_error("Unknown image benchmark option");
        }
        if (profile.imageDimension < 2048) throw std::runtime_error("Image benchmark requires a dimension of at least 2048");
        if (!profile.report.empty() && ggzip::files::identity(fs::absolute(profile.report)).exists)
            throw std::runtime_error("Benchmark report already exists; choose a new path");
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory)) throw std::runtime_error("Cannot inspect available memory");
        const auto pixels = static_cast<std::uint64_t>(profile.imageDimension) * profile.imageDimension;
        const auto requiredMemory = pixels * 24 + 256ull * 1024 * 1024;
        if (memory.ullAvailPhys < requiredMemory * 2 ||
            fs::space(fs::temp_directory_path()).available < pixels * 3 + 512ull * 1024 * 1024) {
            std::puts("SKIP: insufficient safe memory or disk headroom for the selected image workload");
            return 77;
        }
        const auto limit = std::min<std::uint64_t>(2ull * 1024 * 1024 * 1024, memory.ullAvailPhys / 2);
        ggzip::files::Workspace workspace(fs::temp_directory_path());
        profile.startingFreeDiskBytes = fs::space(workspace.root).available;
        profile.persistentAcls = ggzip::fileSystemCapabilities(workspace.root).persistentAcls;
        if (profile.startingFreeDiskBytes < pixels * 3 + 512ull * 1024 * 1024) {
            std::puts("SKIP: insufficient private staging volume space");
            return 77;
        }
        const auto normal = bitmap(workspace, "normal.bmp", profile.imageDimension, true);
        const auto originalDigest = benchmark::sha256(normal);
        const auto originalState = ggzip::files::identity(normal);
        const auto oversized = bitmap(workspace, "oversized-header.bmp", 8001, false);
        const auto truncated = bitmap(workspace, "truncated.bmp", 2048, false);
        const auto executable = fs::path(application.applicationFilePath().toStdWString());
        const auto worker = executable.parent_path() / "ggzip_preview_worker.exe";
        profile.targetDigest = benchmark::sha256(executable);
        profile.helperDigest = benchmark::sha256(worker);
        std::vector<benchmark::Record> records;
        for (unsigned int iteration = 1; iteration <= profile.iterations; ++iteration) {
            auto decoded = benchmark::run("iteration-" + std::to_string(iteration) + "/real-preview-decode", "success",
                executable, {L"--decode", normal.wstring(), std::to_wstring(profile.imageDimension)}, workspace, limit);
            decoded.verified = decoded.process.exitCode == 0 &&
                ggzip::files::same(ggzip::files::identity(normal), originalState) && benchmark::sha256(normal) == originalDigest;
            records.push_back(std::move(decoded));
        }
        auto denied = benchmark::run("aggregate-preview-memory-denial", "memory-failure-with-input-retained",
            executable, {L"--decode", normal.wstring(), std::to_wstring(profile.imageDimension)},
            workspace, 32ull * 1024 * 1024);
        denied.verified = denied.process.exitCode != 0 && denied.process.exitCode != -1 &&
            ggzip::files::same(ggzip::files::identity(normal), originalState) && benchmark::sha256(normal) == originalDigest;
        records.push_back(std::move(denied));
        const std::vector<std::wstring> workerArguments{L"64000000"};
        auto rejected = benchmark::run("oversized-pixel-header", "pixel-limit-rejected-before-decode",
            worker, workerArguments, workspace, 128ull * 1024 * 1024, oversized.wstring());
        rejected.verified = rejected.process.exitCode == 3;
        records.push_back(std::move(rejected));
        auto damaged = benchmark::run("truncated-image", "decode-failure",
            worker, workerArguments, workspace, 128ull * 1024 * 1024, truncated.wstring());
        damaged.verified = damaged.process.exitCode == 4;
        records.push_back(std::move(damaged));
        return benchmark::report(profile, records) ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
