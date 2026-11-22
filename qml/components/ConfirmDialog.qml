// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// Says what is about to happen and what it costs before it happens. The
// confirming button is the destructive one; the cancel button takes focus, so
// a stray Enter never deletes anything.
Popup {
    id: root

    required property string title
    required property string message
    property string confirmLabel: qsTr("Delete")

    signal confirmed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 2 * Theme.spaceLg : 440)
    modal: true
    focus: true
    padding: Theme.spaceLg
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle {
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.72)
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceMd

        Text {
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Text {
            text: root.message
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceSm
            spacing: Theme.spaceSm

            Item {
                Layout.fillWidth: true
            }

            PrimaryButton {
                text: qsTr("Cancel")
                filled: false
                focus: true
                onClicked: root.close()
            }

            PrimaryButton {
                text: root.confirmLabel
                destructive: true
                onClicked: {
                    root.confirmed();
                    root.close();
                }
            }
        }
    }
}
