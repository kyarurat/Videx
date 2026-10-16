#include "LanguageService.h"
#include <QCoreApplication>
#include <QTranslator>
#include <QDebug>

bool LanguageService::install(QTranslator& translator, const QString& language)
{
    Q_INIT_RESOURCE(resources);
    if (language != "en_US") return true; // Chinese is the source language.
    if (!translator.load(QStringLiteral(":/translations/videx_en_US.qm"))) {
        qWarning() << "Cannot load English translations; using Chinese";
        return false;
    }
    return QCoreApplication::installTranslator(&translator);
}
