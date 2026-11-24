// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The window's fixed frame: where you are in the application, the household the
// pages are about, and the two figures that stay true on every page. Up and down
// move between the items; Tab walks through them and then on into the page.
Rectangle {
    id: root

    required property string phaseLabel
    required property string yearsOfFreedomLabel
    /// The households in this database. The rail is where a second one is
    /// switched to, and where an example says so on every page.
    required property ProfilesViewModel profiles

    property int currentIndex: 0

    // The order is the order of the work: what the household has, what it does,
    // what that adds up to. "Accounts & buckets" names the page by both halves
    // of it, because both are the household's own entries.
    readonly property var titles: [
        qsTr("Overview"),
        qsTr("Accounts & buckets"),
        qsTr("Cashflow"),
        qsTr("Plan"),
        qsTr("Guide"),
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

        // The one thing that must never be missed: these figures are not the
        // household's own. It sits in the rail because the rail is on every
        // page, and it is shown however few profiles there are - an example
        // that is the only profile is still an example.
        Rectangle {
            Layout.fillWidth: true
            visible: root.profiles.activeIsExample
            implicitHeight: exampleLabel.implicitHeight + 2 * Theme.spaceXs
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.warning

            Text {
                id: exampleLabel

                anchors.centerIn: parent
                text: qsTr("Example data")
                color: Theme.warning
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.letterSpacing: 0.4
            }
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

        // Who the pages are about. With one profile there is nothing to choose
        // and nothing is shown: a household that never made a second one should
        // never have to learn what a profile is.
        Item {
            Layout.fillWidth: true
            visible: root.profiles.count > 1
            implicitHeight: switcher.implicitHeight

            Rectangle {
                id: switcher

                anchors.left: parent.left
                anchors.right: parent.right
                implicitHeight: switcherLines.implicitHeight + 2 * Theme.spaceSm
                radius: Theme.radiusSmall
                color: switcherPointer.containsMouse ? Theme.surfaceRaised : Theme.background
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: switcherLines

                    anchors.fill: parent
                    anchors.margins: Theme.spaceSm
                    spacing: 2

                    Text {
                        text: root.profiles.activeName
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: root.profiles.activeTrackingModeLabel
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: switcherPointer

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: switcherMenu.opened ? switcherMenu.close() : switcherMenu.open()
                }
            }

            // The others, by name and mode. The active profile is not in the
            // list because choosing it again would do nothing.
            Popup {
                id: switcherMenu

                parent: switcher
                width: switcher.width
                x: 0
                y: -height - Theme.spaceXs
                padding: 1
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                }

                contentItem: Column {
                    spacing: 1

                    Repeater {
                        model: root.profiles.profiles

                        delegate: Item {
                            id: otherRow

                            required property var modelData

                            width: switcherMenu.width - 2
                            implicitHeight: otherLines.implicitHeight + 2 * Theme.spaceSm
                            visible: !otherRow.modelData.isActive

                            Rectangle {
                                anchors.fill: parent
                                color: otherPointer.containsMouse ? Theme.surfaceRaised
                                                                    : "transparent"
                            }

                            ColumnLayout {
                                id: otherLines

                                anchors.fill: parent
                                anchors.margins: Theme.spaceSm
                                spacing: 2

                                Text {
                                    text: otherRow.modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: otherRow.modelData.trackingModeLabel + " · "
                                          + otherRow.modelData.phaseLabel
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            MouseArea {
                                id: otherPointer

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.profiles.switchTo(otherRow.modelData.id).length === 0)
                                        switcherMenu.close();
                                }
                            }
                        }
                    }
                }
            }
        }

        // Why the switch that was just asked for did not happen. It names the
        // screens that hold unsaved work, so nothing is dropped without the
        // household knowing what to finish first.
        Text {
            Layout.fillWidth: true
            visible: root.profiles.switchError.length > 0
            text: root.profiles.switchError
            textFormat: Text.PlainText
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
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
