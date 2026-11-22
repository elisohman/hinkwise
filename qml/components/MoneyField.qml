// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// A field for an amount or a rate.
//
// It filters keystrokes and nothing else: digits, spaces, commas and the
// separator that writes a decimal. The text goes to a view model, which reads
// it in C++ and hands back the sentence under the field when it cannot. The
// field never converts or compares an amount itself.
ColumnLayout {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property string error: ""
    /// The unit the value is in, shown inside the field: "SEK", "%".
    property string unit: ""

    signal edited(string text)

    Layout.fillWidth: true
    spacing: Theme.spaceXs

    TextField {
        id: input

        Layout.fillWidth: true
        placeholderText: root.placeholder
        placeholderTextColor: Theme.textMuted
        color: Theme.textPrimary
        selectionColor: Theme.buffer
        selectedTextColor: Theme.background
        font.pixelSize: Theme.fontBody
        rightPadding: unitLabel.implicitWidth + Theme.spaceMd
        validator: RegularExpressionValidator {
            regularExpression: /^[0-9\s.,]*$/
        }
        onTextEdited: root.edited(input.text)

        background: Rectangle {
            implicitHeight: 34
            radius: Theme.radiusSmall
            color: Theme.background
            border.width: 1
            border.color: input.activeFocus ? Theme.textSecondary
                          : root.error.length > 0 ? Theme.danger
                          : Theme.border
        }

        Text {
            id: unitLabel

            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceSm
            anchors.verticalCenter: parent.verticalCenter
            visible: root.unit.length > 0
            text: root.unit
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }
    }

    Text {
        visible: text.length > 0
        text: root.error
        color: Theme.danger
        font.pixelSize: Theme.fontCaption
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
}
