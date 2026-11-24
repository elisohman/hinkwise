// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// Where the household's holdings live: the four buckets on the left, the
// accounts inside the selected one on the right, and the debts underneath the
// buckets because they reduce the headline figure instead of filling anything.
//
// Two ways to record a balance, and they are different sizes of the same act.
// The update action at the top opens the flow that records every account and
// every debt for a month; the button under the bucket records the accounts of
// the bucket on screen, which is the shortcut for a household that is already
// looking at the right one.
//
// Every write goes through the account model, which goes through the
// AppController and its use cases. Amounts are read in C++; this file only
// passes the text that was typed.
Item {
    id: page

    /// What the update action is called in the tracking mode in force. Main
    /// passes it down from the dashboard, which is the one place that knows
    /// which mode the household chose.
    required property string updateActionLabel

    signal updateRequested()

    // What the delete confirmation is about. The id is empty when nothing is
    // waiting, which is also how the dialogs know they are closed.
    property string pendingAccountId: ""
    property string pendingAccountName: ""
    property string pendingDebtId: ""
    property string pendingDebtName: ""
    /// The balance the edit dialog shows beside the terms it can change. The
    /// balance itself is recorded in the update flow and is never rewritten
    /// here.
    property string pendingDebtBalanceLabel: ""
    /// A mutation that failed outside a dialog, shown under the account list.
    property string pageError: ""

    function openAccountDialog(accountId, name, kindIndex, bucketIndex) {
        page.pendingAccountId = accountId;
        accountName.text = name;
        accountKind.currentIndex = kindIndex;
        accountBucket.currentIndex = bucketIndex;
        accountError.text = "";
        accountDialog.open();
        accountName.forceActiveFocus();
    }

    function openDebtDialog(debtId, name, kindIndex, rateText, styleIndex, paymentText, feeText,
                            termYearsText, balanceLabel) {
        page.pendingDebtId = debtId;
        page.pendingDebtName = name;
        page.pendingDebtBalanceLabel = balanceLabel;
        debtName.text = name;
        debtKind.currentIndex = kindIndex;
        debtRate.text = rateText;
        debtBalance.text = "";
        debtPayment.text = paymentText;
        debtFee.text = feeText;
        debtTermYears.text = termYearsText;
        debtStyleGroup.checkedButton = styleIndex === accounts.annuityStyleIndex
                ? debtStyleAnnuity : debtStyleFixed;
        debtError.text = "";
        debtDialog.open();
        debtName.forceActiveFocus();
    }

    // A fixed loan has neither an administration fee nor a term: both belong to
    // an annuity. Clearing them the moment the style changes is what stops a
    // figure typed under one style reaching the database under the other, and
    // the save handler passes them as empty as well.
    function clearFixedAmortisationFields() {
        if (!debtStyleAnnuity.checked) {
            debtFee.text = "";
            debtTermYears.text = "";
        }
    }

    // A row of the "record this month's balances" form. It is a named type so
    // that the save handler below can read the field it holds without casting
    // an Item to whatever happens to be inside it.
    component BalanceRow: RowLayout {
        id: balanceRow

        required property string accountId
        required property string name

        property alias fieldText: monthField.text

        Layout.fillWidth: true
        spacing: Theme.spaceMd

        Text {
            text: balanceRow.name
            textFormat: Text.PlainText
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        MoneyField {
            id: monthField

            Layout.preferredWidth: 160
            unit: accounts.currencyCode
            placeholder: qsTr("0")
            text: accounts.latestBalanceText(balanceRow.accountId)
        }
    }

    function openValuationDialog(accountId, name) {
        page.pendingAccountId = accountId;
        valuationTitle.text = qsTr("Value of %1").arg(name);
        valuationField.text = accounts.latestBalanceText(accountId);
        valuationError.text = "";
        valuationDialog.open();
    }

    AccountsViewModel {
        id: accounts
    }

    // The two repayment styles are one choice, so the dialog's radios share a
    // group rather than relying on whichever parent they happen to sit in.
    ButtonGroup {
        id: debtStyleGroup
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        // The page's own name, and the one action that is about the page as a
        // whole rather than about the bucket in view.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: qsTr("Accounts & buckets")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            PrimaryButton {
                text: page.updateActionLabel
                onClicked: page.updateRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceLg

            // ---------------------------------------------------------------
            // Left: the four buckets, then the debts
            // ---------------------------------------------------------------
            ScrollView {
                id: bucketScroll

                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: bucketScroll.availableWidth
                    spacing: Theme.spaceXs

                    SectionHeader {
                        Layout.fillWidth: true
                        title: qsTr("Buckets")
                    }

                    Repeater {
                        model: accounts.buckets

                        delegate: Item {
                            id: bucketRow

                            required property int index
                            required property string label
                            required property string balanceLabel
                            required property string targetLabel
                            required property string colourKey
                            required property real bandStart
                            required property real bandEnd
                            required property real balanceMarker
                            required property real capMarker
                            required property bool hasTarget
                            required property bool hasCap

                            readonly property bool selected: bucketRow.index === accounts.selectedBucket

                            Layout.fillWidth: true
                            implicitHeight: bucketContent.implicitHeight + 2 * Theme.spaceSm

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusSmall
                                color: bucketRow.selected ? Theme.surfaceRaised
                                                          : bucketPointer.containsMouse
                                                            ? Theme.surface : "transparent"
                            }

                            ColumnLayout {
                                id: bucketContent

                                anchors.fill: parent
                                anchors.margins: Theme.spaceSm
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceSm

                                    Rectangle {
                                        implicitWidth: 6
                                        implicitHeight: 6
                                        radius: 3
                                        color: Theme.bucketColour(bucketRow.colourKey)
                                    }

                                    Text {
                                        text: bucketRow.label
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        font.weight: bucketRow.selected ? Font.DemiBold : Font.Normal
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: bucketRow.balanceLabel
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }

                                BucketGauge {
                                    Layout.fillWidth: true
                                    visible: bucketRow.hasTarget || bucketRow.hasCap
                                    showBand: bucketRow.hasTarget
                                    showCap: bucketRow.hasCap
                                    bandStart: bucketRow.bandStart
                                    bandEnd: bucketRow.bandEnd
                                    balanceMarker: bucketRow.balanceMarker
                                    capMarker: bucketRow.capMarker
                                    accent: Theme.bucketColour(bucketRow.colourKey)
                                }

                                Text {
                                    text: bucketRow.targetLabel
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: bucketPointer

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: accounts.selectedBucket = bucketRow.index
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceMd
                        Layout.bottomMargin: Theme.spaceXs
                        implicitHeight: 1
                        color: Theme.border
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Text {
                            text: qsTr("Debts")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            Layout.fillWidth: true
                        }

                        PrimaryButton {
                            text: qsTr("Add debt")
                            filled: false
                            onClicked: page.openDebtDialog("", "", 0, "",
                                                          accounts.fixedAmortisationStyleIndex,
                                                          "", "", "", "")
                        }
                    }

                    Repeater {
                        model: accounts.debts

                        delegate: ColumnLayout {
                            id: debtRow

                            required property string debtId
                            required property string name
                            required property int kindIndex
                            required property string kindLabel
                            required property string rateLabel
                            required property int styleIndex
                            required property string rateText
                            required property string paymentText
                            required property string feeText
                            required property string termYearsText
                            required property string balanceLabel

                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm

                                Text {
                                    text: debtRow.name
                                    textFormat: Text.PlainText
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: debtRow.balanceLabel
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 · %2").arg(debtRow.kindLabel, debtRow.rateLabel)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm

                                Item {
                                    Layout.fillWidth: true
                                }

                                PrimaryButton {
                                    text: qsTr("Edit")
                                    filled: false
                                    onClicked: page.openDebtDialog(debtRow.debtId, debtRow.name,
                                                                  debtRow.kindIndex,
                                                                  debtRow.rateText,
                                                                  debtRow.styleIndex,
                                                                  debtRow.paymentText,
                                                                  debtRow.feeText,
                                                                  debtRow.termYearsText,
                                                                  debtRow.balanceLabel)
                                }

                                PrimaryButton {
                                    text: qsTr("Remove")
                                    filled: false
                                    onClicked: {
                                        page.pendingDebtId = debtRow.debtId;
                                        page.pendingDebtName = debtRow.name;
                                        confirmDebtDialog.open();
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: accounts.debts.count === 0
                        text: qsTr("No debts recorded. A mortgage or a loan reduces the "
                             + "headline figure, so enter them here.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceXs
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ---------------------------------------------------------------
            // Right: the accounts of the selected bucket
            // ---------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: accounts.selectedBucketLabel
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Light
                    }

                    Text {
                        text: accounts.bucketBalanceLabel
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    PrimaryButton {
                        text: qsTr("Add account")
                        onClicked: page.openAccountDialog("", "",
                                                          accounts.firstKindForBucket(
                                                              accounts.selectedBucket),
                                                          accounts.selectedBucket)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Text {
                        text: accounts.bucketTargetLabel
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Text {
                        text: accounts.monthLabel
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                    }
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceXs
                    text: qsTr("Record this bucket's balances")
                    filled: false
                    enabled: accounts.accounts.count > 0
                    onClicked: {
                        monthError.text = "";
                        monthDialog.open();
                    }
                }

                ScrollView {
                    id: accountScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spaceSm
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: accountScroll.availableWidth
                        spacing: Theme.spaceSm

                        Text {
                            visible: accounts.accounts.count === 0
                            text: qsTr("Nothing in this bucket yet. Add an account, or pick "
                                 + "another bucket on the left.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: accounts.accounts

                            delegate: Rectangle {
                                id: accountCard

                                required property string accountId
                                required property string name
                                required property int kindIndex
                                required property string kindLabel
                                required property int bucketIndex
                                required property string bucketLabel
                                required property string balanceLabel
                                required property string monthLabel
                                required property bool hasMonthValue

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
                                    spacing: Theme.spaceXs

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spaceMd

                                        Text {
                                            text: accountCard.name
                                            textFormat: Text.PlainText
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: accountCard.balanceLabel
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                        }
                                    }

                                    Text {
                                        text: qsTr("%1 · %2 · this month: %3")
                                                  .arg(accountCard.kindLabel, accountCard.bucketLabel,
                                                       accountCard.monthLabel)
                                        color: accountCard.hasMonthValue ? Theme.textSecondary
                                                                         : Theme.textMuted
                                        font.pixelSize: Theme.fontCaption
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Theme.spaceXs
                                        spacing: Theme.spaceSm

                                        PrimaryButton {
                                            text: qsTr("Edit")
                                            filled: false
                                            onClicked: page.openAccountDialog(accountCard.accountId,
                                                                              accountCard.name,
                                                                              accountCard.kindIndex,
                                                                              accountCard.bucketIndex)
                                        }

                                        PrimaryButton {
                                            text: qsTr("Record value")
                                            filled: false
                                            onClicked: page.openValuationDialog(accountCard.accountId,
                                                                                 accountCard.name)
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        PrimaryButton {
                                            text: qsTr("Delete")
                                            filled: false
                                            destructive: true
                                            onClicked: {
                                                page.pendingAccountId = accountCard.accountId;
                                                page.pendingAccountName = accountCard.name;
                                                confirmAccountDialog.open();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: page.pageError.length > 0
                    text: page.pageError
                    textFormat: Text.PlainText
                    color: Theme.danger
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Add or edit an account
    // -------------------------------------------------------------------
    Popup {
        id: accountDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 2 * Theme.spaceLg : 440)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape

        readonly property bool editing: page.pendingAccountId.length > 0

        Overlay.modal: Rectangle {
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.72)
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: accountDialog.editing ? qsTr("Edit account") : qsTr("Add account")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            FormRow {
                label: qsTr("Name")

                TextField {
                    id: accountName

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
                        border.color: parent.activeFocus ? Theme.textSecondary : Theme.border
                    }
                }
            }

            FormRow {
                label: qsTr("Kind")
                hint: qsTr("Sets the bucket")

                ChoiceField {
                    id: accountKind

                    Layout.fillWidth: true
                    model: accounts.assetKindLabels
                    onActivated: (index) => accountBucket.currentIndex = accounts.defaultBucketForKind(
                                     index)
                }

                Text {
                    visible: accountKind.currentIndex === accounts.realEstateKindIndex
                    text: qsTr("Market value, not your equity. Record the mortgage "
                         + "separately as a debt.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: accountKind.currentIndex >= 0 && accountBucket.currentIndex >= 0
                          ? qsTr("A %1 usually goes in %2. Move it if your situation is "
                                 + "different - the bucket is what the model counts, and "
                                 + "this is yours to set.")
                                .arg(accounts.assetKindLabels[accountKind.currentIndex])
                                .arg(accounts.bucketLabels[accountBucket.currentIndex])
                          : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            FormRow {
                label: qsTr("Bucket")
                hint: qsTr("Override allowed")

                ChoiceField {
                    id: accountBucket

                    Layout.fillWidth: true
                    model: accounts.bucketLabels
                }
            }

            Text {
                id: accountError

                visible: text.length > 0
                textFormat: Text.PlainText
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceSm
                spacing: Theme.spaceSm

                Item {
                    Layout.fillWidth: true
                }

                PrimaryButton {
                    text: qsTr("Cancel")
                    filled: false
                    onClicked: accountDialog.close()
                }

                PrimaryButton {
                    text: accountDialog.editing ? qsTr("Save") : qsTr("Add")
                    onClicked: {
                        const failure = accountDialog.editing
                                ? accounts.updateAccount(page.pendingAccountId, accountName.text,
                                                         accountKind.currentIndex,
                                                         accountBucket.currentIndex)
                                : accounts.addAccount(accountName.text,
                                                      accountKind.currentIndex,
                                                      accountBucket.currentIndex);
                        if (failure.length > 0) {
                            accountError.text = failure;
                            return;
                        }
                        accountDialog.close();
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Record one account's value for this month
    // -------------------------------------------------------------------
    Popup {
        id: valuationDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, parent ? parent.width - 2 * Theme.spaceLg : 380)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape

        Overlay.modal: Rectangle {
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.72)
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                id: valuationTitle

                textFormat: Text.PlainText
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Recorded for %1.").arg(accounts.monthLabel)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
            }

            FormRow {
                label: qsTr("Balance")
                hint: accounts.currencyCode

                MoneyField {
                    id: valuationField

                    unit: accounts.currencyCode
                    placeholder: qsTr("0")
                }
            }

            Text {
                id: valuationError

                visible: text.length > 0
                textFormat: Text.PlainText
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Item {
                    Layout.fillWidth: true
                }

                PrimaryButton {
                    text: qsTr("Cancel")
                    filled: false
                    onClicked: valuationDialog.close()
                }

                PrimaryButton {
                    text: qsTr("Save")
                    onClicked: {
                        const failure = accounts.recordValuation(page.pendingAccountId,
                                                                 valuationField.text);
                        if (failure.length > 0) {
                            valuationError.text = failure;
                            return;
                        }
                        valuationDialog.close();
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Record every account's balance for this month at once
    // -------------------------------------------------------------------
    Popup {
        id: monthDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 2 * Theme.spaceLg : 480)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape

        Overlay.modal: Rectangle {
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.72)
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: qsTr("Balances for %1").arg(accounts.monthLabel)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("Every account in %1. Leave a field empty to skip it this month.")
                          .arg(accounts.selectedBucketLabel)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(280, monthList.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: monthList

                    width: parent.width
                    spacing: Theme.spaceSm

                    Repeater {
                        id: monthRepeater

                        model: accounts.accounts

                        delegate: BalanceRow {
                        }
                    }
                }
            }

            Text {
                id: monthError

                visible: text.length > 0
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Item {
                    Layout.fillWidth: true
                }

                PrimaryButton {
                    text: qsTr("Cancel")
                    filled: false
                    onClicked: monthDialog.close()
                }

                PrimaryButton {
                    text: qsTr("Save all")
                    onClicked: {
                        const balances = {};
                        for (let index = 0; index < monthRepeater.count; ++index) {
                            const row = monthRepeater.itemAt(index) as BalanceRow;
                            balances[row.accountId] = row.fieldText;
                        }
                        const failure = accounts.recordMonthBalances(balances);
                        if (failure.length > 0) {
                            monthError.text = failure;
                            return;
                        }
                        monthDialog.close();
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Add a debt
    // -------------------------------------------------------------------
    Popup {
        id: debtDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 2 * Theme.spaceLg : 480)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape

        readonly property bool editing: page.pendingDebtId.length > 0

        Overlay.modal: Rectangle {
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.72)
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: debtDialog.editing
                      ? qsTr("Edit %1").arg(page.pendingDebtName)
                      : qsTr("Add a debt")
                textFormat: Text.PlainText
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Text {
                text: qsTr("A debt reduces net worth. One priced above a typical "
                     + "mortgage is what the model calls expensive and asks you to "
                     + "pay down first.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // The form is taller than a 600 px window once both styles show
            // their own fields, so it scrolls while the title and the buttons
            // stay where they are.
            ScrollView {
                id: debtFormScroll

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(360, debtForm.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: debtForm

                    width: debtFormScroll.availableWidth
                    spacing: Theme.spaceMd

                    FormRow {
                        label: qsTr("Name")

                        TextField {
                            id: debtName

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
                                border.color: parent.activeFocus ? Theme.textSecondary
                                                                 : Theme.border
                            }
                        }
                    }

                    FormRow {
                        label: qsTr("Kind")

                        ChoiceField {
                            id: debtKind

                            Layout.fillWidth: true
                            model: accounts.liabilityKindLabels
                        }

                        Text {
                            visible: debtKind.currentIndex === accounts.mortgageKindIndex
                            text: qsTr("The outstanding loan, alongside the property at "
                                 + "its market value.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        FormRow {
                            label: qsTr("Interest rate")
                            hint: "%"

                            MoneyField {
                                id: debtRate

                                unit: "%"
                                placeholder: qsTr("4,5")
                            }
                        }

                        FormRow {
                            visible: !debtDialog.editing
                            label: qsTr("Current balance")
                            hint: accounts.currencyCode

                            MoneyField {
                                id: debtBalance

                                unit: accounts.currencyCode
                                placeholder: qsTr("0")
                            }
                        }
                    }

                    Text {
                        visible: debtDialog.editing
                        text: qsTr("Current balance: %1. It is recorded in the update "
                             + "flow, not here.").arg(page.pendingDebtBalanceLabel)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    FormRow {
                        label: qsTr("Repayment style")

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            ChoiceCard {
                                id: debtStyleFixed

                                Layout.fillWidth: true
                                ButtonGroup.group: debtStyleGroup
                                checked: true
                                label: qsTr("Fixed amortisation")
                                description: qsTr("You repay a set amount each month, "
                                                   + "interest on top.")
                                onCheckedChanged: {
                                    if (checked)
                                        page.clearFixedAmortisationFields();
                                }
                            }

                            ChoiceCard {
                                id: debtStyleAnnuity

                                Layout.fillWidth: true
                                ButtonGroup.group: debtStyleGroup
                                label: qsTr("Annuity")
                                description: qsTr("You pay a set amount each month that "
                                                   + "covers interest, fees and "
                                                   + "amortisation, so the split shifts "
                                                   + "over time.")
                            }
                        }
                    }

                    FormRow {
                        label: debtStyleAnnuity.checked ? qsTr("Monthly payment")
                                                        : qsTr("Monthly amortisation")
                        hint: accounts.currencyCode

                        MoneyField {
                            id: debtPayment

                            unit: accounts.currencyCode
                            placeholder: qsTr("0")
                        }
                    }

                    FormRow {
                        visible: debtStyleAnnuity.checked
                        label: qsTr("Monthly fee")
                        hint: accounts.currencyCode

                        MoneyField {
                            id: debtFee

                            unit: accounts.currencyCode
                            placeholder: qsTr("0")
                        }

                        Text {
                            text: qsTr("Administration fee, if your lender charges one.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    FormRow {
                        visible: debtStyleAnnuity.checked
                        label: qsTr("Term")
                        hint: qsTr("Years")

                        MoneyField {
                            id: debtTermYears

                            unit: qsTr("Years")
                            placeholder: qsTr("18")
                        }

                        Text {
                            text: qsTr("What is left to repay, in years. CSN states "
                                 + "this; entering it lets hinkwise recalculate the "
                                 + "payment each January the way CSN does. Empty "
                                 + "means not stated.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Text {
                id: debtError

                visible: text.length > 0
                textFormat: Text.PlainText
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceSm
                spacing: Theme.spaceSm

                Item {
                    Layout.fillWidth: true
                }

                PrimaryButton {
                    text: qsTr("Cancel")
                    filled: false
                    onClicked: debtDialog.close()
                }

                PrimaryButton {
                    text: debtDialog.editing ? qsTr("Save") : qsTr("Add")
                    onClicked: {
                        const annuity = debtStyleAnnuity.checked;
                        const styleIndex = annuity ? accounts.annuityStyleIndex
                                                   : accounts.fixedAmortisationStyleIndex;
                        const fee = annuity ? debtFee.text : "";
                        const term = annuity ? debtTermYears.text : "";
                        const failure = debtDialog.editing
                                ? accounts.updateDebt(page.pendingDebtId, debtName.text,
                                                      debtKind.currentIndex, debtRate.text,
                                                      styleIndex, debtPayment.text, fee, term)
                                : accounts.addDebt(debtName.text, debtKind.currentIndex,
                                                   debtRate.text, styleIndex,
                                                   debtPayment.text, fee, term,
                                                   debtBalance.text);
                        if (failure.length > 0) {
                            debtError.text = failure;
                            return;
                        }
                        debtDialog.close();
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmAccountDialog

        title: qsTr("Delete %1?").arg(page.pendingAccountName)
        message: qsTr("The account and every balance recorded for it are removed. "
             + "The history is not archived: a month that used the old balance will "
             + "be recalculated without it. This cannot be undone.")
        confirmLabel: qsTr("Delete account")

        onConfirmed: {
            const failure = accounts.removeAccount(page.pendingAccountId);
            page.pageError = failure;
        }
    }

    ConfirmDialog {
        id: confirmDebtDialog

        title: qsTr("Delete %1?").arg(page.pendingDebtName)
        message: qsTr("The debt and every balance recorded for it are removed, and "
             + "net worth rises by what was left to repay. This cannot be undone.")
        confirmLabel: qsTr("Delete debt")

        onConfirmed: {
            const failure = accounts.removeDebt(page.pendingDebtId);
            page.pageError = failure;
        }
    }
}
