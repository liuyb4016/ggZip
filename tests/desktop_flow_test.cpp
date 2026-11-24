#include "MainWindow.h"
#include "ArchiveTableModel.h"
#include "Dialogs.h"
#include "Preferences.h"
#include "ProgressRunner.h"
#include "ggzip/EngineFactory.h"
#include "preview_fixtures.h"
#include <QApplication>
#include <QEventLoop>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QTableView>
#include <QTimer>
#include <QAction>
#include <QPlainTextEdit>
#include <QLabel>
#include <cstdio>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool result, const char* label) {
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
bool until(const std::function<bool()>& finished, const std::function<void()>& modal = {}) {
    QEventLoop loop;
    QTimer poll;
    bool completed = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (modal) modal();
        if (finished()) { completed = true; loop.quit(); }
    });
    poll.start(20);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return completed;
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication application(argc, argv);
    QApplication::setOrganizationName("ggZipTests");
    QApplication::setApplicationName("DesktopFlows");
    try {
        previewtest::Directory temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromStdWString(temporary.path.wstring()));
        _wputenv_s(L"APPDATA", temporary.path.c_str());
        auto engine = ggzip::createEngine();
        std::atomic_bool cancel{false};
        const auto archive = temporary.path / "browse.zip";
        previewtest::zip(archive, {{"dir/one.txt", "ONE"}, {"dir/sub/two.txt", "TWO"}, {"other.txt", "OTHER"}});
        MainWindow window;
        auto* table = window.findChild<QTableView*>("archiveTable");
        auto* runner = window.findChild<ProgressRunner*>();
        auto* model = qobject_cast<ArchiveTableModel*>(table->model());
        window.openArchive(QString::fromStdWString(archive.wstring()));
        check(until([&] { return !runner->isRunning(); }) && model->allCount() == 3,
              "GUI asynchronously opens and lists an archive");
        model->setPrefix("dir/");
        check(model->rowCount() == 2, "GUI archive navigation exposes inferred directories");
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->entryAt(row)->path == L"dir/one.txt") table->selectRow(row);
        auto* selected = window.findChild<QAction*>("extractSelected");
        check(selected && selected->isEnabled(), "selected extraction action follows the actual selection");
        const auto output = temporary.path / "selected";
        QTimer extractionDialog;
        QObject::connect(&extractionDialog, &QTimer::timeout, &window, [&] {
            if (auto* dialog = dynamic_cast<ExtractDialog*>(QApplication::activeModalWidget())) {
                dialog->findChild<QLineEdit*>("extractTarget")->setText(QString::fromStdWString(output.wstring()));
                dialog->accept();
            }
        });
        extractionDialog.start(20);
        selected->trigger();
        extractionDialog.stop();
        check(until([&] { return !runner->isRunning(); }, [] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
        }) && fs::exists(output / "dir/one.txt") && !fs::exists(output / "other.txt"),
              "GUI selected extraction preserves structure and excludes unrelated files");
        const auto input = temporary.path / "secret.txt";
        previewtest::write(input, "SECRET");
        const auto encrypted = temporary.path / "encrypted.7z";
        ggzip::CreateOptions creation;
        creation.password = L"fixture-password";
        engine->create(encrypted.wstring(), {input.wstring()}, creation, {}, cancel);
        int prompts = 0;
        window.openArchive(QString::fromStdWString(encrypted.wstring()));
        check(until([&] { return !runner->isRunning() && model->allCount() == 1; }, [&] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                std::printf("Unexpected dialog: %s\n", message->text().toUtf8().constData());
                message->accept();
            }
            if (auto* dialog = dynamic_cast<ArchivePasswordDialog*>(QApplication::activeModalWidget())) {
                ++prompts;
                dialog->findChild<QLineEdit*>("archivePassword")->setText(prompts == 1 ? "wrong" : "fixture-password");
                dialog->accept();
            }
        }) && prompts == 2, "encrypted browsing retries an incorrect password without freezing the GUI");
        check(!fs::exists(temporary.path / "ggZip/passwords.dat"), "GUI session passwords are not saved automatically");
        bool previewShown = false;
        QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                  Q_ARG(QModelIndex, model->index(0, 0)));
        check(until([&] { return !runner->isRunning() && previewShown; }, [&] {
            if (auto* dialog = dynamic_cast<TextPreviewDialog*>(QApplication::activeModalWidget())) {
                previewShown = dialog->findChild<QPlainTextEdit*>()->toPlainText() == "SECRET";
                dialog->accept();
            }
        }), "encrypted member preview uses the scoped password session in the background");
        previewShown = false;
        QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                  Q_ARG(QModelIndex, model->index(0, 0)));
        runner->requestCancel();
        check(until([&] { return !runner->isRunning(); }, [&] {
            if (auto* dialog = dynamic_cast<TextPreviewDialog*>(QApplication::activeModalWidget())) {
                previewShown = true;
                dialog->accept();
            }
        }) && !previewShown, "cancelled GUI preview cannot display a previous cached result");
        window.openArchive(QString::fromStdWString(archive.wstring()));
        check(until([&] { return !runner->isRunning(); }) && model->allCount() == 3,
              "switching archives restores normal browsing");
        window.openArchive(QString::fromStdWString(encrypted.wstring()));
        check(until([&] { return !runner->isRunning(); }, [&] {
            if (auto* dialog = dynamic_cast<ArchivePasswordDialog*>(QApplication::activeModalWidget())) {
                ++prompts;
                dialog->reject();
            }
        }) && prompts == 3, "switching archives clears the previous password session");
        const auto display = temporary.path / "display.zip";
        previewtest::zip(display, {{"<img src=nonexistent-file.png>/", "", 0x41ed0010}});
        window.openArchive(QString::fromStdWString(display.wstring()));
        check(until([&] { return !runner->isRunning(); }) && model->rowCount() == 1,
              "untrusted directory display fixture opens without extraction");
        QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                  Q_ARG(QModelIndex, model->index(0, 0)));
        check(window.findChild<QLabel*>("archivePath")->textFormat() == Qt::PlainText,
              "archive member names cannot invoke rich-text image loading in navigation");
        window.close();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return failures ? 1 : 0;
}
