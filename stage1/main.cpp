#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include "HardwareController.h"

int main(int argc, char *argv[]) {
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("DISPLAY", ":0");
    }

    // CRITICAL FIX: Qt Charts requires QApplication, NOT QGuiApplication
    QApplication app(argc, argv);

    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#00E5FF");

    HardwareController hwController;
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("Hardware", &hwController);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() {
            qDebug() << "FATAL ERROR: QML file failed to load.";
            QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/PureGene/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        qDebug() << "ERROR: QML file is empty or path is incorrect. Exiting.";
        return -1;
    }

    qDebug() << "GUI successfully loaded! Starting application...";
    return app.exec();
}
