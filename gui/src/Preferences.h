#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;

class ArchivePasswordDialog : public QDialog {
public:
    explicit ArchivePasswordDialog(const QString& archive, QWidget* parent = nullptr);
    QString password() const;
private:
    QLineEdit* password_;
};

class PreferencesDialog : public QDialog {
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
    // 是否有改动需要重启才能生效（目前仅界面语言）
    bool needsRestart() const { return needsRestart_; }
private:
    bool needsRestart_ = false;
};
