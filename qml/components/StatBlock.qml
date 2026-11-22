// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// Label, figure, and one line saying what the figure measures. The note is
// where the model gets to be precise without turning the label into a
// sentence.
ColumnLayout {
    id: root

    property string label: ""
    property string value: ""
    property string note: ""
    property color valueColour: Theme.textPrimary

    spacing: Theme.spaceXs

    Text {
        text: root.label
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    Text {
        text: root.value
        color: root.valueColour
        font.pixelSize: Theme.fontTitle
        font.weight: Font.Light
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    Text {
        visible: text.length > 0
        text: root.note
        color: Theme.textSecondary
        font.pixelSize: Theme.fontCaption
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
}
