// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The first run: five screens that end with a household the dashboard can
// describe. It replaces the shell rather than living inside it, because every
// other page needs the profile this one creates.
//
// The wizard model owns the answers and every reading of them. This file lays
// fields out and moves between steps; it never converts an amount.
Item {
    id: page

    readonly property var stepTitles: [
        qsTr("Which currency do you think in?"),
        qsTr("Where are you on the way?"),
        qsTr("What comes in, and what goes out?"),
        qsTr("What do you own today?"),
        qsTr("Does this look right?")
    ]

    readonly property var stepNotes: [
        qsTr("Every figure in Hinkwise is in this currency. It travels with each "
             + "amount, so nothing has to be guessed later."),
        qsTr("The phase decides the sizing table, not the size of your income, and "
             + "the way you keep the record decides how an old month is read. Both "
             + "can change whenever your situation does."),
        qsTr("The model sizes every bucket from what you spend. Needs and wants "
             + "are the two halves of that spending."),
        qsTr("A name, what kind of holding it is and what it is worth. The bucket "
             + "follows from the kind, which is the model's answer rather than a choice."),
        qsTr("This is what will be created. Nothing is written until you press "
             + "the button.")
    ]

    WizardViewModel {
        id: wizard
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 720
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spaceMd

            // Where you are, and how much is left.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceMd

                Text {
                    text: qsTr("Hinkwise")
                    color: Theme.textPrimary
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 4

                    Repeater {
                        model: wizard.stepCount

                        delegate: Rectangle {
                            required property int index

                            implicitWidth: index === wizard.step ? 22 : 8
                            implicitHeight: 4
                            radius: 2
                            color: index <= wizard.step ? Theme.textPrimary : Theme.border
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    text: qsTr("Step %1 of %2").arg(wizard.step + 1).arg(wizard.stepCount)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs

                Text {
                    text: page.stepTitles[wizard.step]
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.Light
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: page.stepNotes[wizard.step]
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: Theme.spaceSm
                currentIndex: wizard.step

                // 1. Currency
                ScrollView {
                    id: currencyScroll

                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: currencyScroll.availableWidth
                        spacing: Theme.spaceSm

                        Repeater {
                            model: wizard.currencyChoices

                            delegate: RadioButton {
                                id: currencyCard

                                required property var modelData

                                Layout.fillWidth: true
                                padding: Theme.spaceMd
                                checked: modelData.index === wizard.currencyIndex
                                onClicked: wizard.currencyIndex = modelData.index

                                indicator: Item {}

                                background: Rectangle {
                                    radius: Theme.radius
                                    color: currencyCard.checked ? Theme.surfaceRaised
                                                                : Theme.surface
                                    border.width: 1
                                    border.color: currencyCard.checked
                                                  || currencyCard.visualFocus ? Theme.textSecondary
                                                                              : Theme.border
                                }

                                contentItem: RowLayout {
                                    spacing: Theme.spaceMd

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: currencyCard.modelData.code
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            font.weight: Font.DemiBold
                                        }

                                        Text {
                                            text: currencyCard.modelData.name
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                        }
                                    }

                                    Rectangle {
                                        implicitWidth: 16
                                        implicitHeight: 16
                                        radius: 8
                                        color: "transparent"
                                        border.width: 1
                                        border.color: currencyCard.checked ? Theme.textPrimary
                                                                          : Theme.border

                                        Rectangle {
                                            anchors.centerIn: parent
                                            visible: currencyCard.checked
                                            implicitWidth: 8
                                            implicitHeight: 8
                                            radius: 4
                                            color: Theme.textPrimary
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 2. Phase
                ScrollView {
                    id: phaseScroll

                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: phaseScroll.availableWidth
                        spacing: Theme.spaceSm

                        Repeater {
                            model: wizard.phaseChoices

                            delegate: RadioButton {
                                id: phaseCard

                                required property var modelData

                                Layout.fillWidth: true
                                padding: Theme.spaceMd
                                checked: modelData.index === wizard.phaseIndex
                                onClicked: wizard.phaseIndex = modelData.index

                                indicator: Item {}

                                background: Rectangle {
                                    radius: Theme.radius
                                    color: phaseCard.checked ? Theme.surfaceRaised : Theme.surface
                                    border.width: 1
                                    border.color: phaseCard.checked
                                                  || phaseCard.visualFocus ? Theme.textSecondary
                                                                           : Theme.border
                                }

                                contentItem: RowLayout {
                                    spacing: Theme.spaceMd

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: phaseCard.modelData.label
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            font.weight: Font.DemiBold
                                        }

                                        Text {
                                            text: phaseCard.modelData.description
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    Rectangle {
                                        implicitWidth: 16
                                        implicitHeight: 16
                                        radius: 8
                                        color: "transparent"
                                        border.width: 1
                                        border.color: phaseCard.checked ? Theme.textPrimary
                                                                       : Theme.border

                                        Rectangle {
                                            anchors.centerIn: parent
                                            visible: phaseCard.checked
                                            implicitWidth: 8
                                            implicitHeight: 8
                                            radius: 4
                                            color: Theme.textPrimary
                                        }
                                    }
                                }
                            }
                        }

                        // The same step asks the second question, because the
                        // two are the same kind of statement: how the household
                        // lives, not what it has. A sixth screen would make a
                        // five-step setup six for a choice with a good default.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceMd
                            implicitHeight: 1
                            color: Theme.border
                        }

                        Text {
                            text: qsTr("How you use hinkwise")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                            Layout.fillWidth: true
                        }

                        Text {
                            text: qsTr("Both keep the same record. The choice decides how "
                                 + "the application reads a month you have not written down "
                                 + "yet, and you can change it later in settings.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: wizard.trackingModeChoices

                            delegate: ChoiceCard {
                                id: trackingCard

                                required property var modelData

                                label: trackingCard.modelData.label
                                description: trackingCard.modelData.description
                                checked: trackingCard.modelData.index === wizard.trackingModeIndex
                                onClicked: wizard.trackingModeIndex = trackingCard.modelData.index
                            }
                        }
                    }
                }

                // 3. Income and spending
                ScrollView {
                    id: incomeScroll

                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: incomeScroll.availableWidth
                        spacing: Theme.spaceMd

                        FormRow {
                            label: qsTr("Monthly income after tax")
                            hint: wizard.currencyCode

                            MoneyField {
                                text: wizard.incomeText
                                placeholder: qsTr("30 000")
                                unit: wizard.currencyCode
                                error: wizard.incomeError
                                onEdited: (value) => wizard.incomeText = value
                            }
                        }

                        FormRow {
                            label: qsTr("Needs each month")
                            hint: wizard.currencyCode

                            MoneyField {
                                text: wizard.needsText
                                placeholder: qsTr("Rent, food, transport")
                                unit: wizard.currencyCode
                                error: wizard.needsError
                                onEdited: (value) => wizard.needsText = value
                            }
                        }

                        FormRow {
                            label: qsTr("Wants each month")
                            hint: wizard.currencyCode

                            MoneyField {
                                text: wizard.wantsText
                                placeholder: qsTr("Everything else you choose")
                                unit: wizard.currencyCode
                                error: wizard.wantsError
                                onEdited: (value) => wizard.wantsText = value
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: saving.implicitHeight + 2 * Theme.spaceMd
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                id: saving

                                anchors.fill: parent
                                anchors.margins: Theme.spaceMd
                                spacing: Theme.spaceXs

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceMd

                                    Text {
                                        text: qsTr("Saving")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontCaption
                                        font.capitalization: Font.AllUppercase
                                        font.letterSpacing: 0.6
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: wizard.savingLabel
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontTitle
                                        font.weight: Font.Light
                                    }
                                }

                                Text {
                                    text: wizard.savingNote
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                // 4. Accounts
                ScrollView {
                    id: accountScroll

                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: accountScroll.availableWidth
                        spacing: Theme.spaceSm

                        Repeater {
                            model: wizard.accounts

                            delegate: Rectangle {
                                id: accountCard

                                required property int index
                                required property string name
                                required property int kindIndex
                                required property string bucketLabel
                                required property string balanceText
                                required property string balanceError

                                Layout.fillWidth: true
                                implicitHeight: cardContent.implicitHeight + 2 * Theme.spaceMd
                                radius: Theme.radius
                                color: Theme.surface
                                border.color: Theme.border
                                border.width: 1

                                ColumnLayout {
                                    id: cardContent

                                    anchors.fill: parent
                                    anchors.margins: Theme.spaceMd
                                    spacing: Theme.spaceSm

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spaceSm

                                        FormRow {
                                            Layout.fillWidth: true
                                            label: qsTr("Name")

                                            TextField {
                                                Layout.fillWidth: true
                                                text: accountCard.name
                                                color: Theme.textPrimary
                                                font.pixelSize: Theme.fontBody
                                                selectionColor: Theme.buffer
                                                selectedTextColor: Theme.background
                                                onTextEdited: wizard.accounts.setName(accountCard.index,
                                                                                      text)

                                                background: Rectangle {
                                                    implicitHeight: 34
                                                    radius: Theme.radiusSmall
                                                    color: Theme.background
                                                    border.width: 1
                                                    border.color: parent.activeFocus ? Theme.textSecondary
                                                                                     : Theme.border
                                                }
                                            }
                                        }

                                        FormRow {
                                            Layout.fillWidth: true
                                            label: qsTr("Kind")

                                            ChoiceField {
                                                Layout.fillWidth: true
                                                model: wizard.assetKindLabels
                                                currentIndex: accountCard.kindIndex
                                                onActivated: (index) => wizard.accounts.setKind(
                                                                 accountCard.index, index)
                                            }
                                        }

                                        FormRow {
                                            Layout.fillWidth: true
                                            label: qsTr("Balance")
                                            hint: wizard.currencyCode

                                            MoneyField {
                                                text: accountCard.balanceText
                                                placeholder: qsTr("0")
                                                unit: wizard.currencyCode
                                                error: accountCard.balanceError
                                                onEdited: (value) => wizard.accounts.setBalanceText(
                                                               accountCard.index, value)
                                            }
                                        }

                                        PrimaryButton {
                                            Layout.alignment: Qt.AlignBottom
                                            text: qsTr("Remove")
                                            filled: false
                                            onClicked: wizard.accounts.removeRow(accountCard.index)
                                        }
                                    }

                                    Text {
                                        text: qsTr("Goes to %1").arg(accountCard.bucketLabel)
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontCaption
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        PrimaryButton {
                            Layout.topMargin: Theme.spaceXs
                            text: qsTr("Add another account")
                            filled: false
                            onClicked: wizard.accounts.addRow()
                        }
                    }
                }

                // 5. Review
                ScrollView {
                    id: reviewScroll

                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: reviewScroll.availableWidth
                        spacing: Theme.spaceMd

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: summary.implicitHeight + 2 * Theme.spaceLg
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                            border.width: 1

                            ColumnLayout {
                                id: summary

                                anchors.fill: parent
                                anchors.margins: Theme.spaceLg
                                spacing: Theme.spaceSm

                                Repeater {
                                    model: wizard.reviewLines

                                    delegate: RowLayout {
                                        id: reviewLine

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: Theme.spaceMd

                                        Text {
                                            text: reviewLine.modelData.label
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontCaption
                                            font.capitalization: Font.AllUppercase
                                            font.letterSpacing: 0.6
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: reviewLine.modelData.value
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceXs

                            SectionHeader {
                                Layout.fillWidth: true
                                title: qsTr("Accounts")
                                note: wizard.accounts.count === 1
                                      ? qsTr("1 account")
                                      : qsTr("%1 accounts").arg(wizard.accounts.count)
                            }

                            Repeater {
                                model: wizard.accounts

                                delegate: RowLayout {
                                    id: reviewAccount

                                    required property int index
                                    required property string name
                                    required property string kindLabel
                                    required property string bucketLabel
                                    required property string balanceLabel

                                    Layout.fillWidth: true
                                    spacing: Theme.spaceMd

                                    Text {
                                        text: reviewAccount.name
                                        textFormat: Text.PlainText
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: reviewAccount.kindLabel
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }

                                    Text {
                                        text: reviewAccount.bucketLabel
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }

                                    Text {
                                        text: reviewAccount.balanceLabel
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                    }
                                }
                            }
                        }

                        Text {
                            visible: wizard.createError.length > 0
                            text: wizard.createError
                            textFormat: Text.PlainText
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // Back, next, and the one sentence saying what stops you.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceSm
                spacing: Theme.spaceMd

                PrimaryButton {
                    text: qsTr("Back")
                    filled: false
                    enabled: wizard.canGoBack
                    onClicked: wizard.back()
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    visible: text.length > 0
                    text: wizard.blockingReason
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    Layout.maximumWidth: 380
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.WordWrap
                }

                PrimaryButton {
                    text: wizard.isReview ? qsTr("Create my economy") : qsTr("Next")
                    enabled: wizard.isReview ? wizard.canCreate : wizard.canGoNext
                    onClicked: {
                        if (wizard.isReview)
                            wizard.createEconomy();
                        else
                            wizard.next();
                    }
                }
            }
        }
    }
}
