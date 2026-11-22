// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Hinkwise

// The window's fixed frame: where you are in the application, and the two
// figures that stay true on every page. Up and down move between the items;
// Tab walks through them and then on into the page.
Rectangle {
    id: root

    required property string phaseLabel
    required property string yearsOfFreedomLabel

    property int currentIndex: 0

    readonly property var titles: [
        qsTr("Dashboard"),
        qsTr("Buckets"),
        qsTr("Cashflow"),
        qsTr("Plan"),
        qsTr("Settings")
    ]

    implicitWidth: 220
    color: Theme.surface
    focus: true
    activeFocusOnTab: true

    function moveTo(index) {
        root.currentIndex = (index + root.titles.length) % root.titles.length;
    }

    Keys.onUpPressed: root.moveTo(root.currentIndex - 1)
    Keys.onDownPressed: root.moveTo(root.currentIndex + 1)
    // Tab walks the items and then leaves the rail, so the keyboard can reach
    // the controls inside the page. A handler consumes its key unless it says
    // otherwise, hence the explicit false at either end.
    Keys.onTabPressed: (event) => {
        if (root.currentIndex < root.titles.length - 1) {
            root.currentIndex += 1;
            event.accepted = true;
        } else {
            event.accepted = false;
        }
    }
    Keys.onBacktabPressed: (event) => {
        if (root.currentIndex > 0) {
            root.currentIndex -= 1;
            event.accepted = true;
        } else {
            event.accepted = false;
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        Text {
            text: qsTr("Hinkwise")
            color: Theme.textPrimary
            font.pixelSize: 18
            font.weight: Font.DemiBold
            font.letterSpacing: 0.2
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Repeater {
                model: root.titles

                delegate: Item {
                    id: navItem

                    required property int index
                    required property string modelData

                    readonly property bool current: navItem.index === root.currentIndex

                    Layout.fillWidth: true
                    implicitHeight: 34

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: navItem.current || pointer.containsMouse ? Theme.surfaceRaised
                                                                       : "transparent"
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: parent.height - 12
                        radius: 1
                        color: Theme.textPrimary
                        visible: navItem.current
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spaceMd
                        anchors.verticalCenter: parent.verticalCenter
                        text: navItem.modelData
                        color: navItem.current ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.weight: navItem.current ? Font.DemiBold : Font.Normal
                    }

                    MouseArea {
                        id: pointer

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.currentIndex = navItem.index;
                            root.forceActiveFocus();
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            Text {
                text: qsTr("Phase")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }

            Text {
                text: root.phaseLabel
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Item {
                implicitHeight: Theme.spaceSm
            }

            Text {
                text: qsTr("Years of freedom")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }

            Text {
                text: root.yearsOfFreedomLabel
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Light
            }
        }
    }
}
