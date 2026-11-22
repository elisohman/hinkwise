// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The year-end review: the model's annual ritual in four questions, ending in
// the transfers domain::planRebalance() suggests.
//
// It is a page inside the plan page rather than an item in the rail, because a
// review happens once a year. Nothing on it moves money: every suggestion is
// advice, and the last card says so in as many words.
Item {
    id: page

    signal back()
    signal navigateRequested(int index)

    // A numbered question. The number is part of the ritual's order, not a
    // priority: each step reads the one before it.
    component StepHeader: RowLayout {
        id: header

        property int number: 1
        property string title: ""

        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Rectangle {
            implicitWidth: 22
            implicitHeight: 22
            radius: 11
            color: Theme.surfaceRaised
            border.color: Theme.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: header.number
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
            }
        }

        Text {
            text: header.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Light
            Layout.fillWidth: true
        }
    }

    // A switch whose state is the whole point of the sentence above it. The
    // same action is reachable from the keyboard, because a rule about not
    // selling into a fall is worth being able to turn on without a mouse.
    component ToggleRow: RowLayout {
        id: toggle

        property bool checked: false

        signal toggled()

        Layout.fillWidth: true
        spacing: Theme.spaceSm
        activeFocusOnTab: true

        Keys.onSpacePressed: toggle.toggled()
        Keys.onReturnPressed: toggle.toggled()

        Rectangle {
            implicitWidth: 38
            implicitHeight: 22
            radius: 11
            color: toggle.checked ? Theme.textPrimary : Theme.surfaceRaised
            border.color: toggle.activeFocus ? Theme.textSecondary : Theme.border
            border.width: 1

            Rectangle {
                x: toggle.checked ? parent.width - width - 2 : 2
                y: 2
                implicitWidth: 18
                implicitHeight: 18
                radius: 9
                color: toggle.checked ? Theme.background : Theme.textMuted
            }
        }

        Text {
            text: qsTr("The market is down right now")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
        }

        TapHandler {
            cursorShape: Qt.PointingHandCursor
            onTapped: {
                toggle.forceActiveFocus();
                toggle.toggled();
            }
        }
    }

    ReviewViewModel {
        id: review
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            PrimaryButton {
                text: qsTr("Back to the plan")
                filled: false
                onClicked: page.back()
            }

            Text {
                text: qsTr("Year-end review")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            Item {
                Layout.fillWidth: true
            }
        }

        ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.spaceLg

                Text {
                    text: qsTr("Four questions, once a year. Check what changed, check the goals, "
                         + "check the buckets, and settle on what to do next.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                // -------------------------------------------------------
                // 1. Situation
                // -------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: situationCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: situationCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        StepHeader {
                            number: 1
                            title: qsTr("Has your situation changed?")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

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
                                    text: review.phaseLabel
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontTitle
                                    font.weight: Font.Light
                                }

                                Text {
                                    text: review.phaseNote
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }

                            PrimaryButton {
                                text: qsTr("Settings")
                                filled: false
                                Layout.alignment: Qt.AlignTop
                                onClicked: page.navigateRequested(4)
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
                                label: qsTr("Monthly income")
                                value: review.incomeLabel
                                note: qsTr("Before needs and wants")
                            }

                            StatBlock {
                                Layout.fillWidth: true
                                label: qsTr("Monthly expenses")
                                value: review.expensesLabel
                                note: qsTr("Needs and wants together")
                            }

                            PrimaryButton {
                                text: qsTr("Cashflow")
                                filled: false
                                Layout.alignment: Qt.AlignTop
                                onClicked: page.navigateRequested(2)
                            }
                        }

                        Text {
                            text: review.flowNote
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -------------------------------------------------------
                // 2. Goals
                // -------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: goalsCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: goalsCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        StepHeader {
                            number: 2
                            title: qsTr("Have your goals changed?")
                        }

                        Text {
                            text: qsTr("A large purchase inside three years is money the buffer "
                                 + "has to hold, so the model adds it to the buffer target.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: review.goals.count === 0
                            text: qsTr("No planned purchases. Add a roof, a car or a sabbatical "
                                 + "below, and the buffer target moves with it.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: review.goals

                            delegate: RowLayout {
                                id: goalRow

                                required property string goalId
                                required property string name
                                required property string amountLabel
                                required property string dueLabel
                                required property bool counted

                                Layout.fillWidth: true
                                spacing: Theme.spaceMd

                                Text {
                                    text: goalRow.name
                                    textFormat: Text.PlainText
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: goalRow.dueLabel
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                }

                                Text {
                                    text: goalRow.amountLabel
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                }

                                Text {
                                    text: goalRow.counted ? qsTr("in the target") : qsTr("later")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    Layout.preferredWidth: 74
                                }

                                PrimaryButton {
                                    text: qsTr("Remove")
                                    filled: false
                                    onClicked: review.removeGoal(goalRow.goalId)
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spaceMd
                            rowSpacing: Theme.spaceSm

                            FormRow {
                                label: qsTr("What")
                                hint: qsTr("A name you will recognise")

                                TextField {
                                    id: goalName

                                    Layout.fillWidth: true
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    selectionColor: Theme.buffer
                                    selectedTextColor: Theme.background

                                    background: Rectangle {
                                        implicitHeight: 34
                                        radius: Theme.radiusSmall
                                        color: Theme.background
                                        border.width: 1
                                        border.color: goalName.activeFocus ? Theme.textSecondary
                                                                           : Theme.border
                                    }
                                }
                            }

                            FormRow {
                                label: qsTr("Amount")
                                hint: review.currencyCode

                                MoneyField {
                                    id: goalAmount

                                    placeholder: qsTr("What it will cost")
                                    unit: review.currencyCode
                                }
                            }

                            FormRow {
                                label: qsTr("Due month")

                                ChoiceField {
                                    id: goalMonth

                                    Layout.fillWidth: true
                                    model: review.monthLabels
                                    currentIndex: review.dueMonthDefault
                                }
                            }

                            FormRow {
                                label: qsTr("Due year")

                                ChoiceField {
                                    id: goalYear

                                    Layout.fillWidth: true
                                    model: review.dueYearLabels
                                    currentIndex: review.dueYearDefault
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            PrimaryButton {
                                text: qsTr("Add purchase")
                                onClicked: {
                                    goalError.text = review.addGoal(goalName.text,
                                                                    goalAmount.text,
                                                                    goalMonth.currentIndex,
                                                                    goalYear.currentIndex);
                                    if (goalError.text.length === 0) {
                                        goalName.text = "";
                                        goalAmount.text = "";
                                    }
                                }
                            }

                            Text {
                                id: goalError

                                textFormat: Text.PlainText
                                color: Theme.danger
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
                                label: qsTr("Buffer target today")
                                value: review.bufferTargetNowLabel
                                note: qsTr("As the dashboard sizes it")
                            }

                            StatBlock {
                                Layout.fillWidth: true
                                label: qsTr("With these purchases")
                                value: review.bufferTargetWithGoalsLabel
                                note: review.lumpSumLabel
                            }
                        }

                        Text {
                            text: review.goalsEffectLabel
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: review.goalsBeyondThreeYears
                            text: qsTr("Purchases due later than three years do not change the "
                                 + "buffer: they are too far away to hold cash for.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -------------------------------------------------------
                // 3. Buckets
                // -------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: bucketsCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: bucketsCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        StepHeader {
                            number: 3
                            title: qsTr("Does a bucket need topping up?")
                        }

                        Text {
                            text: qsTr("Each bucket against the band the model gives it. The dot "
                                 + "is the balance; anything outside the tinted stretch is what the "
                                 + "proposal below reacts to.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spaceMd
                            rowSpacing: Theme.spaceMd

                            Repeater {
                                model: review.buckets

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
                    }
                }

                // -------------------------------------------------------
                // 4. The proposal
                // -------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: proposalCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: proposalCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceMd

                        StepHeader {
                            number: 4
                            title: qsTr("What would the model do next?")
                        }

                        Text {
                            text: qsTr("You do not sell growth into a fall. The buffer holds "
                                 + "years of spending so that a bad market never forces a sale; "
                                 + "when growth is down, a refill comes from Preserve instead, even "
                                 + "if Preserve is below its own band.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        ToggleRow {
                            checked: review.marketDown
                            onToggled: review.marketDown = !review.marketDown
                        }

                        Text {
                            visible: !review.marketDownApplies
                            text: qsTr("In the accumulating phases nothing is bought from or "
                                 + "sold out of Growth, so this switch changes nothing yet. It "
                                 + "matters once the buckets are drawn down.")
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

                        Text {
                            text: review.rationaleLabel
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: review.proposalEmpty
                            text: qsTr("No transfer is suggested this month: nothing is far "
                                 + "enough outside its band to be worth moving.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: review.transfers

                            delegate: RowLayout {
                                id: transferRow

                                required property string fromLabel
                                required property string toLabel
                                required property string fromColourKey
                                required property string toColourKey
                                required property string amountLabel

                                Layout.fillWidth: true
                                spacing: Theme.spaceSm

                                Rectangle {
                                    implicitWidth: 8
                                    implicitHeight: 8
                                    radius: 4
                                    color: Theme.bucketColour(transferRow.fromColourKey)
                                }

                                Text {
                                    text: transferRow.fromLabel
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                }

                                Text {
                                    text: qsTr("\u2192")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontBody
                                }

                                Rectangle {
                                    implicitWidth: 8
                                    implicitHeight: 8
                                    radius: 4
                                    color: Theme.bucketColour(transferRow.toColourKey)
                                }

                                Text {
                                    text: transferRow.toLabel
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: transferRow.amountLabel
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                text: qsTr("Next contribution goes to")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.6
                            }

                            Text {
                                text: review.nextContributionLabel
                                color: review.hasNextContribution ? Theme.textPrimary
                                                                  : Theme.textMuted
                                font.pixelSize: Theme.fontBody
                                font.weight: review.hasNextContribution ? Font.DemiBold
                                                                        : Font.Normal
                                Layout.fillWidth: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            implicitHeight: moneyNote.implicitHeight + 2 * Theme.spaceMd
                            radius: Theme.radiusSmall
                            color: Theme.background
                            border.color: Theme.border
                            border.width: 1

                            Text {
                                id: moneyNote

                                anchors.fill: parent
                                anchors.margins: Theme.spaceMd
                                text: qsTr("Advice, not an action. Hinkwise never touches money: "
                                     + "nothing moves automatically, and the transfers above are "
                                     + "yours to make or ignore.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
