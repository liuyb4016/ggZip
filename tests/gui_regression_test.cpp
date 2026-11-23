#include "ArchiveTableModel.h"
#include "Dialogs.h"
#include "PreviewImage.h"
#include "ProgressRunner.h"
#include "preview_fixtures.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

namespace {
int failures = 0;
int checks = 0;
void check(bool result, const char* label) {
    ++checks;
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
bool throws(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--deployed"))
        QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    try {
        ArchiveTableModel model;
        std::vector<ggzip::Entry> entries;
        for (const auto& path : {L"root.txt", L"dir/first.txt", L"dir/指南/中文.txt", L"directory.txt"}) {
            ggzip::Entry entry;
            entry.path = path;
            entries.push_back(entry);
        }
        model.setEntries(entries);
        check(model.rowCount() == 3 && model.allCount() == 4, "implicit archive directories are navigable");
        model.setPrefix("dir/");
        check(model.rowCount() == 2, "directory navigation uses slash-delimited prefixes correctly");
        model.setSearch("中文", false);
        check(model.rowCount() == 0, "current-directory filter does not select nested entries");
        model.setSearch("中文", true);
        check(model.rowCount() == 1 && model.entryAt(0)->path == L"dir/指南/中文.txt",
              "whole-archive search retains original full member path");
        model.setSearch("", false);
        model.setPrefix("dir/指南/");
        check(model.rowCount() == 1, "search result can be located in its containing directory");
        model.sort(0, Qt::DescendingOrder);
        check(model.entryAt(0)->path == L"dir/指南/中文.txt", "sorting preserves extraction identity");
        check(ggzDecodeTextPreview(QByteArray::fromHex("efbbbf") + "UTF8") == "UTF8", "UTF-8 BOM text preview");
        check(ggzDecodeTextPreview({}).isEmpty(), "empty text preview");
        check(ggzDecodeTextPreview(QByteArray::fromHex("fffe2d4e8765")) == QString::fromUtf8("中文"),
              "UTF-16 BOM text preview");
        check(ggzDecodeTextPreview(QString::fromUtf8("中文").toUtf8()) == QString::fromUtf8("中文"),
              "UTF-8 Chinese text preview");
        previewtest::Directory temporary;
        std::atomic_bool cancel{false};
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (const auto& format : {"PNG", "JPEG", "BMP", "ICO", "WEBP"}) {
            const QString path = QString::fromStdWString((temporary.path / (std::string("image.") + format)).wstring());
            check(image.save(path, format), "declared image codec is available");
            const QImage decoded = decodePreviewImage(path, 64000000, GetTickCount64() + 30000, cancel);
            check(decoded.size() == image.size(), "isolated helper actually decodes packaged image format");
        }
        const auto gifPath = temporary.path / "image.gif";
        const QByteArray gif = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
        previewtest::write(gifPath, std::string(gif.constData(), static_cast<std::size_t>(gif.size())));
        check(decodePreviewImage(QString::fromStdWString(gifPath.wstring()), 64000000,
              GetTickCount64() + 30000, cancel).size() == QSize(1, 1), "GIF decoder reads real fixture");
        const auto damaged = temporary.path / "bad.png";
        previewtest::write(damaged, "NOT AN IMAGE");
        check(throws([&] { decodePreviewImage(QString::fromStdWString(damaged.wstring()), 64000000,
              GetTickCount64() + 30000, cancel); }), "damaged image fails safely");
        const auto large = temporary.path / "large.png";
        image.save(QString::fromStdWString(large.wstring()), "PNG");
        check(throws([&] { decodePreviewImage(QString::fromStdWString(large.wstring()), 1,
              GetTickCount64() + 30000, cancel); }), "pixel ceiling rejects image before allocation");
        cancel.store(true);
        check(throws([&] { decodePreviewImage(QString::fromStdWString(large.wstring()), 64000000,
              GetTickCount64() + 30000, cancel); }), "cancelled image preview is not published");
        ProgressRunner runner;
        QEventLoop loop;
        bool finished = false;
        bool guiThread = false;
        QObject::connect(&runner, &ProgressRunner::finished, &loop, [&](bool ok, const QString&) {
            finished = !ok;
            guiThread = QThread::currentThread() == application.thread();
            loop.quit();
        });
        check(runner.start([](const std::atomic_bool& stopped, const ProgressRunner::OnProgress&) {
            while (!stopped.load()) Sleep(10);
        }), "background runner starts");
        check(!runner.start({}), "background runner rejects overlapping work");
        QTimer::singleShot(30, &runner, &ProgressRunner::requestCancel);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        runner.wait();
        check(finished && guiThread && !runner.isRunning(), "cancelled completion is delivered on GUI thread");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
