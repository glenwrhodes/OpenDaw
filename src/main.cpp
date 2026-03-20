#include <QApplication>
#include <QFile>
#include <QDateTime>
#include <QTimer>
#include <juce_events/juce_events.h>
#include "app/FreeDawApplication.h"
#include "ui/MainWindow.h"
#include "ui/SplashScreen.h"
#include "utils/IconFont.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

#ifdef _WIN32
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep)
{
    const char* desc = "Unknown";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:    desc = "ACCESS_VIOLATION"; break;
        case EXCEPTION_STACK_OVERFLOW:      desc = "STACK_OVERFLOW"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:  desc = "INT_DIVIDE_BY_ZERO"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: desc = "ILLEGAL_INSTRUCTION"; break;
        case EXCEPTION_IN_PAGE_ERROR:       desc = "IN_PAGE_ERROR"; break;
    }

    if (logFile) {
        QTextStream out(logFile);
        out << "\n========== CRASH ==========\n";
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
            << " EXCEPTION: " << desc
            << " (0x" << Qt::hex << ep->ExceptionRecord->ExceptionCode << ")\n";
        out << "Address: 0x" << Qt::hex
            << reinterpret_cast<quintptr>(ep->ExceptionRecord->ExceptionAddress) << "\n";
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && ep->ExceptionRecord->NumberParameters >= 2) {
            const char* op = ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ";
            out << "Faulting " << op << " at address: 0x" << Qt::hex
                << ep->ExceptionRecord->ExceptionInformation[1] << "\n";
        }
#ifdef _M_X64
        auto* ctx = ep->ContextRecord;
        out << "RIP=0x" << Qt::hex << ctx->Rip
            << " RSP=0x" << ctx->Rsp
            << " RBP=0x" << ctx->Rbp << "\n";
#endif
        out << "========== END CRASH ==========\n";
        out.flush();
        logFile->flush();
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char* argv[])
{
    QApplication qtApp(argc, argv);
    qtApp.setApplicationName("FreeDaw");
    qtApp.setApplicationVersion(FREEDAW_VERSION);
    qtApp.setOrganizationName("FreeDaw");

    qtApp.setStyleSheet(
        "QToolTip { background: #3a3a3a; color: #dcdcdc; border: 1px solid #666; "
        "padding: 3px; font-size: 11px; }");

    QFile lf(QApplication::applicationDirPath() + "/freedaw.log");
    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        logFile = &lf;
        qInstallMessageHandler(logMessageHandler);
    }

#ifdef _WIN32
    SetUnhandledExceptionFilter(crashHandler);
#endif

    freedaw::icons::registerFonts();

    auto* splash = new freedaw::SplashScreen();
    splash->show();
    qtApp.processEvents();

    juce::ScopedJuceInitialiser_GUI juceInit;

    freedaw::FreeDawApplication app;
    if (!app.initialize())
        return 1;

    app.checkRecovery(splash);

    app.showMainWindow();

    splash->finish();
    splash->update();

    QObject::connect(splash, &freedaw::SplashScreen::dismissed, splash, [&]() {
        app.mainWindow()->raise();
        app.mainWindow()->activateWindow();
    });

    return qtApp.exec();
}
