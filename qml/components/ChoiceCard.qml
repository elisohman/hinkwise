// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// One option of a small, consequential choice: the name of it, and the
// sentences that explain what picking it means.
//
// Settings and the setup wizard both offer how a household keeps its record,
// and both have to describe the two modes the same way - the mode decides how
// every other page reads an old month, so two descriptions would be two
// products.
RadioButton {
    id: root

    required property string label
    property string description: ""

    Layout.fillWidth: true
    padding: Theme.spaceMd
    indicator: Item {}

    background: Rectangle {
        radius: Theme.radius
        color: root.checked ? Theme.surfaceRaised : Theme.surface
        border.width: 1
        border.color: root.checked || root.visualFocus ? Theme.textSecondary : Theme.border
    }

    contentItem: RowLayout {
        spacing: Theme.spaceMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: root.label
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: root.description
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            radius: 8
            color: "transparent"
            border.width: 1
            border.color: root.checked ? Theme.textPrimary : Theme.border

            Rectangle {
                anchors.centerIn: parent
                visible: root.checked
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: Theme.textPrimary
            }
        }
    }
}
