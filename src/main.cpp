#include "app/ThemeManager.h"
#include "ui/MainWindow.h"
#include "services/SettingsService.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QIcon>
int main(int argc, char* argv[])
{
    QApplication app(argc,argv);
    QCoreApplication::setApplicationName(QStringLiteral("Videx"));
    QCoreApplication::setOrganizationName(QStringLiteral("Videx"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0-rc.1"));
    QTranslator translator;
    // Chinese source strings are the fallback until additional catalogs are shipped.
    if (translator.load(QLocale(QStringLiteral("zh_CN")),QStringLiteral("videx"),QStringLiteral("_"),
                        QCoreApplication::applicationDirPath()+QStringLiteral("/translations")))
        app.installTranslator(&translator);
    ThemeManager theme;
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    SettingsService settings;
    MainWindow window(&theme, &settings);
    window.show();
    return app.exec();
}
