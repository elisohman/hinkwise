// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// A labelled slot: the caption above, the control the page declares below it.
// Children added to a FormRow are laid out under the caption in the order they
// are declared, so a page reads as the form it makes.
ColumnLayout {
    id: root

    property string label: ""
    /// What the field accepts or what it means, on the same line as the label.
    property string hint: ""

    Layout.fillWidth: true
    spacing: Theme.spaceXs

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Text {
            text: root.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Text {
            visible: text.length > 0
            text: root.hint
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }
    }
}
