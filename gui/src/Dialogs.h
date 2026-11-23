#pragma once
// 解压 / 压缩 / 预览 / 密码管理对话框。
#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <vector>
#include "ggzip/PasswordStore.h"

// BOM / UTF-8 / 系统编码（GBK）自动识别（GUI 各处共用）
QString ggzDecodeTextPreview(const QByteArray& raw);

class QCheckBox;
class QComboBox;
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
    void setPassword(const QString& password);
    void setSelectedOnly();

private:
    QLineEdit* dirEdit_;
    QLineEdit* pwEdit_;
    QCheckBox* smartCheck_;
    QComboBox* pwPreset_ = nullptr;
    std::vector<ggzip::PasswordEntry> stored_;  // 密码库条目（供下拉填充）
};

// 密码管理器（DPAPI 存储）
class PasswordManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordManagerDialog(QWidget* parent = nullptr);

private:
    void reload();
    void addPassword();
    void removeSelected();
    QListWidget* list_;
};

class QPixmap;

class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    // 显示一张图片预览（已按屏幕尺寸缩放），Esc / 关闭按钮关闭
    PreviewDialog(const QPixmap& pixmap, const QString& fileName,
                  QWidget* parent = nullptr);
};

class TextPreviewDialog : public QDialog {
    Q_OBJECT
public:
    // 文本预览（等宽字体，只读）；truncated 表示内容因超过预览上限被截断
    TextPreviewDialog(const QString& text, const QString& fileName, bool truncated,
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
    QString volumeSize() const;  // 空 = 不分卷

private:
    void addFiles();
    void addFolder();
    void removeSelected();

    QListWidget* inputList_;
    QLineEdit* outEdit_;
    QLineEdit* pwEdit_;
    QComboBox* pwPreset_ = nullptr;
    std::vector<ggzip::PasswordEntry> stored_;
    QComboBox* volCombo_;
    QSpinBox* levelSpin_;
};
