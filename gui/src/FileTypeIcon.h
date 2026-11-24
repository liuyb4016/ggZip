#pragma once
// 压缩包条目的彩色类型徽章图标（header-only）。
// 在 QCoreApplication-only 环境（如 gui_regression_test）中返回空 QIcon，
// 避免构造 QPixmap 触发 "Must construct a QGuiApplication" 崩溃。
#include <QCoreApplication>
#include <QGuiApplication>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QString>

namespace ggzipui {

struct TypeBadge {
    const char* label;
    const char* color;
    const char* description;  // 中文原文键，走 QCoreApplication::translate 查表
};

inline TypeBadge badgeFor(const QString& name, bool isDir) {
    if (isDir) return { "DIR", "#e0a83a", "文件夹" };
    const QString ext = name.section(QLatin1Char('.'), -1).toLower();
    if (ext == "zip")  return { "ZIP", "#2f6fed", "ZIP 压缩包" };
    if (ext == "7z")   return { "7Z",  "#2f6fed", "7z 压缩包" };
    if (ext == "rar")  return { "RAR", "#e06c4a", "RAR 压缩包" };
    if (ext == "tar" || ext == "gz" || ext == "bz2" || ext == "xz" || ext == "zst" ||
        ext == "tgz" || ext == "tbz" || ext == "txz" || ext == "tzst")
                       return { "ARC", "#8a63d2", "归档文件" };
    if (ext == "iso" || ext == "cab" || ext == "wim" || ext == "msi")
                       return { "IMG", "#0f9e8e", "磁盘映像" };
    if (ext == "pdf")  return { "PDF", "#d9485f", "PDF 文档" };
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
        ext == "bmp" || ext == "webp" || ext == "ico")
                       return { "PIC", "#c252a8", "图片" };
    if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "aac" ||
        ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov")
                       return { "AV",  "#d97c3a", "音视频" };
    if (ext == "txt" || ext == "md" || ext == "log" || ext == "ini" || ext == "cfg" ||
        ext == "json" || ext == "xml" || ext == "yaml" || ext == "yml" || ext == "csv" ||
        ext == "html" || ext == "css" || ext == "py"  || ext == "js"  || ext == "cpp" ||
        ext == "c"    || ext == "h"   || ext == "hpp" || ext == "cs"  || ext == "bat" ||
        ext == "ps1"  || ext == "srt")
                       return { "TXT", "#5a7f9e", "文本 / 代码" };
    return { "FILE", "#7d8899", "文件" };
}

inline QString typeDescription(const QString& name, bool isDir) {
    return QCoreApplication::translate("ArchiveTableModel", badgeFor(name, isDir).description);
}

inline QIcon fileTypeIcon(const QString& name, bool isDir) {
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return QIcon();
    const TypeBadge badge = badgeFor(name, isDir);
    const QString key = QString::fromLatin1(badge.label) + QLatin1Char('|') +
                        QString::fromLatin1(badge.color);
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(QLatin1String(badge.color)));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(0, 0, size, size, 5, 5);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(badge.label[0] == 'F' ? 6 : 8);  // FILE 标签稍长
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter,
                     QString::fromLatin1(badge.label));
    painter.end();

    const QIcon icon(pixmap);
    cache.insert(key, icon);
    return icon;
}

} // namespace ggzipui
