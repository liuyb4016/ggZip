// ggZip GUI 入口
#include "MainWindow.h"
#include "ggzip/EngineFactory.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    // --selftest：验证压缩引擎可定位后退出（供 CI 使用）
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--selftest")
            return ggzip::createEngine()->available() ? 0 : 3;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("ggZip");
    QApplication::setOrganizationName("ggZip");

    MainWindow w;
    w.resize(920, 620);
    w.show();

    // 支持命令行直接打开压缩包
    for (int i = 1; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        if (!path.isEmpty()) w.openArchive(path);
    }
    return app.exec();
}
