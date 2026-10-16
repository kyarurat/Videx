#pragma once
#include <QString>
class QTranslator;
// The translator must outlive the UI. Language changes apply at the next launch.
class LanguageService
{
public:
    static bool install(QTranslator& translator, const QString& language);
};
