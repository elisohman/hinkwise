// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import Hinkwise

// The one button the pages use: a filled action for the thing to do next, and
// a quiet outline for the thing that only changes the screen. Neither is ever
// the browser default, so every action reads the same on every page.
Button {
    id: root

    /// Filled and prominent, or outlined and secondary.
    property bool filled: true
    /// Filled in the danger colour, for the action that removes something.
    property bool destructive: false

    readonly property color accent: root.destructive ? Theme.danger : Theme.textPrimary

    implicitWidth: Math.max(label.implicitWidth + 2 * Theme.spaceLg, 72)
    implicitHeight: Math.max(label.implicitHeight + Theme.spaceMd, 34)
    padding: 0

    background: Rectangle {
        radius: Theme.radiusSmall
        color: !root.enabled ? Theme.surfaceRaised
             : root.filled ? root.accent
             : root.hovered ? Theme.surfaceRaised
             : "transparent"
        border.color: root.visualFocus ? Theme.textSecondary
                      : root.filled ? "transparent"
                      : Theme.border
        border.width: 1
    }

    contentItem: Text {
        id: label

        text: root.text
        color: !root.enabled ? Theme.textMuted
             : root.filled ? Theme.background
             : Theme.textPrimary
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
