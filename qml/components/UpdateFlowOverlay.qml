// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The recurring task as one object: update the balances, then the month's
// cashflow, then go and look at what changed.
//
// It covers the window rather than floating over it. A dialog says "deal with
// this and come back"; this is the thing the household came to do, and the two
// steps are long enough to need the height. Each step saves on its own, so
// leaving after one of them is a finished visit and not an abandoned one.
Item {
    id: root

    required property UpdateFlowViewModel flow

    property int step: 0

    signal closeRequested()

    readonly property var stepTitles: [
        qsTr("Balances"),
        qsTr("Cashflow")
    ]

    function requestClose() {
        if (root.flow.dirty)
            confirmLeave.open();
        else
            root.closeRequested();
    }

    // The picker is driven by the model, so it is put back whenever the model
    // resets the month - opening the flow always starts at the current one.
    function syncMonth() {
        if (monthField.currentIndex !== root.flow.monthIndex)
            monthField.currentIndex = root.flow.monthIndex;
    }

    focus: true
    activeFocusOnTab: true

    Keys.onEscapePressed: root.requestClose()

    Connections {
        target: root.flow

        function onChanged() {
            root.syncMonth();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceMd

        // ---------------------------------------------------------------
        // Where you are, which month, and what each step is worth so far
        // ---------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            ColumnLayout {
                spacing: 2

                Text {
                    text: root.flow.actionLabel
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.Light
                }

                Text {
                    text: qsTr("Step %1 of 2 · %2")
                              .arg(root.step + 1)
                              .arg(root.stepTitles[root.step])
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                }
            }

            Item {
                Layout.fillWidth: true
            }

            PrimaryButton {
                text: qsTr("Close")
                filled: false
                onClicked: root.requestClose()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceLg

            FormRow {
                Layout.preferredWidth: 240
                label: qsTr("Month")
                hint: qsTr("This month or earlier")

                ChoiceField {
                    id: monthField

                    Layout.fillWidth: true
                    model: root.flow.monthLabels
                    currentIndex: root.flow.monthIndex
                    onActivated: (index) => root.flow.monthIndex = index
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    visible: text.length > 0
                    text: root.flow.monthNote
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: text.length > 0
                    text: root.flow.resultNote
                    color: Theme.positive
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Both steps, always: a visit that saved one of them says exactly
        // which one, and that the other was not touched.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Text {
                text: qsTr("Balances")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }

            Text {
                text: root.flow.balancesStatus
                color: Theme.statusColour(root.flow.balancesStatusKey)
                font.pixelSize: Theme.fontCaption
            }

            Rectangle {
                implicitWidth: 1
                implicitHeight: 12
                color: Theme.border
            }

            Text {
                text: qsTr("Cashflow")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }

            Text {
                text: root.flow.cashflowStatus
                color: Theme.statusColour(root.flow.cashflowStatusKey)
                font.pixelSize: Theme.fontCaption
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // ---------------------------------------------------------------
        // The step itself
        // ---------------------------------------------------------------
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.step

            // Step 1: every asset and every debt, one month
            ScrollView {
                id: balancesScroll

                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: balancesScroll.availableWidth
                    spacing: Theme.spaceSm

                    Text {
                        visible: !root.flow.hasAccounts
                        text: qsTr("There is nothing to update yet. Add an account on "
                             + "Accounts & buckets, and it appears here with everything "
                             + "else you own and owe.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: root.flow.hasAccounts
                        text: qsTr("Every account and every debt, for %1. A field left "
                             + "empty records nothing for that row.")
                                  .arg(root.flow.monthLabel)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.flow.balances

                        delegate: RowLayout {
                            id: balanceRow

                            required property int index
                            required property string name
                            required property string detailLabel
                            required property bool isDebt
                            required property string recordLabel
                            required property string amountText
                            required property string note
                            required property string fieldError

                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: balanceRow.name
                                    textFormat: Text.PlainText
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: balanceRow.isDebt
                                          ? qsTr("%1 · owed").arg(balanceRow.detailLabel)
                                          : balanceRow.detailLabel
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: balanceRow.recordLabel
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 220
                                spacing: 2

                                MoneyField {
                                    text: balanceRow.amountText
                                    placeholder: qsTr("Not recorded")
                                    unit: root.flow.currencyCode
                                    error: balanceRow.fieldError
                                    onEdited: (value) => root.flow.balances.setAmountText(
                                                    balanceRow.index, value)
                                }

                                Text {
                                    visible: text.length > 0
                                    text: balanceRow.note
                                    color: Theme.positive
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            // Step 2: the month's cashflow, in the cashflow page's own editor
            ScrollView {
                id: cashflowScroll

                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: cashflowScroll.availableWidth
                    spacing: Theme.spaceMd

                    Text {
                        text: qsTr("What came in and went out in %1. The buckets are "
                             + "sized from this month, which is why the balances alone "
                             + "cannot move the plan.")
                                  .arg(root.flow.monthLabel)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    MonthFlowForm {
                        Layout.fillWidth: true
                        model: root.flow.cashflow
                    }
                }
            }
        }

        // ---------------------------------------------------------------
        // Back, forward, and the save for the step on screen
        // ---------------------------------------------------------------
        Text {
            visible: root.step === 0 && text.length > 0
            text: root.flow.balancesError
            textFormat: Text.PlainText
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            PrimaryButton {
                text: qsTr("Back")
                filled: false
                visible: root.step > 0
                onClicked: root.step -= 1
            }

            Item {
                Layout.fillWidth: true
            }

            PrimaryButton {
                text: qsTr("Save balances")
                visible: root.step === 0
                enabled: root.flow.hasAccounts
                onClicked: root.flow.saveBalances()
            }

            PrimaryButton {
                text: qsTr("Save cashflow")
                visible: root.step === 1
                enabled: root.flow.cashflow.savingSplitValid
                onClicked: root.flow.saveCashflow()
            }

            PrimaryButton {
                text: qsTr("Next: cashflow")
                filled: false
                visible: root.step === 0
                onClicked: root.step = 1
            }

            PrimaryButton {
                text: qsTr("Done")
                filled: false
                visible: root.step === 1
                onClicked: root.requestClose()
            }
        }
    }

    ConfirmDialog {
        id: confirmLeave

        title: qsTr("Leave without saving?")
        message: qsTr("Something you typed in this visit has not been saved. Leaving "
             + "now loses it; each step keeps only what you saved.")
        confirmLabel: qsTr("Leave without saving")

        onConfirmed: root.closeRequested()
    }
}
