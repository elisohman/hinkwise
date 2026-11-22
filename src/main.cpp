// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("hinkwise"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Must match the installed .desktop file basename so that Wayland
    // compositors (niri) associate the window with the right app id.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.elisohman.hinkwise"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("Hinkwise", "Main");
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    return app.exec();
}
