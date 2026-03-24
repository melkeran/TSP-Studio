/**
 * @file main.cpp
 * @brief TSP Studio Entry Point
 * @author Mohamed Elkeran
 * 
 * DISCLAIMER:
 * This software is provided "AS IS", without warranty of any kind. 
 * Use or modification of this code is allowed without responsibility 
 * of the author for any damages or direct results of the application execution.
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "ui/GuiController.h"
#include "ui/TSPStudioMapView.h"
#include "core/Tracer.h"

int main(int argc, char *argv[])
{
    Tracer::init(); // Initialize and clear log file
    TRACE_SCOPE;
    TRACE_MSG("Initializing QGuiApplication");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    TRACE_MSG("Setting application metadata");
    app.setOrganizationName("AI_Vision_Solutions");
    app.setOrganizationDomain("ai-vision.com");
    app.setApplicationName("TSP Studio");

    TRACE_MSG("Registering types");
    qmlRegisterType<TSPStudioMapView>("TSPStudio", 1, 0, "TSPStudioMapView");
    qRegisterMetaType<std::vector<int>>("std::vector<int>");

    TRACE_MSG("Setting up QQmlEngine");
    GuiController guiController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("guiController", &guiController);

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            TRACE_MSG("Failed to load QML");
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);
    
    TRACE_MSG("Loading QML");
    engine.load(url);

    TRACE_MSG("Starting app event loop");
    return app.exec();
}

