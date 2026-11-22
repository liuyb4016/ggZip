// ggZip GUI 入口
#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
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
