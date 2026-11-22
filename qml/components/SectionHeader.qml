// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// A quiet rule with a label: enough structure to separate sections, not enough
// to compete with the numbers underneath.
RowLayout {
    id: root

    property string title: ""
    property string note: ""

    spacing: Theme.spaceMd

    Text {
        text: root.title
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.border
    }

    Text {
        visible: text.length > 0
        text: root.note
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
    }
}
