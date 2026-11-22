// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import Hinkwise

// One bucket on one track: the band the model sizes, and where the household
// actually sits. The dot is the balance, the tinted stretch is the band, and
// the thin line is a cap where the model has one instead of a band.
Item {
    id: root

    property real bandStart: 0
    property real bandEnd: 0
    property real balanceMarker: 0
    property real capMarker: 0
    property bool showBand: false
    property bool showCap: false
    property color accent: Theme.textMuted

    implicitHeight: 14

    Rectangle {
        id: track

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 8
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
    }

    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        visible: root.showBand
        x: root.bandStart * track.width
        width: Math.max(0, (root.bandEnd - root.bandStart) * track.width)
        height: track.height
        radius: Theme.radiusSmall
        color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.32)
    }

    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        visible: root.showCap
        x: Math.min(Math.max(root.capMarker * track.width - width / 2, 0), track.width - width)
        width: 2
        height: track.height + 4
        radius: 1
        color: Theme.textSecondary
    }

    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        x: Math.min(Math.max(root.balanceMarker * track.width - width / 2, 0), track.width - width)
        width: 10
        height: 10
        radius: width / 2
        color: root.accent
        border.color: Theme.surface
        border.width: 2
    }
}
