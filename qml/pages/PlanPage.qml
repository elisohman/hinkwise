// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Hinkwise

// Where the household is on the way to freedom, and what the future looks like
// from here: the x21 / x25 / x33 band, a straight-line projection of today's
// saving, what an extra saving would do, and the assumptions the projection
// rests on.
//
// The year-end review lives here rather than in the rail: it is an annual
// ritual, not a place you go every day.
Item {
    id: page

    /// The review is the page's own state, because the Overview opens it too.
    property bool showingReview: false

    /// What the update action is called in the tracking mode in force. Main
    /// passes it down from the dashboard, which is the one place that knows
    /// which mode the household chose.
    required property string updateActionLabel

    signal navigateRequested(int index)
    signal updateRequested()

    // The projection as the domain computed it: stacked bands per bucket, one
    // line along the top for the total, the FI target as a mark, and the year
    // the target is reached marked on the axis. Every position is a 0..1 ratio;
    // this turns them into pixels and nothing else.
    component ProjectionChart: Item {
        id: chart

        property var bands: []
        property var yLabels: []
        property var xLabels: []
        property var debtSeries: []
        property var netWorthSeries: []
        property bool hasDebt: false
        property real zeroRatio: 0
        property bool hasTarget: false
        property real targetRatio: 0
        property bool hasMarker: false
        property real markerRatio: 0
        property string markerLabel: ""

        implicitHeight: 240

        function seriesLine(points) {
            var result = [];
            for (var index = 0; index < points.length; ++index) {
                result.push(Qt.point(points[index].x * plot.width,
                                     (1 - points[index].y) * plot.height));
            }
            return result;
        }

        function topLine(key) {
            var result = [];
            for (var index = 0; index < chart.bands.length; ++index) {
                result.push(Qt.point(chart.bands[index].x * plot.width,
                                     (1 - chart.bands[index][key]) * plot.height));
            }
            return result;
        }

        function bandPath(topKey, bottomKey) {
            var result = [];
            for (var index = 0; index < chart.bands.length; ++index) {
                result.push(Qt.point(chart.bands[index].x * plot.width,
                                     (1 - chart.bands[index][topKey]) * plot.height));
            }
            for (var back = chart.bands.length - 1; back >= 0; --back) {
                // The lowest band sits on zero, not on the rim: when the axis
                // reaches below zero the rim is a negative figure.
                var floor = bottomKey.length === 0 ? chart.zeroRatio
                                                   : chart.bands[back][bottomKey];
                result.push(Qt.point(chart.bands[back].x * plot.width,
                                     (1 - floor) * plot.height));
            }
            return result;
        }

        Item {
            id: plot

            anchors.left: parent.left
            anchors.leftMargin: 88
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - 26

            Repeater {
                model: chart.yLabels

                delegate: Rectangle {
                    required property var modelData

                    x: 0
                    y: (1 - modelData.y) * plot.height
                    width: plot.width
                    height: 1
                    color: Theme.border
                    opacity: 0.6
                }
            }

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: Qt.rgba(Theme.buffer.r, Theme.buffer.g, Theme.buffer.b, 0.42)
                    strokeColor: "transparent"

                    PathPolyline {
                        path: chart.bandPath("bufferTop", "")
                    }
                }
            }

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: Qt.rgba(Theme.preserve.r, Theme.preserve.g, Theme.preserve.b, 0.42)
                    strokeColor: "transparent"

                    PathPolyline {
                        path: chart.bandPath("preserveTop", "bufferTop")
                    }
                }
            }

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: Qt.rgba(Theme.growth.r, Theme.growth.g, Theme.growth.b, 0.42)
                    strokeColor: "transparent"

                    PathPolyline {
                        path: chart.bandPath("growthTop", "preserveTop")
                    }
                }
            }

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: Qt.rgba(Theme.play.r, Theme.play.g, Theme.play.b, 0.42)
                    strokeColor: "transparent"

                    PathPolyline {
                        path: chart.bandPath("playTop", "growthTop")
                    }
                }
            }

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.textSecondary
                    strokeWidth: 1.5

                    PathPolyline {
                        path: chart.topLine("playTop")
                    }
                }
            }

            // The loan falling, dashed so it reads as a balance being paid off
            // rather than as a fifth bucket. Net worth is what everything owned
            // comes to once every debt is subtracted - the bands, but also the
            // house and the pensions - so the crossing says more than either
            // line alone.
            Shape {
                anchors.fill: parent
                visible: chart.hasDebt && chart.debtSeries.length > 1

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.danger
                    strokeWidth: 1.5
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [5, 4]

                    PathPolyline {
                        path: chart.seriesLine(chart.debtSeries)
                    }
                }
            }

            Shape {
                anchors.fill: parent
                visible: chart.hasDebt && chart.netWorthSeries.length > 1

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.textPrimary
                    strokeWidth: 1.5

                    PathPolyline {
                        path: chart.seriesLine(chart.netWorthSeries)
                    }
                }
            }

            // Zero stays marked when the axis reaches below it: without this
            // line a negative net worth would look like a small positive one.
            Rectangle {
                visible: chart.hasDebt && chart.zeroRatio > 0
                x: 0
                y: (1 - chart.zeroRatio) * plot.height
                width: plot.width
                height: 1
                color: Theme.textMuted
                opacity: 0.6
            }

            Rectangle {
                visible: chart.hasTarget
                x: 0
                y: (1 - chart.targetRatio) * plot.height
                width: plot.width
                height: 1
                color: Theme.positive
                opacity: 0.8
            }

            Text {
                visible: chart.hasTarget
                anchors.right: parent.right
                y: (1 - chart.targetRatio) * plot.height - height - 2
                text: qsTr("FI target")
                color: Theme.positive
                font.pixelSize: Theme.fontCaption
            }

            Rectangle {
                visible: chart.hasMarker
                x: chart.markerRatio * plot.width
                y: 0
                width: 1
                height: plot.height
                color: Theme.positive
                opacity: 0.5
            }

            Text {
                visible: chart.hasMarker
                x: chart.markerRatio * plot.width + Theme.spaceXs
                y: 0
                text: chart.markerLabel
                color: Theme.positive
                font.pixelSize: Theme.fontCaption
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        Repeater {
            model: chart.yLabels

            delegate: Text {
                required property var modelData

                x: plot.x - width - Theme.spaceSm
                y: (1 - modelData.y) * plot.height - height / 2
                text: modelData.label
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }

        Repeater {
            model: chart.xLabels

            delegate: Text {
                required property var modelData

                x: plot.x + modelData.x * plot.width - width / 2
                y: plot.height + Theme.spaceXs
                text: modelData.label
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    PlanViewModel {
        id: plan
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: page.showingReview ? 1 : 0

        // ---------------------------------------------------------------
        // The plan
        // ---------------------------------------------------------------
        Item {
            id: planPage

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spaceLg
                spacing: Theme.spaceLg

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: qsTr("Plan")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontDisplay
                        font.weight: Font.Light
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    PrimaryButton {
                        text: qsTr("Year-end review")
                        filled: false
                        onClicked: page.showingReview = true
                    }
                }

                ScrollView {
                    id: planScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: planScroll.availableWidth
                        spacing: Theme.spaceLg

                        // -----------------------------------------------
                        // Years of freedom and the FI band
                        // -----------------------------------------------
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: freedomCard.implicitHeight + 2 * Theme.spaceLg
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                id: freedomCard

                                anchors.fill: parent
                                anchors.margins: Theme.spaceLg
                                spacing: Theme.spaceSm

                                RowLayout {
                                    spacing: Theme.spaceSm

                                    Text {
                                        text: plan.yearsOfFreedomLabel
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontHero
                                        font.weight: Font.Light
                                    }

                                    Text {
                                        text: qsTr("years of freedom")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontTitle
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
                                    Layout.topMargin: Theme.spaceMd
                                    visible: plan.fiTargetKnown
                                    spacing: Theme.spaceSm

                                    Text {
                                        text: qsTr("FI progress · %1 of the x%2 target")
                                                  .arg(plan.fiProgressLabel)
                                                  .arg(plan.fiMultiplierDefault)
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontCaption
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: qsTr("x%1  ·  x%2  ·  x%3")
                                                  .arg(plan.fiMultiplierLow)
                                                  .arg(plan.fiMultiplierDefault)
                                                  .arg(plan.fiMultiplierHigh)
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }

                                Item {
                                    id: fiBar

                                    Layout.fillWidth: true
                                    implicitHeight: 30
                                    visible: plan.fiTargetKnown

                                    Rectangle {
                                        id: fiTrack

                                        width: parent.width
                                        height: 8
                                        radius: Theme.radiusSmall
                                        color: Theme.surfaceRaised
                                    }

                                    Rectangle {
                                        x: plan.fiTargetLowRatio * fiBar.width
                                        width: Math.max(0, (plan.fiTargetHighRatio
                                                            - plan.fiTargetLowRatio) * fiBar.width)
                                        height: fiTrack.height
                                        radius: Theme.radiusSmall
                                        color: Qt.rgba(Theme.positive.r, Theme.positive.g,
                                                       Theme.positive.b, 0.24)
                                    }

                                    Rectangle {
                                        width: plan.fiProgressRatio * fiBar.width
                                        height: fiTrack.height
                                        radius: Theme.radiusSmall
                                        color: Theme.textSecondary
                                    }

                                    Repeater {
                                        model: [
                                            {
                                                position: plan.fiTargetLowRatio,
                                                label: qsTr("x%1").arg(plan.fiMultiplierLow)
                                            },
                                            {
                                                position: plan.fiTargetDefaultRatio,
                                                label: qsTr("x%1").arg(plan.fiMultiplierDefault)
                                            },
                                            {
                                                position: plan.fiTargetHighRatio,
                                                label: qsTr("x%1").arg(plan.fiMultiplierHigh)
                                            }
                                        ]

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
                                                text: tick.modelData.label
                                                color: Theme.textMuted
                                                font.pixelSize: Theme.fontCaption
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: Theme.spaceSm
                                    visible: plan.fiTargetKnown
                                    spacing: Theme.spaceLg

                                    StatBlock {
                                        Layout.fillWidth: true
                                        label: qsTr("x%1").arg(plan.fiMultiplierLow)
                                        value: plan.fiTargetLowLabel
                                        note: qsTr("The cautious target")
                                    }

                                    StatBlock {
                                        Layout.fillWidth: true
                                        label: qsTr("x%1").arg(plan.fiMultiplierDefault)
                                        value: plan.fiTargetDefaultLabel
                                        note: qsTr("The classic 4 % rule")
                                    }

                                    StatBlock {
                                        Layout.fillWidth: true
                                        label: qsTr("x%1").arg(plan.fiMultiplierHigh)
                                        value: plan.fiTargetHighLabel
                                        note: qsTr("After capital tax")
                                    }
                                }

                                Text {
                                    visible: !plan.fiTargetKnown
                                    text: qsTr("The targets are multiples of what the household "
                                         + "spends, so they appear once a month has been recorded.")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontBody
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // -----------------------------------------------
                        // The projection
                        // -----------------------------------------------
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: projectionCard.implicitHeight + 2 * Theme.spaceLg
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                id: projectionCard

                                anchors.fill: parent
                                anchors.margins: Theme.spaceLg
                                spacing: Theme.spaceMd

                                SectionHeader {
                                    Layout.fillWidth: true
                                    title: qsTr("Projection")
                                    note: qsTr("%1 years").arg(plan.horizonYears)
                                }

                                // The projection without its inputs: what it needs,
                                // and the way to enter it, rather than a bare
                                // absence.
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: plan.chartEmpty
                                    spacing: Theme.spaceSm

                                    Text {
                                        text: qsTr("No projection yet")
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontTitle
                                        font.weight: Font.Light
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: qsTr("Record your accounts and a month of cashflow "
                                             + "and the plan can project from there.")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontBody
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }

                                    PrimaryButton {
                                        text: page.updateActionLabel
                                        onClicked: page.updateRequested()
                                    }
                                }

                                ProjectionChart {
                                    Layout.fillWidth: true
                                    visible: !plan.chartEmpty
                                    bands: plan.projectionBands
                                    yLabels: plan.projectionYLabels
                                    xLabels: plan.projectionXLabels
                                    debtSeries: plan.debtSeries
                                    netWorthSeries: plan.netWorthSeries
                                    hasDebt: plan.hasDebt
                                    zeroRatio: plan.zeroLineRatio
                                    hasTarget: plan.hasTargetLine
                                    targetRatio: plan.targetLineRatio
                                    hasMarker: plan.hasFiMarker
                                    markerRatio: plan.fiMarkerRatio
                                    markerLabel: plan.fiMarkerLabel
                                }

                                // The debt in one line: what is owed today, when the
                                // recorded amortisation clears it, and what is left at
                                // the end once it is subtracted.
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: plan.hasDebt
                                    spacing: Theme.spaceLg

                                    StatBlock {
                                        Layout.fillWidth: true
                                        label: qsTr("Debt today")
                                        value: plan.debtTodayLabel
                                        note: qsTr("Everything the balance sheet says is owed")
                                    }

                                    StatBlock {
                                        Layout.fillWidth: true
                                        visible: !plan.debtWithoutAmortisation
                                        label: qsTr("Debt-free")
                                        value: plan.debtFreeLabel
                                        note: qsTr("At the recorded amortisation")
                                    }

                                    StatBlock {
                                        Layout.fillWidth: true
                                        label: qsTr("Net worth at the end")
                                        value: plan.netWorthEndLabel
                                        note: qsTr("In %1 years: everything owned less everything "
                                             + "owed").arg(plan.horizonYears)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: plan.debtWithoutAmortisation
                                    spacing: Theme.spaceMd

                                    Text {
                                        Layout.fillWidth: true
                                        text: plan.amortisationNote
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }

                                    PrimaryButton {
                                        text: qsTr("Cashflow")
                                        filled: false
                                        onClicked: page.navigateRequested(2)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceLg

                                    Repeater {
                                        model: plan.bucketLegend

                                        delegate: RowLayout {
                                            id: legendRow

                                            required property var modelData

                                            spacing: Theme.spaceXs

                                            Rectangle {
                                                implicitWidth: 8
                                                implicitHeight: 8
                                                radius: 4
                                                color: Theme.bucketColour(legendRow.modelData.colourKey)
                                            }

                                            Text {
                                                text: legendRow.modelData.label
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontCaption
                                            }
                                        }
                                    }

                                    RowLayout {
                                        visible: plan.hasDebt
                                        spacing: Theme.spaceXs

                                        Rectangle {
                                            implicitWidth: 12
                                            implicitHeight: 2
                                            color: Theme.danger
                                        }

                                        Text {
                                            text: qsTr("Debt")
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                        }
                                    }

                                    RowLayout {
                                        visible: plan.hasDebt
                                        spacing: Theme.spaceXs

                                        Rectangle {
                                            implicitWidth: 12
                                            implicitHeight: 2
                                            color: Theme.textPrimary
                                        }

                                        Text {
                                            text: qsTr("Net worth")
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: qsTr("The bands are investable capital only: the house "
                                         + "and the pensions count in net worth, not in spendable "
                                         + "money. The debt falls by the amortisation recorded in "
                                         + "cashflow.")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: plan.chartCaption
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.border
                                }

                                // The what-if amount, and what it would change.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceMd

                                    FormRow {
                                        Layout.preferredWidth: 240
                                        label: qsTr("Extra saving each month")
                                        hint: plan.currencyCode

                                        MoneyField {
                                            id: extraField

                                            enabled: plan.extraSavingAvailable
                                            placeholder: qsTr("0")
                                            unit: plan.currencyCode
                                            error: plan.extraSavingError
                                            onEdited: (value) => plan.setExtraMonthlySaving(value)
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spaceXs

                                        Text {
                                            text: qsTr("At today's saving: %1")
                                                      .arg(plan.baseYearsLabel)
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            visible: plan.extraSavingActive
                                            text: plan.extraYearLabel.length > 0
                                                  ? qsTr("With the extra, %1: %2")
                                                        .arg(plan.extraYearLabel)
                                                        .arg(plan.extraYearsLabel)
                                                  : plan.extraYearsLabel
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            visible: plan.extraSavingActive && text.length > 0
                                            text: plan.fiDeltaLabel
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }

                                Text {
                                    visible: !plan.extraSavingAvailable
                                    text: qsTr("In the preserving phases the model has no next "
                                         + "contribution to add to: the buckets are drawn down, so "
                                         + "an extra saving has nowhere to go.")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // -----------------------------------------------
                        // The assumptions
                        // -----------------------------------------------
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: assumptionsCard.implicitHeight + 2 * Theme.spaceLg
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                id: assumptionsCard

                                anchors.fill: parent
                                anchors.margins: Theme.spaceLg
                                spacing: Theme.spaceSm

                                SectionHeader {
                                    Layout.fillWidth: true
                                    title: qsTr("Assumptions")
                                }

                                Text {
                                    text: qsTr("The real return used for each bucket, at the middle "
                                         + "of the band the model states, with that band beside it.")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Repeater {
                                    model: plan.assumptions

                                    delegate: RowLayout {
                                        id: assumptionRow

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: Theme.spaceSm

                                        Rectangle {
                                            implicitWidth: 6
                                            implicitHeight: 6
                                            radius: 3
                                            color: Theme.bucketColour(assumptionRow.modelData.colourKey)
                                        }

                                        Text {
                                            text: assumptionRow.modelData.label
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: assumptionRow.modelData.usedLabel
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                        }

                                        Text {
                                            text: assumptionRow.modelData.rangeLabel
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontCaption
                                            Layout.preferredWidth: 130
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.topMargin: Theme.spaceXs
                                    implicitHeight: 1
                                    color: Theme.border
                                }

                                Text {
                                    text: qsTr("Returns are real: after inflation. Every figure is "
                                         + "in today's money, so a number read in ten years buys what "
                                         + "it buys today.")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: qsTr("A straight-line projection, not a forecast. It adds "
                                         + "the same saving every year and applies the model's "
                                         + "long-run average return evenly, which no market does.")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        }
                    }
            }
        }

        // ---------------------------------------------------------------
        // The year-end review
        // ---------------------------------------------------------------
        YearEndReviewPage {
            onBack: page.showingReview = false
            onNavigateRequested: (index) => {
                page.showingReview = false;
                page.navigateRequested(index);
            }
        }
    }
}
