// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

ApplicationWindow {
    id: window

    width: 1120
    height: 720
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Hinkwise")
    color: Theme.background

    DashboardViewModel {
        id: dashboardModel

        Component.onCompleted: dashboardModel.reload()
    }

    // Before there is a household there is nothing to show in the rail, so the
    // first run takes the whole window. Once the wizard has written the
    // profile, dashboardModel.hasProfile flips and the shell appears.
    OnboardingPage {
        anchors.fill: parent
        visible: !dashboardModel.hasProfile
    }

    RowLayout {
        anchors.fill: parent
        visible: dashboardModel.hasProfile
        spacing: 0

        NavRail {
            id: rail

            Layout.fillHeight: true
            phaseLabel: dashboardModel.phaseLabel
            yearsOfFreedomLabel: dashboardModel.yearsOfFreedomLabel
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: rail.currentIndex

            DashboardPage {
                dashboard: dashboardModel
            }

            BucketsPage {
            }

            CashflowPage {
            }

            PlanPage {
                onNavigateRequested: (index) => rail.currentIndex = index
            }

            SettingsPage {
            }
        }
    }
}
