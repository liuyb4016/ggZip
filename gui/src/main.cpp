// ggZip GUI 入口
#include "MainWindow.h"
#include "LanguageManager.h"
#include "Theme.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/FileAssociations.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <cstdio>
#include <exception>
#include <QTimer>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ggZip");
    QApplication::setOrganizationName("ggZip");
    QApplication::setApplicationVersion(GGZIP_VERSION);
    QApplication::setWindowIcon(QIcon(":/ggzip.ico"));
    ggzipui::applyConfiguredTheme();
    LanguageManager language;
    const QString selectedLanguage = QSettings().value("ui/language", "zh_CN").toString();
    if (!language.loadLanguage(selectedLanguage)) language.loadLanguage("en");

    const QStringList arguments = QApplication::arguments();
    if (arguments.contains("--selftest")) {
        if (QApplication::windowIcon().isNull()) return 4;
        return ggzip::createEngine()->available() ? 0 : 3;
    }
    const bool registerAssociations = arguments.contains("--register-associations");
    const bool unregisterAssociations = arguments.contains("--unregister-associations");
    if (registerAssociations || unregisterAssociations) {
        if (registerAssociations && unregisterAssociations) return 2;
        try {
            const std::wstring applicationPath = QApplication::applicationFilePath().toStdWString();
            if (registerAssociations) ggzip::registerFileAssociations(applicationPath);
            else ggzip::unregisterFileAssociations(applicationPath);
            return 0;
        } catch (const std::exception& error) {
            if (!arguments.contains("--quiet"))
                QMessageBox::critical(nullptr, "文件关联失败", QString::fromUtf8(error.what()));
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }

    MainWindow w;
    w.show();
    if (arguments.contains("--shell-action")) {
        const auto actions = arguments.mid(arguments.indexOf("--shell-action") + 1);
        QTimer::singleShot(0, &w, [&w, actions] { w.runShellAction(actions); });
        return app.exec();
    }

    // 支持命令行直接打开压缩包
    for (const QString& path : arguments.mid(1)) {
        if (!path.isEmpty()) w.openArchive(path);
    }
    return app.exec();
}
