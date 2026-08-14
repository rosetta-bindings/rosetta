// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Auto-QML demo: a generic QtQuick inspector window driven by one
// rosetta::walk over Person. Field rows and method rows in the generic
// Inspector.qml (shipped from rosetta/visitors/qml/) are rendered from
// the metadata the visitor pushes into a single rosetta::ReflectedObject.
//
// Build flags: -freflection -freflection-latest -fannotation-attributes
// See CMakeLists.txt in this folder.

#include "../Algo.h"
#include <rosetta/visitors/qml.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    Algo                   algo;
    rosetta::ReflectedObject reflected;
    reflected.setTypeName(QStringLiteral("Algo"));
    rosetta::bind_qml<Algo>(&reflected, algo);
    reflected.emitReady();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("inspector"), &reflected);
    engine.loadFromModule("ReflectedQml", "Inspector");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    return app.exec();
}
