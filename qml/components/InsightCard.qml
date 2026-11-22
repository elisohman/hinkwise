// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// One check from the model: how urgent it is, what it saw, why it matters,
// and what the model suggests. The text is the rule's own, unwrapped and
// unedited.
ColumnLayout {
    id: root

    property string severity: ""
    property string severityKey: "info"
    property string title: ""
    property string why: ""
    property string suggestion: ""

    readonly property color accent: Theme.severityColour(root.severityKey)

    spacing: Theme.spaceSm

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 6
            implicitHeight: 6
            radius: width / 2
            color: root.accent
        }

        Text {
            text: root.severity
            color: root.accent
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }
    }

    Text {
        text: root.title
        color: Theme.textPrimary
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        Layout.fillWidth: true
        Layout.maximumWidth: 720
        wrapMode: Text.WordWrap
    }

    Text {
        text: root.why
        color: Theme.textSecondary
        font.pixelSize: Theme.fontCaption
        Layout.fillWidth: true
        Layout.maximumWidth: 720
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spaceXs
        spacing: Theme.spaceSm

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 2
            implicitHeight: suggestionText.implicitHeight
            radius: 1
            color: root.accent
            opacity: 0.7
        }

        Text {
            id: suggestionText
            text: root.suggestion
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            Layout.maximumWidth: 710
            wrapMode: Text.WordWrap
        }
    }
}
