// Copyright (c) fmaerten@gmail.com
// License: MIT

// Entry point for the Qt viewer.
//
// Same observation as demo.cpp, and it is the whole point: the only line in
// this entire application that mentions the bound library is
// `scene::register_all()`. After that, the 3D view, the property editor, the
// object list, the Add menu and the console all work from metadata.
//
// Stock C++20 + Qt 6 (Widgets, OpenGL, OpenGLWidgets). No reflection, no C++26
// toolchain — the metadata was emitted as data on the generation host.

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>

#include "auto_dynamic.h"
#include "mainwindow.h"

int main(int argc, char **argv) {
    // Headless-ish driving, so the example is smoke-testable with nobody at the
    // keyboard (same spirit as ROSETTA_IMGUI_FRAMES in examples/imgui):
    //
    //   viewer --run "set cube shading Wireframe" --run "call cube subdivide" \
    //          --shot out.png
    //
    // Each --run goes through the very same console the user types into.
    QString     shot;
    QStringList script;
    for (int i = 1; i + 1 < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--shot") {
            shot = QString::fromLocal8Bit(argv[i + 1]);
        } else if (a == "--run") {
            script << QString::fromLocal8Bit(argv[i + 1]);
        }
    }

    // A core-profile 3.3 context, which is what SceneView's shaders target and
    // the only GL profile macOS offers above 2.1.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    scene::register_all(); // <- the one line that knows the library exists

    MainWindow w;
    w.show();

    for (const QString &cmd : script) {
        w.command(cmd);
    }

    if (!shot.isEmpty()) {
        QTimer::singleShot(1200, &w, [&w, shot] {
            const bool ok = w.grab().save(shot);
            qInfo("%s %s", ok ? "wrote" : "FAILED to write", qPrintable(shot));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
