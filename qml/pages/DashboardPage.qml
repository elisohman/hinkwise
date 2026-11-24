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

    /// Opening the update flow, and the year-end review, are the page's to ask
    /// for: the window owns both, so the same sheet opens from every entry
    /// point instead of a copy per page.
    signal updateRequested()
    signal reviewRequested()

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

        // The recurring task, before any figure: what is on record, what is
        // missing from it, and the way to fix that. It never names a single
        // month for data that has two.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statusContent.implicitHeight + 2 * Theme.spaceMd
            visible: page.dashboard.hasProfile
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            border.width: 1

            RowLayout {
                id: statusContent

                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                spacing: Theme.spaceMd

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.spaceSm
                    implicitWidth: 6
                    implicitHeight: 6
                    radius: 3
                    color: page.dashboard.inputsUpToDate ? Theme.positive : Theme.warning
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: page.dashboard.inputStatusHeadline
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: text.length > 0
                        text: page.dashboard.inputStatusNote
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }

                PrimaryButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: page.dashboard.updateActionLabel
                    onClicked: page.updateRequested()
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

                // Once a year, not every month: the review checks a year of
                // records and plans the next one, so it sits at the end of the
                // page rather than in the rail.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceSm
                    spacing: Theme.spaceMd

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: qsTr("Year-end review")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                        }

                        Text {
                            text: qsTr("Once a year: what the year added, and what the "
                                 + "next one needs.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    PrimaryButton {
                        text: qsTr("Open the review")
                        filled: false
                        onClicked: page.reviewRequested()
                    }
                }
            }
        }
    }
}
