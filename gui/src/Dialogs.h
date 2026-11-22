#pragma once
// 解压 / 压缩对话框。
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

class CreateDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateDialog(QWidget* parent = nullptr);

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
