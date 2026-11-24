// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presentation/app_controller.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>
#include <cstdlib>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    // organisationName and applicationName must differ, otherwise Qt nests the
    // data directory as hinkwise/hinkwise. Data lives in
    // ~/.local/share/elisohman/hinkwise/.
    QCoreApplication::setOrganizationName(QStringLiteral("elisohman"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Must match the installed .desktop file basename so that Wayland
    // compositors (niri) associate the window with the right app id.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.elisohman.hinkwise"));

    // Qt's Wayland text-input-v3 client logs a warning every time focus leaves a
    // text field while the compositor reports no focused surface. It fires on
    // every dialog dismissal, carries no action for us, and is a Qt-side
    // diagnostic rather than anything hinkwise does wrong - so it is filtered.
    // Deliberately narrow: qt.qpa.services stays visible, because a failure to
    // reach the desktop portal there can mean file dialogs are degraded. An
    // explicit QT_LOGGING_RULES always wins, so the noise can be brought back
    // for diagnosis.
    if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES"))
        QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.wayland.textinput.warning=false"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The controller exists before the engine does, so the database is open and
    // the QML singleton points at a live connection from the first screen on.
    hinkwise::presentation::AppController controller{nullptr};
    if (!controller.initialise())
        qWarning("hinkwise: %s", qPrintable(controller.lastError()));

    QQmlApplicationEngine engine;
    engine.loadFromModule("Hinkwise", "Main");
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    return app.exec();
}
