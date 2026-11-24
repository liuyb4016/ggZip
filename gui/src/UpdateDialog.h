#pragma once
#include <QDialog>
#include <QUrl>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QNetworkReply;
class QNetworkAccessManager;

struct UpdateInfo {
    QString version;
    QString notes;
    QUrl releaseUrl;
    bool newer = false;
};

UpdateInfo parseUpdate(const QByteArray& data, const QString& currentVersion);

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(QWidget* parent = nullptr);
private:
    QNetworkAccessManager* network_;
    QNetworkReply* reply_ = nullptr;
    QLabel* status_;
    QPlainTextEdit* notes_;
    QPushButton* open_;
    QByteArray received_;
    QUrl releaseUrl_;
};
