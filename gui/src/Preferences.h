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
};
