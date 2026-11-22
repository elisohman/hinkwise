// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Hinkwise

// What came in and where it went: this month's 50/30/20 split, the twelve
// months behind it, and the savings rate over the same window.
//
// Every figure is the model's. The shares come from the domain's metrics, the
// history from the recorded month flows, and a month nobody recorded is drawn
// as an empty slot - not as a bar at zero, which would be a claim about the
// household instead of a fact about the record.
Item {
    id: page

    // One horizontal stacked bar: needs, then wants, then saving, each a share
    // of income. The colours borrow the buckets they behave like - the buffer's
    // blue for needs, play's violet for wants, growth's gold for saving.
    component SplitBar: Item {
        id: bar

        property real needsRatio: 0
        property real wantsRatio: 0
        property real savingRatio: 0

        implicitHeight: 16

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
        }

        Rectangle {
            width: bar.width * bar.needsRatio
            height: parent.height
            color: Theme.buffer
        }

        Rectangle {
            x: bar.width * bar.needsRatio
            width: bar.width * bar.wantsRatio
            height: parent.height
            color: Theme.play
        }

        Rectangle {
            x: bar.width * (bar.needsRatio + bar.wantsRatio)
            width: bar.width * bar.savingRatio
            height: parent.height
            color: Theme.growth
        }
    }

    // The savings rate as one line, with the model's 10 % and 20 % marks.
    // Points arrive as 0..1 positions; this only turns them into pixels.
    component RateChart: Item {
        id: chart

        property var points: []
        property var references: []
        property string topLabel: ""
        property string firstLabel: ""
        property string lastLabel: ""

        implicitHeight: 150

        function polyline(series) {
            var result = [];
            for (var index = 0; index < series.length; ++index) {
                result.push(Qt.point(series[index].x * plot.width,
                                     (1 - series[index].y) * plot.height));
            }
            return result;
        }

        Item {
            id: plot

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - 20

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }

            Repeater {
                model: chart.references

                delegate: Item {
                    id: reference

                    required property var modelData

                    width: plot.width
                    height: referenceLabel.implicitHeight
                    y: (1 - reference.modelData.y) * plot.height - height / 2

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }

                    Text {
                        id: referenceLabel

                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceXs
                        text: reference.modelData.label
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }

            Shape {
                anchors.fill: parent
                visible: chart.points.length > 1

                ShapePath {
                    strokeColor: Theme.textPrimary
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap

                    PathPolyline {
                        path: chart.polyline(chart.points)
                    }
                }
            }

            Repeater {
                model: chart.points

                delegate: Rectangle {
                    id: point

                    required property var modelData

                    x: modelData.x * plot.width - width / 2
                    y: (1 - modelData.y) * plot.height - height / 2
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.textPrimary
                }
            }

            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: chart.topLabel
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }

        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: chart.firstLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: chart.lastLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }
    }

    CashflowViewModel {
        id: cashflow
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: qsTr("Cashflow")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("Showing %1").arg(cashflow.selectedMonthLabel)
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
            }
        }

        ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.spaceLg

                // -----------------------------------------------------------
                // This month, editable
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: monthForm.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: monthForm

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("This month")
                            note: cashflow.currentMonthLabel
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spaceMd
                            rowSpacing: Theme.spaceSm

                            FormRow {
                                label: qsTr("Income after tax")
                                hint: cashflow.currencyCode

                                MoneyField {
                                    id: incomeField

                                    text: cashflow.incomeText
                                    placeholder: qsTr("What came in")
                                    unit: cashflow.currencyCode
                                }
                            }

                            FormRow {
                                label: qsTr("Needs")
                                hint: cashflow.currencyCode

                                MoneyField {
                                    id: needsField

                                    text: cashflow.needsText
                                    placeholder: qsTr("Rent, food, transport")
                                    unit: cashflow.currencyCode
                                }
                            }

                            FormRow {
                                label: qsTr("Wants")
                                hint: cashflow.currencyCode

                                MoneyField {
                                    id: wantsField

                                    text: cashflow.wantsText
                                    placeholder: qsTr("Everything else you choose")
                                    unit: cashflow.currencyCode
                                }
                            }

                            FormRow {
                                label: qsTr("Saving")
                                hint: cashflow.currencyCode

                                MoneyField {
                                    id: savingField

                                    text: cashflow.savingText
                                    placeholder: qsTr("Paid to yourself, amortisation included")
                                    unit: cashflow.currencyCode
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            PrimaryButton {
                                text: qsTr("Record %1").arg(cashflow.currentMonthLabel)
                                onClicked: cashflow.saveMonth(incomeField.text, needsField.text,
                                                              wantsField.text, savingField.text)
                            }

                            Text {
                                visible: text.length > 0
                                text: cashflow.savedNote
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            visible: text.length > 0
                            text: cashflow.saveError
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: text.length > 0
                            text: cashflow.savingNote
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // The split of the month on screen
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: splitCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: splitCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Needs, wants and saving")
                            note: cashflow.selectedMonthLabel
                        }

                        Text {
                            visible: !cashflow.selectedMonthRecorded
                            text: qsTr("No month was recorded here, so there is no split to show. "
                                 + "A gap in the record is not a month of zeros.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: cashflow.selectedMonthRecorded
                            spacing: Theme.spaceSm

                            SplitBar {
                                Layout.fillWidth: true
                                needsRatio: cashflow.needsRatio
                                wantsRatio: cashflow.wantsRatio
                                savingRatio: cashflow.savingRatio
                            }

                            Text {
                                text: qsTr("Model: %1 needs, %2 wants, %3 saving")
                                          .arg(cashflow.needsTargetLabel)
                                          .arg(cashflow.wantsTargetLabel)
                                          .arg(cashflow.savingTargetLabel)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spaceXs
                                spacing: Theme.spaceLg

                                StatBlock {
                                    Layout.fillWidth: true
                                    label: qsTr("Needs · %1 of income").arg(cashflow.needsShareLabel)
                                    value: cashflow.needsLabel
                                    note: cashflow.needsDeviationLabel
                                }

                                StatBlock {
                                    Layout.fillWidth: true
                                    label: qsTr("Wants · %1 of income").arg(cashflow.wantsShareLabel)
                                    value: cashflow.wantsLabel
                                    note: cashflow.wantsDeviationLabel
                                }

                                StatBlock {
                                    Layout.fillWidth: true
                                    label: qsTr("Saving · %1 of income").arg(cashflow.savingShareLabel)
                                    value: cashflow.savingLabel
                                    note: cashflow.savingDeviationLabel
                                }
                            }
                        }
                    }
                }

                // -----------------------------------------------------------
                // Twelve months of splits
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: historyCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: historyCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Twelve months")
                            note: cashflow.selectedMonthLabel
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceXs

                            Repeater {
                                model: cashflow.history

                                delegate: Item {
                                    id: historyColumn

                                    required property int index
                                    required property string label
                                    required property string fullLabel
                                    required property bool recorded
                                    required property real needsRatio
                                    required property real wantsRatio
                                    required property real savingRatio
                                    required property bool selected

                                    Layout.fillWidth: true
                                    implicitHeight: columnContent.implicitHeight

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Theme.radiusSmall
                                        color: historyColumn.selected ? Theme.surfaceRaised
                                                                      : "transparent"
                                    }

                                    ColumnLayout {
                                        id: columnContent

                                        anchors.fill: parent
                                        anchors.margins: 2
                                        spacing: 2

                                        Text {
                                            text: historyColumn.label
                                            color: historyColumn.selected ? Theme.textPrimary
                                                                          : Theme.textMuted
                                            font.pixelSize: Theme.fontCaption
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.fillWidth: true
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 84

                                            // An empty slot, drawn as an outline: the month
                                            // is part of the window and was never recorded.
                                            Rectangle {
                                                anchors.fill: parent
                                                visible: !historyColumn.recorded
                                                radius: 2
                                                color: "transparent"
                                                border.color: Theme.border
                                                border.width: 1
                                            }

                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                visible: historyColumn.recorded
                                                width: parent.width
                                                height: parent.height * historyColumn.needsRatio
                                                color: Theme.buffer
                                            }

                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: parent.height
                                                                    * historyColumn.needsRatio
                                                visible: historyColumn.recorded
                                                width: parent.width
                                                height: parent.height * historyColumn.wantsRatio
                                                color: Theme.play
                                            }

                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: parent.height
                                                                    * (historyColumn.needsRatio
                                                                       + historyColumn.wantsRatio)
                                                visible: historyColumn.recorded
                                                width: parent.width
                                                height: parent.height * historyColumn.savingRatio
                                                color: Theme.growth
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: cashflow.selectMonth(historyColumn.index)

                                        ToolTip.visible: containsMouse
                                        ToolTip.text: historyColumn.fullLabel
                                    }
                                }
                            }
                        }

                        Text {
                            text: qsTr("Each column is one month's income, split the same way. "
                                 + "An outlined column was never recorded.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // The savings rate over time
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: savingsCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: savingsCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Savings rate")
                            note: cashflow.selectedSavingsRateLabel
                        }

                        Text {
                            visible: cashflow.savingsEmpty
                            text: qsTr("The line appears once a month has been recorded. The "
                                 + "model's band is 10–20 % of income, amortisation included.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RateChart {
                            Layout.fillWidth: true
                            visible: !cashflow.savingsEmpty
                            points: cashflow.savingsPoints
                            references: cashflow.savingsReferences
                            topLabel: cashflow.savingsAxisTopLabel
                            firstLabel: cashflow.savingsFirstLabel
                            lastLabel: cashflow.savingsLastLabel
                        }
                    }
                }
            }
        }
    }
}
