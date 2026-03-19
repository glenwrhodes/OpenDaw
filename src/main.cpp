#include <QApplication>
#include <QFile>
#include <QDateTime>
#include <QTimer>
#include <juce_events/juce_events.h>
#include "app/FreeDawApplication.h"
#include "ui/MainWindow.h"
#include "ui/SplashScreen.h"
#include "utils/IconFont.h"

#ifndef FREEDAW_VERSION
#define FREEDAW_VERSION "dev"
#endif

static QFile* logFile = nullptr;

void logMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (!logFile) return;
    const char* prefix = "";
    switch (type) {
        case QtDebugMsg:    prefix = "DBG"; break;
        case QtWarningMsg:  prefix = "WRN"; break;
        case QtCriticalMsg: prefix = "CRT"; break;
        case QtFatalMsg:    prefix = "FTL"; break;
        default: break;
    }
    QTextStream out(logFile);
    out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
        << " [" << prefix << "] " << msg << "\n";
    out.flush();
}

int main(int argc, char* argv[])
{
    QApplication qtApp(argc, argv);
    qtApp.setApplicationName("FreeDaw");
    qtApp.setApplicationVersion(FREEDAW_VERSION);
    qtApp.setOrganizationName("FreeDaw");

    QFile lf(QApplication::applicationDirPath() + "/freedaw.log");
    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        logFile = &lf;
        qInstallMessageHandler(logMessageHandler);
    }

    freedaw::icons::registerFonts();

    auto* splash = new freedaw::SplashScreen();
    splash->show();
    qtApp.processEvents();

    juce::ScopedJuceInitialiser_GUI juceInit;

    freedaw::FreeDawApplication app;
    if (!app.initialize())
        return 1;

    app.showMainWindow();

    splash->finish();
    splash->update();

    QObject::connect(splash, &freedaw::SplashScreen::dismissed, splash, [&]() {
        app.mainWindow()->raise();
        app.mainWindow()->activateWindow();
    });

    return qtApp.exec();
}
