#pragma once
#include <QTranslator>
#include <QHash>
#include <memory>

class LanguageManager : public QTranslator {
public:
    explicit LanguageManager(QObject* parent = nullptr);
    bool loadLanguage(const QString& language);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int count = -1) const override;
    bool isEmpty() const override { return translations_.isEmpty(); }
private:
    QHash<QString, QString> translations_;
    std::unique_ptr<QTranslator> standard_;
};
