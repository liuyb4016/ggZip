#pragma once
// 解压 / 压缩 / 预览对话框。
#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

class ExtractDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExtractDialog(const QString& archivePath, bool smartDefault = false,
                           QWidget* parent = nullptr);

    QString targetDir() const;
    QString password() const;
    bool smart() const;

private:
    QLineEdit* dirEdit_;
    QLineEdit* pwEdit_;
    QCheckBox* smartCheck_;
};

class QPixmap;

class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    // 显示一张图片预览（已按屏幕尺寸缩放），Esc / 关闭按钮关闭
    PreviewDialog(const QPixmap& pixmap, const QString& fileName,
                  QWidget* parent = nullptr);
};

// 注意：不叫 CreateDialog——Win32 头文件里有 CreateDialog 宏，会冲突
class CreateArchiveDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateArchiveDialog(QWidget* parent = nullptr);

    QStringList inputs() const;
    QString archivePath() const;
    QString password() const;
    int level() const;

private:
    void addFiles();
    void addFolder();
    void removeSelected();

    QListWidget* inputList_;
    QLineEdit* outEdit_;
    QLineEdit* pwEdit_;
    QSpinBox* levelSpin_;
};
