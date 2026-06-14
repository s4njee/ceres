#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <QtQml>

#include "app/JobController.h"
#include "models/ChangeListModel.h"

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

    JobController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/CeresUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
