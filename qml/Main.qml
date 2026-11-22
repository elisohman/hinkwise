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

    RowLayout {
        anchors.fill: parent
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

            PlaceholderPage {
                title: qsTr("Buckets")
                note: qsTr("Accounts and valuations arrive next.")
            }

            PlaceholderPage {
                title: qsTr("Cashflow")
                note: qsTr("Monthly income and spending arrive next.")
            }

            PlaceholderPage {
                title: qsTr("Plan")
                note: qsTr("Targets, projections and rebalancing arrive next.")
            }

            PlaceholderPage {
                title: qsTr("Settings")
                note: qsTr("Profile, currency and jurisdiction arrive next.")
            }
        }
    }
}
