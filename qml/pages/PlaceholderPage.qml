// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import Hinkwise

// A page that exists in the navigation but not yet in the product. It says
// what will arrive instead of pretending to be finished.
Item {
    id: root

    required property string title
    required property string note

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.spaceXl, 420)
        spacing: Theme.spaceSm

        Text {
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: root.note
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
