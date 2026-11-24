#pragma once
#include "TaskQueue.h"
#include <QDialog>
#include <QPointer>

class QTableWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;

class BatchDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchDialog(ggzip::IEngine& engine, const QStringList& initial = {}, QWidget* parent = nullptr);
protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
private:
    void addInputs();
    void startQueue();
    void refresh();
    ggzip::ConflictDecision ask(const ggzip::Conflict& conflict);
    ggzip::IEngine& engine_;
    TaskQueue queue_;
    QTableWidget* table_;
    QLineEdit* output_;
    QLineEdit* password_;
    QComboBox* mode_;
    QCheckBox* continue_;
    QStringList inputs_;
    bool prepared_ = false;
    bool closing_ = false;
    QPointer<QDialog> conflict_;
};
