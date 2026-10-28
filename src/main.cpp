#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QDateTime>
#include <QTimer>
#include <QIcon>
#include <juce_events/juce_events.h>
#include "app/OpenDawApplication.h"
#include "ui/MainWindow.h"
#include "ui/SplashScreen.h"
#include "utils/IconFont.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#ifndef OpenDaw_VERSION
#define OpenDaw_VERSION "dev"
#endif

static QFile* logFile = nullptr;
static QMutex logMutex;

void logMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    // Export and decode workers log too; QFile is not thread-safe.
    QMutexLocker lock(&logMutex);
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
static wchar_t crashDumpPath[MAX_PATH] = {};

static void writeCrashDump(EXCEPTION_POINTERS* ep)
{
    if (!crashDumpPath[0]) return;
    HANDLE file = CreateFileW(crashDumpPath, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory
                                                 | MiniDumpWithThreadInfo),
                      &info, nullptr, nullptr);
    CloseHandle(file);
}

static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep)
{
    writeCrashDump(ep);

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
    qtApp.setApplicationName("OpenDaw");
    qtApp.setApplicationVersion(OpenDaw_VERSION);
    qtApp.setOrganizationName("OpenDaw");

    qtApp.setWindowIcon(QIcon(":/icon.png"));


    // Keep the previous run's log so a crash report survives the next launch.
    const QString logPath = QApplication::applicationDirPath() + "/OpenDaw.log";
    const QString prevLogPath = QApplication::applicationDirPath() + "/OpenDaw.prev.log";
    if (QFile::exists(logPath)) {
        QFile::remove(prevLogPath);
        QFile::rename(logPath, prevLogPath);
    }

    QFile lf(logPath);
    // Destroyed just before `lf` (after the app objects, so their teardown is
    // still logged): stop the handler writing into a dead QFile.
    struct LogFileGuard {
        ~LogFileGuard()
        {
            QMutexLocker lock(&logMutex);
            qInstallMessageHandler(nullptr);
            logFile = nullptr;
        }
    } logFileGuard;
    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        logFile = &lf;
        qInstallMessageHandler(logMessageHandler);
    }

#ifdef _WIN32
    const QString dumpPath = QApplication::applicationDirPath() + "/OpenDaw-crash.dmp";
    if (dumpPath.size() < MAX_PATH)
        dumpPath.toWCharArray(crashDumpPath);
    SetUnhandledExceptionFilter(crashHandler);
#endif

    OpenDaw::icons::registerFonts();

    auto* splash = new OpenDaw::SplashScreen();
    splash->show();
    qtApp.processEvents();

    juce::ScopedJuceInitialiser_GUI juceInit;

    qDebug() << "[main] creating app";
    OpenDaw::OpenDawApplication app;
    qDebug() << "[main] calling initialize";
    if (!app.initialize())
        return 1;
    qDebug() << "[main] initialize done, checking recovery";

    app.checkRecovery(splash);
    qDebug() << "[main] recovery check done, showing main window";

    app.showMainWindow();
    qDebug() << "[main] main window shown";

    // Load a .tracktionedit file passed as a command-line argument (drag-onto-exe)
    QString fileToLoad;
    const QStringList args = qtApp.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].endsWith(".tracktionedit", Qt::CaseInsensitive)
            && QFileInfo::exists(args[i])) {
            fileToLoad = args[i];
            break;
        }
    }

    if (!fileToLoad.isEmpty()) {
        app.mainWindow()->loadFile(fileToLoad);
    }

    splash->finish();
    splash->update();

    QObject::connect(splash, &OpenDaw::SplashScreen::dismissed, splash, [&]() {
        app.mainWindow()->raise();
        app.mainWindow()->activateWindow();
    });

    return qtApp.exec();
}
