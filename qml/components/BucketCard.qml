// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

Rectangle {
    id: root

    required property string title
    required property string horizon
    required property string amount
    required property string targetLabel
    required property real fillRatio
    required property color accent

    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    implicitHeight: 132

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceSm

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.title
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
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
            font.pixelSize: Theme.fontDisplay
            font.weight: Font.Light
        }

        Item { Layout.fillHeight: true }

        // Fill against the model's target band
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: Theme.surfaceRaised

            Rectangle {
                width: Math.max(0, Math.min(1, root.fillRatio)) * parent.width
                height: parent.height
                radius: parent.radius
                color: root.accent
            }
        }

        Text {
            text: root.targetLabel
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
        }
    }
}
