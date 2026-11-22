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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXl
        spacing: Theme.spaceLg

        ColumnLayout {
            spacing: Theme.spaceXs

            Text {
                text: qsTr("Hinkwise")
                color: Theme.textPrimary
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("Your personal economy, seen through the four-bucket model.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spaceMd
            rowSpacing: Theme.spaceMd

            BucketCard {
                Layout.fillWidth: true
                title: qsTr("Buffer")
                horizon: qsTr("under 3 years")
                amount: "—"
                targetLabel: qsTr("Target: 0.5–6 months of expenses")
                fillRatio: 0.0
                accent: Theme.buffer
            }

            BucketCard {
                Layout.fillWidth: true
                title: qsTr("Preserve value")
                horizon: qsTr("3–9 years")
                amount: "—"
                targetLabel: qsTr("Housing and pension live here")
                fillRatio: 0.0
                accent: Theme.preserve
            }

            BucketCard {
                Layout.fillWidth: true
                title: qsTr("Growth")
                horizon: qsTr("10+ years")
                amount: "—"
                targetLabel: qsTr("Target: as large as possible")
                fillRatio: 0.0
                accent: Theme.growth
            }

            BucketCard {
                Layout.fillWidth: true
                title: qsTr("Play")
                horizon: qsTr("any horizon")
                amount: "—"
                targetLabel: qsTr("Cap: 10% of Growth")
                fillRatio: 0.0
                accent: Theme.play
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            text: qsTr("Skeleton build — no data yet.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }
    }
}
