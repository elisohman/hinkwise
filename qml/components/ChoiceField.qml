// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Hinkwise

// A picker with one answer, themed like the rest of the application. The pages
// pass a list of labels and read the index back, so the index is always a
// position in a list built from the domain's own enumeration.
ComboBox {
    id: root

    implicitHeight: 34
    font.pixelSize: Theme.fontBody
    leftPadding: Theme.spaceSm
    rightPadding: Theme.spaceLg

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.background
        border.width: 1
        border.color: root.activeFocus ? Theme.textSecondary : Theme.border
    }

    contentItem: Text {
        text: root.displayText
        color: root.enabled ? Theme.textPrimary : Theme.textMuted
        font.pixelSize: Theme.fontBody
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: root.width - width - Theme.spaceSm
        y: root.topPadding + (root.availableHeight - height) / 2
        text: "\u25BE"
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
    }

    delegate: ItemDelegate {
        id: entry

        required property int index
        required property string modelData

        width: root.width
        implicitHeight: 30
        padding: Theme.spaceSm

        background: Rectangle {
            color: entry.highlighted ? Theme.surfaceRaised : Theme.surface
        }

        contentItem: Text {
            text: entry.modelData
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: root.height
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
