/// @file main.cpp
/// GUI entry point for Ceres.
///
/// Bootstraps the Qt Quick application by:
///   1. Setting the "Basic" style (no Material/Fusion) so we get a blank canvas
///      that our custom Theme.qml controls entirely.
///   2. Registering C++ model types as uncreatable QML types — they're owned by
///      JobController and handed to QML via Q_PROPERTY, but QML needs to know
///      their type to bind to roles and properties.
///   3. Creating the two context-property singletons (controller + completer)
///      and injecting them into the QML engine's root context.
///
/// The controller and completer live on the stack here (parent = nullptr) so
/// they outlive the QML engine — QML pointers to them remain valid during
/// teardown.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <QtQml>

#include "app/JobController.h"
#include "app/PathCompleter.h"
#include "core/ProfileStore.h"
#include "core/SecretStore.h"
#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"
#include "models/JobListModel.h"
#include "models/PeerModel.h"
#include "sched/Scheduler.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Ceres"));
    QGuiApplication::setOrganizationName(QStringLiteral("Ceres"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));  // custom high-contrast dark theme, no Material

    // ChangeListModel instances are owned by C++ (handed out via controller.changes),
    // so register it as uncreatable just to make the type known to QML.
    qmlRegisterUncreatableType<ChangeListModel>(
        "CeresUi", 1, 0, "ChangeListModel",
        QStringLiteral("ChangeListModel is provided by the controller"));
    qmlRegisterUncreatableType<JobListModel>(
        "CeresUi", 1, 0, "JobListModel",
        QStringLiteral("JobListModel is provided by the controller"));
    qmlRegisterUncreatableType<PeerModel>(
        "CeresUi", 1, 0, "PeerModel",
        QStringLiteral("PeerModel is provided by the controller"));

    const RsyncCapabilities caps = BinaryLocator::locateRsync();
    JobController controller(caps, nullptr, ProfileStore{}, SecretStore{}, Scheduler{}, true);
    PathCompleter completer(caps);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("completer"), &completer);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/CeresUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
