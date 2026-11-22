// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// One bucket: what it is called, what it holds, and where that sits against
// the band the model gives it.
Rectangle {
    id: root

    required property string title
    required property string horizon
    required property string amount
    required property string target
    required property string accentKey
    required property real bandStartRatio
    required property real bandEndRatio
    required property real balanceRatio
    required property real capRatio
    required property bool bandShown
    required property bool capShown

    readonly property color accent: Theme.bucketColour(root.accentKey)
    readonly property bool hasGauge: root.bandShown || root.capShown

    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    implicitHeight: content.implicitHeight + 2 * Theme.spaceMd

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: width / 2
                color: root.accent
            }

            Text {
                text: root.title
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            Text {
                text: root.horizon
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }

        Text {
            text: root.amount
            color: Theme.textPrimary
            font.pixelSize: Theme.fontMetric
            font.weight: Font.Light
        }

        Item {
            Layout.fillHeight: true
            implicitHeight: Theme.spaceXs
        }

        BucketGauge {
            Layout.fillWidth: true
            visible: root.hasGauge
            showBand: root.bandShown
            showCap: root.capShown
            bandStart: root.bandStartRatio
            bandEnd: root.bandEndRatio
            balanceMarker: root.balanceRatio
            capMarker: root.capRatio
            accent: root.accent
        }

        Text {
            text: root.target
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
}
