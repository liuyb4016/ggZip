#include "LanguageManager.h"
#include "ggzip/Diagnostics.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>

LanguageManager::LanguageManager(QObject* parent) : QTranslator(parent) {}

bool LanguageManager::loadLanguage(const QString& language) {
    if (language != "zh_CN" && language != "en") return false;
    if (language == "zh_CN") {
        auto standard = std::make_unique<QTranslator>();
        const QString deployed = QCoreApplication::applicationDirPath() + "/translations";
        if (!standard->load("qtbase_zh_CN", deployed) &&
            !standard->load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) return false;
        QCoreApplication::removeTranslator(this);
        if (standard_) QCoreApplication::removeTranslator(standard_.get());
        translations_.clear();
        standard_ = std::move(standard);
        QCoreApplication::installTranslator(standard_.get());
        ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::SimplifiedChinese);
        return true;
    }
    QFile file(":/translations/en.json");
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const auto catalog = document.object();
    QHash<QString, QString> translated;
    for (auto iterator = catalog.begin(); iterator != catalog.end(); ++iterator) {
        if (!iterator.value().isString() || iterator.value().toString().isEmpty()) return false;
        translated.insert(iterator.key(), iterator.value().toString());
    }
    if (translated.isEmpty()) return false;
    QCoreApplication::removeTranslator(this);
    if (standard_) QCoreApplication::removeTranslator(standard_.get());
    standard_.reset();
    translations_ = std::move(translated);
    QCoreApplication::installTranslator(this);
    ggzip::setDiagnosticLanguage(ggzip::DiagnosticLanguage::English);
    return true;
}

QString LanguageManager::translate(const char*, const char* sourceText, const char*, int) const {
    return translations_.value(QString::fromUtf8(sourceText));
}
