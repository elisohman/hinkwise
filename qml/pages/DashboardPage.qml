// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The one page that is finished: the household's position as the model sees
// it, from the single headline figure down to the checks the rules returned.
Item {
    id: page

    required property DashboardViewModel dashboard

    readonly property int insightLimit: 5

    // Where the x21 / x25 / x33 band sits on the FI track, and what to call
    // each mark. The positions are ratios the view model has already worked
    // out from the jurisdiction's multipliers.
    readonly property var fiTicks: [
        {
            position: page.dashboard.fiTargetLowRatio,
            multiplier: page.dashboard.fiMultiplierLow
        },
        {
            position: page.dashboard.fiTargetDefaultRatio,
            multiplier: page.dashboard.fiMultiplierDefault
        },
        {
            position: page.dashboard.fiTargetHighRatio,
            multiplier: page.dashboard.fiMultiplierHigh
        }
    ]

    property bool netWorthIncludesPension: true

    component Segment: Button {
        id: segment

        property bool selected: false

        implicitWidth: segmentLabel.implicitWidth + 2 * Theme.spaceMd
        implicitHeight: 26
        padding: 0

        background: Rectangle {
            radius: Theme.radiusSmall
            color: segment.selected ? Theme.surfaceRaised
                 : segment.hovered ? Theme.surface : "transparent"
            border.color: segment.visualFocus ? Theme.textSecondary
                          : segment.selected ? Theme.border : "transparent"
            border.width: 1
        }

        contentItem: Text {
            id: segmentLabel

            text: segment.text
            color: segment.selected ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontCaption
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        // Header: the page, the month the figures describe, and the phase the
        // model is sizing for.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: qsTr("Overview")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                text: page.dashboard.monthLabel
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
                Layout.alignment: Qt.AlignVCenter
            }

            Rectangle {
                visible: page.dashboard.hasProfile
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: phaseLabel.implicitWidth + 2 * Theme.spaceSm
                implicitHeight: phaseLabel.implicitHeight + Theme.spaceSm
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: Theme.border
                border.width: 1

                Text {
                    id: phaseLabel

                    anchors.centerIn: parent
                    text: page.dashboard.phaseLabel
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.dashboard.hasProfile
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.spaceLg

                // The headline: how many years of the household's own life the
                // investable capital would pay for.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    RowLayout {
                        spacing: Theme.spaceSm

                        Text {
                            text: page.dashboard.yearsOfFreedomLabel
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontHero
                            font.weight: Font.Light
                        }

                        Text {
                            text: qsTr("years of freedom")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontTitle
                            // The unit sits on the headline's baseline rather
                            // than the bottom of its box.
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: Theme.spaceMd - 2
                        }
                    }

                    Text {
                        text: qsTr("Investable capital divided by a year of expenses.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        visible: page.dashboard.fiTargetKnown
                        spacing: Theme.spaceSm

                        Text {
                            text: qsTr("FI progress")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            text: page.dashboard.fiProgressLabel
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                    }

                    Item {
                        id: fiBar

                        Layout.fillWidth: true
                        implicitHeight: 28
                        visible: page.dashboard.fiTargetKnown

                        Rectangle {
                            id: fiTrack

                            width: parent.width
                            height: 8
                            radius: Theme.radiusSmall
                            color: Theme.surfaceRaised
                        }

                        Rectangle {
                            x: page.dashboard.fiTargetLowRatio * fiBar.width
                            width: Math.max(0, (page.dashboard.fiTargetHighRatio
                                                - page.dashboard.fiTargetLowRatio) * fiBar.width)
                            height: fiTrack.height
                            radius: Theme.radiusSmall
                            color: Qt.rgba(Theme.positive.r, Theme.positive.g, Theme.positive.b, 0.24)
                        }

                        Rectangle {
                            width: page.dashboard.fiProgressRatio * fiBar.width
                            height: fiTrack.height
                            radius: Theme.radiusSmall
                            color: Theme.textSecondary
                        }

                        Repeater {
                            model: page.fiTicks

                            delegate: Item {
                                id: tick

                                required property var modelData

                                x: tick.modelData.position * fiBar.width
                                width: 0
                                height: fiBar.height

                                Rectangle {
                                    x: -0.5
                                    width: 1
                                    height: fiTrack.height
                                    color: Theme.background
                                }

                                Text {
                                    x: -width / 2
                                    y: fiTrack.height + Theme.spaceXs
                                    text: qsTr("x%1").arg(tick.modelData.multiplier)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }
                }

                // The four buckets, each on its own band.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spaceMd
                    rowSpacing: Theme.spaceMd

                    Repeater {
                        model: page.dashboard.buckets

                        delegate: BucketCard {
                            required property string label
                            required property string horizonLabel
                            required property string balanceLabel
                            required property string targetLabel
                            required property string colourKey
                            required property real bandStart
                            required property real bandEnd
                            required property real balanceMarker
                            required property real capMarker
                            required property bool hasTarget
                            required property bool hasCap

                            Layout.fillWidth: true
                            title: label
                            horizon: horizonLabel
                            amount: balanceLabel
                            target: targetLabel
                            accentKey: colourKey
                            bandStartRatio: bandStart
                            bandEndRatio: bandEnd
                            balanceRatio: balanceMarker
                            capRatio: capMarker
                            bandShown: hasTarget
                            capShown: hasCap
                        }
                    }
                }

                // Net worth: the risk view and the cautious view, one at a
                // time through a toggle rather than two competing figures.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: netWorthContent.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: netWorthContent

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Net worth")
                        }

                        Text {
                            text: page.netWorthIncludesPension
                                  ? page.dashboard.netWorthLabel
                                  : page.dashboard.financialAssetsLabel
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontDisplay
                            font.weight: Font.Light
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Rectangle {
                                Layout.alignment: Qt.AlignLeft
                                implicitWidth: segmentRow.implicitWidth + 2 * Theme.spaceXs
                                implicitHeight: segmentRow.implicitHeight + 2 * Theme.spaceXs
                                radius: Theme.radiusSmall
                                color: Theme.background
                                border.color: Theme.border
                                border.width: 1

                                RowLayout {
                                    id: segmentRow

                                    anchors.centerIn: parent
                                    spacing: 2

                                    Segment {
                                        text: qsTr("Including pension and housing")
                                        selected: page.netWorthIncludesPension
                                        onClicked: page.netWorthIncludesPension = true
                                    }

                                    Segment {
                                        text: qsTr("Financial assets only")
                                        selected: !page.netWorthIncludesPension
                                        onClicked: page.netWorthIncludesPension = false
                                    }
                                }
                            }

                            Text {
                                text: page.netWorthIncludesPension
                                      ? qsTr("Everything owned, housing and pension "
                                             + "included, minus every debt.")
                                      : qsTr("Assets outside housing and pension, "
                                             + "before debts.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceLg

                            StatBlock {
                                Layout.fillWidth: true
                                label: qsTr("Crash impact")
                                value: page.dashboard.crashImpactLabel
                                note: qsTr("of total net worth if the growth bucket falls by half")
                            }

                            StatBlock {
                                Layout.fillWidth: true
                                label: qsTr("Housing and pension")
                                value: page.dashboard.pensionAndHousingShareLabel
                                note: qsTr("of all assets")
                            }
                        }
                    }
                }

                // The rules, most urgent first.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    SectionHeader {
                        Layout.fillWidth: true
                        title: qsTr("Insights")
                        note: page.dashboard.insightsCount > 0
                              ? (page.dashboard.insightsCount === 1
                                 ? qsTr("1 check")
                                 : qsTr("%1 checks").arg(page.dashboard.insightsCount))
                              : ""
                    }

                    Text {
                        visible: page.dashboard.insightsCount === 0
                        text: qsTr("No warnings. The model has nothing to flag.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        visible: page.dashboard.insightsCount > 0
                        Layout.fillWidth: true
                        implicitHeight: insightList.implicitHeight + 2 * Theme.spaceLg
                        radius: Theme.radius
                        color: Theme.surface
                        border.color: Theme.border
                        border.width: 1

                        ColumnLayout {
                            id: insightList

                            anchors.fill: parent
                            anchors.margins: Theme.spaceLg
                            spacing: Theme.spaceMd

                            Repeater {
                                model: page.dashboard.insights

                                // The card's own properties carry the model's
                                // vocabulary, so the roles are read through
                                // `model` rather than through required names
                                // that would shadow them.
                                delegate: ColumnLayout {
                                    id: insightRow

                                    required property int index
                                    required property var model

                                    Layout.fillWidth: true
                                    visible: insightRow.index < page.insightLimit
                                    spacing: Theme.spaceMd

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 1
                                        color: Theme.border
                                        visible: insightRow.index > 0
                                    }

                                    InsightCard {
                                        Layout.fillWidth: true
                                        severity: insightRow.model.severity
                                        severityKey: insightRow.model.severityKey
                                        title: insightRow.model.title
                                        why: insightRow.model.why
                                        suggestion: insightRow.model.suggestion
                                    }
                                }
                            }

                            Text {
                                visible: page.dashboard.insightsCount > page.insightLimit
                                text: qsTr("and %1 more").arg(page.dashboard.insightsCount
                                                             - page.insightLimit)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        // Nothing entered yet: say what the application will ask for, and how
        // it will use it, without pretending the button works.
        ColumnLayout {
            visible: !page.dashboard.hasProfile
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 460
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spaceMd

            Item {
                Layout.fillHeight: true
            }

            Text {
                text: qsTr("Set up your economy")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("The model sorts what you own into four buckets by when you "
                          + "will need it: buffer, preserve value, growth and play. Every "
                          + "target is a multiple of what you spend, so the plan fits your "
                          + "life rather than a fixed sum.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Button {
                id: setupButton

                text: qsTr("Start setup")
                enabled: false
                Layout.alignment: Qt.AlignHCenter
                padding: 0
                implicitWidth: setupLabel.implicitWidth + 2 * Theme.spaceLg
                implicitHeight: setupLabel.implicitHeight + Theme.spaceMd

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: Theme.border
                    border.width: 1
                }

                contentItem: Text {
                    id: setupLabel

                    text: setupButton.text
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Text {
                text: qsTr("Data entry arrives in the next update.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
