#include "app/ThemeManager.h"
#include "ui/MainWindow.h"
#include "services/SettingsService.h"
#include "services/LanguageService.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QIcon>
int main(int argc, char* argv[])
{
    QApplication app(argc,argv);
    QCoreApplication::setApplicationName(QStringLiteral("Videx"));
    QCoreApplication::setOrganizationName(QStringLiteral("Videx"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VIDEX_VERSION));
    SettingsService settings;
    QTranslator translator;
    LanguageService::install(translator, settings.loadPreferences().language);
    ThemeManager theme;
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    MainWindow window(&theme, &settings);
    window.show();
    return app.exec();
}
