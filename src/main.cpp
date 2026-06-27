/// @file main.cpp
/// GUI entry point for Ceres.
///
/// Bootstraps the Qt Quick application by:
///   1. Setting the "Basic" style (no Material/Fusion) so we get a blank canvas
///      that our custom Theme.qml controls entirely.
///   2. Registering C++ model types as uncreatable QML types — they're owned by
///      JobController and handed to QML via Q_PROPERTY, but QML needs to know
///      their type to bind to roles and properties.
///   3. Creating the context-property objects (controller, completer, sessions,
///      transfers, notifier) and injecting them into the QML engine's root context.
///
/// Those objects live on the stack in main() (parent = nullptr) so they outlive the
/// QML engine — QML pointers to them remain valid during teardown.

#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <QtQml>

#include "cli/AdHocTransfer.h"
#include "app/BrowseController.h"
#include "app/JobController.h"
#include "app/PathCompleter.h"
#include "app/BrowseSessions.h"
#include "app/Notifier.h"
#include "app/TransferManager.h"
#include "core/SecretStore.h"
#include "core/SshHostStore.h"
#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"
#include "models/FileListModel.h"
#include "models/PeerModel.h"
#include "models/SshHostListModel.h"
#include "models/TransfersModel.h"

#ifndef CERES_VERSION
#define CERES_VERSION "0.0.0"
#endif

namespace {

// Shared app identity, set on whichever QCoreApplication subclass we end up using.
void setAppIdentity()
{
    QCoreApplication::setApplicationName(QStringLiteral("Ceres"));
    QCoreApplication::setOrganizationName(QStringLiteral("Ceres"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CERES_VERSION));
}

bool isHelpArg(const char *arg)
{
    const QByteArray a(arg);
    return a == QByteArrayLiteral("-h") || a == QByteArrayLiteral("--help");
}

bool isMacProcessSerialArg(const char *arg)
{
    return QByteArray(arg).startsWith("-psn_");
}

void printUsage(FILE *stream)
{
    fputs("usage:\n", stream);
    fputs("  ceres                         launch the GUI\n", stream);
    fputs("  ceres <source> <destination>  run rsync -axh with a terminal progress bar\n", stream);
}

} // namespace

int main(int argc, char *argv[])
{
    // SSH_ASKPASS hook: when the rsync child's ssh execs us to obtain a password, we
    // run with CERES_ASKPASS set — just print the password and exit before booting the
    // GUI. The password is passed in-env (never on argv) by RsyncProcessEngine.
    if (qEnvironmentVariableIsSet("CERES_ASKPASS")) {
        fputs(qgetenv("CERES_SSH_PASSWORD").constData(), stdout);
        fputc('\n', stdout);
        return 0;
    }

    if (argc == 2 && isHelpArg(argv[1])) {
        printUsage(stdout);
        return 0;
    }

    if (argc == 3) {
        QCoreApplication app(argc, argv);
        setAppIdentity();
        return AdHocTransfer::run(QString::fromLocal8Bit(argv[1]),
                                  QString::fromLocal8Bit(argv[2]));
    }

    if (argc > 1 && !(argc == 2 && isMacProcessSerialArg(argv[1]))) {
        printUsage(stderr);
        return 64;
    }

    QGuiApplication app(argc, argv);
    setAppIdentity();
    // Associates the window with its .desktop entry on Linux (Wayland app id / taskbar icon).
    QGuiApplication::setDesktopFileName(QStringLiteral("ceres"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/ceres-512.png")));

    QQuickStyle::setStyle(QStringLiteral("Basic"));  // custom high-contrast dark theme, no Material

    // ChangeListModel instances are owned by C++ (handed out via controller.changes),
    // so register it as uncreatable just to make the type known to QML.
    qmlRegisterUncreatableType<ChangeListModel>(
        "CeresUi", 1, 0, "ChangeListModel",
        QStringLiteral("ChangeListModel is provided by the controller"));
    qmlRegisterUncreatableType<SshHostListModel>(
        "CeresUi", 1, 0, "SshHostListModel",
        QStringLiteral("SshHostListModel is provided by the controller"));
    qmlRegisterUncreatableType<PeerModel>(
        "CeresUi", 1, 0, "PeerModel",
        QStringLiteral("PeerModel is provided by the controller"));
    qmlRegisterUncreatableType<FileListModel>(
        "CeresUi", 1, 0, "FileListModel",
        QStringLiteral("FileListModel is provided by the browse controller"));
    qmlRegisterUncreatableType<TransfersModel>(
        "CeresUi", 1, 0, "TransfersModel",
        QStringLiteral("TransfersModel is provided by the transfer manager"));

    const RsyncCapabilities caps = BinaryLocator::locateRsync();
    JobController controller(caps, nullptr, SecretStore{},
                             SshHostStore{}, true);
    PathCompleter completer(caps);
    TransferManager transfers(caps);
    Notifier notifier;
    BrowseSessions sessions(caps, SshHostStore{}, SecretStore{}, &transfers);
    // A host saved from any browse session should appear in the sidebar immediately.
    QObject::connect(&sessions, &BrowseSessions::hostsChanged,
                     &controller, &JobController::reloadSshHosts);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("completer"), &completer);
    engine.rootContext()->setContextProperty(QStringLiteral("sessions"), &sessions);
    engine.rootContext()->setContextProperty(QStringLiteral("transfers"), &transfers);
    engine.rootContext()->setContextProperty(QStringLiteral("notifier"), &notifier);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/CeresUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
