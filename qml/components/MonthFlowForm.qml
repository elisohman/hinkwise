// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Hinkwise

// The month editor: income, needs, wants, saving and the optional breakdown,
// with the model's own reading of them.
//
// It is one component because it is one editor. The cashflow page writes a
// month with it and so does the update flow, so the fields, the validation and
// the sentences they produce cannot drift apart between the two. Nothing is
// written here: the host places the buttons and calls model.saveMonth(), which
// is what makes a step of the update flow save independently.
ColumnLayout {
    id: root

    required property CashflowViewModel model

    // A control stops following the view model once it has been typed in, so
    // the model pushes its own value back after a save, a discard or a reload.
    // The draft itself lives in the view model, which keeps it across reloads.
    function syncFields() {
        if (incomeField.text !== root.model.incomeText)
            incomeField.text = root.model.incomeText;
        if (needsField.text !== root.model.needsText)
            needsField.text = root.model.needsText;
        if (wantsField.text !== root.model.wantsText)
            wantsField.text = root.model.wantsText;
        if (savingField.text !== root.model.savingText)
            savingField.text = root.model.savingText;
        if (bufferSplitField.text !== root.model.bufferSplitText)
            bufferSplitField.text = root.model.bufferSplitText;
        if (investingSplitField.text !== root.model.investingSplitText)
            investingSplitField.text = root.model.investingSplitText;
        if (amortisationSplitField.text !== root.model.amortisationSplitText)
            amortisationSplitField.text = root.model.amortisationSplitText;
    }

    Connections {
        target: root.model

        function onChanged() {
            root.syncFields();
        }
    }

    spacing: Theme.spaceMd

    SectionHeader {
        Layout.fillWidth: true
        title: qsTr("This month")
        note: root.model.editingMonthLabel
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spaceMd
        rowSpacing: Theme.spaceSm

        FormRow {
            label: qsTr("Income after tax")
            hint: root.model.currencyCode

            MoneyField {
                id: incomeField

                text: root.model.incomeText
                placeholder: qsTr("What came in")
                unit: root.model.currencyCode
                onEdited: (value) => root.model.incomeText = value
            }
        }

        FormRow {
            label: qsTr("Needs")
            hint: root.model.currencyCode

            MoneyField {
                id: needsField

                text: root.model.needsText
                placeholder: qsTr("Rent, food, transport")
                unit: root.model.currencyCode
                onEdited: (value) => root.model.needsText = value
            }
        }

        FormRow {
            label: qsTr("Wants")
            hint: root.model.currencyCode

            MoneyField {
                id: wantsField

                text: root.model.wantsText
                placeholder: qsTr("Everything else you choose")
                unit: root.model.currencyCode
                onEdited: (value) => root.model.wantsText = value
            }
        }

        FormRow {
            label: qsTr("Saving")
            hint: root.model.currencyCode

            MoneyField {
                id: savingField

                text: root.model.savingText
                placeholder: qsTr("Paid to yourself, amortisation included")
                unit: root.model.currencyCode
                onEdited: (value) => root.model.savingText = value
            }
        }
    }

    // ---------------------------------------------------------------
    // Where the saving went, when the household knows
    // ---------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Text {
            text: qsTr("Where the saving went")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("Optional. If you split it, all three have to add up "
                 + "to the saving total; leave all three empty if you only know "
                 + "the total.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            FormRow {
                Layout.fillWidth: true
                label: qsTr("To the buffer")

                MoneyField {
                    id: bufferSplitField

                    text: root.model.bufferSplitText
                    placeholder: qsTr("Optional")
                    unit: root.model.currencyCode
                    onEdited: (value) => root.model.bufferSplitText = value
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("To investments")

                MoneyField {
                    id: investingSplitField

                    text: root.model.investingSplitText
                    placeholder: qsTr("Optional")
                    unit: root.model.currencyCode
                    onEdited: (value) => root.model.investingSplitText = value
                }
            }

            FormRow {
                Layout.fillWidth: true
                label: qsTr("To debt amortisation")

                MoneyField {
                    id: amortisationSplitField

                    text: root.model.amortisationSplitText
                    placeholder: qsTr("Optional")
                    unit: root.model.currencyCode
                    onEdited: (value) => root.model.amortisationSplitText = value
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            PrimaryButton {
                text: qsTr("Fill the rest into investments")
                filled: false
                enabled: root.model.savingSplitCanFill
                onClicked: root.model.fillSavingSplitRemainder()
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Text {
            visible: root.model.savingSplitError.length > 0
            text: root.model.savingSplitError
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Debt amortisation counts as saving in the model: "
                 + "paying down a loan is money kept, not money spent.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    Text {
        visible: text.length > 0
        text: root.model.savingNote
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    Text {
        visible: text.length > 0
        text: root.model.saveError
        textFormat: Text.PlainText
        color: Theme.danger
        font.pixelSize: Theme.fontCaption
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
}
