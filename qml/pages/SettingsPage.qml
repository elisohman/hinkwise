// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hinkwise

// Who the household is, where it is on the journey, and where its data lives.
//
// The phase sits at the top because it is a life state and not a preference:
// it decides how every bucket is sized and which way money moves between them.
// Picking another phase therefore opens the before/after sheet, and nothing is
// written until the household confirms it.
Item {
    id: page

    /// The households this database holds. The settings list, the rail's
    /// switcher and the shell's example badge all read this one model, so the
    /// three cannot disagree about which household is open.
    required property ProfilesViewModel profiles

    /// True while any field on this page holds what the household typed but has
    /// not saved. The shell refuses a profile switch while this is set.
    readonly property bool unsavedChanges: settings.trackingModeDirty
                                          || settings.displayNameDirty
                                          || settings.bufferTargetDirty
                                          || settings.jurisdictionDirty

    /// The profile whose deletion has been asked for, and its name for the
    /// question. Zero while no deletion is waiting.
    property int pendingDeleteId: 0
    property string pendingDeleteName: ""

    // The four phases as one control. The label wraps rather than elides: at
    // the window's minimum width "Financially independent" is two lines, and a
    // phase the household cannot read is not a choice.
    component PhaseSegment: Button {
        id: segment

        property bool selected: false

        padding: 0
        implicitHeight: Math.max(segmentLabel.implicitHeight + 2 * Theme.spaceSm, 34)

        background: Rectangle {
            radius: Theme.radiusSmall
            color: segment.selected ? Theme.surfaceRaised
                 : segment.hovered ? Theme.surface
                 : "transparent"
            border.width: 1
            border.color: segment.visualFocus ? Theme.textSecondary
                          : segment.selected ? Theme.border
                          : "transparent"
        }

        contentItem: Text {
            id: segmentLabel

            text: segment.text
            color: segment.selected ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontCaption
            font.weight: segment.selected ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
    }

    // A switch drawn from the theme's own colours rather than the platform's,
    // so the row that changes household looks like the rest of the page. The
    // active profile's switch is disabled: choosing the household that is
    // already open would do nothing.
    component ProfileSwitch: Item {
        id: control

        property bool checked: false

        signal toggled()

        implicitWidth: 36
        implicitHeight: 20
        opacity: control.enabled ? 1.0 : 0.45

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: control.checked ? Theme.positive : Theme.surfaceRaised
            border.width: 1
            border.color: control.checked ? Theme.positive : Theme.border
        }

        Rectangle {
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            implicitWidth: 14
            implicitHeight: 14
            radius: 7
            color: Theme.background
        }

        MouseArea {
            anchors.fill: parent
            enabled: control.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: control.toggled()
        }
    }

    // A field stops following the view model once it has been typed in, so the
    // model pushes its own value back after a save, a reset or a reload.
    function syncFields() {
        if (rateField.text !== settings.safeWithdrawalRateText)
            rateField.text = settings.safeWithdrawalRateText;
        if (dragField.text !== settings.capitalTaxDragText)
            dragField.text = settings.capitalTaxDragText;
        if (lowField.text !== settings.fiMultiplierLowText)
            lowField.text = settings.fiMultiplierLowText;
        if (defaultField.text !== settings.fiMultiplierDefaultText)
            defaultField.text = settings.fiMultiplierDefaultText;
        if (highField.text !== settings.fiMultiplierHighText)
            highField.text = settings.fiMultiplierHighText;
        if (nameField.text !== settings.displayNameText)
            nameField.text = settings.displayNameText;
        if (bufferSlider.value !== settings.bufferTargetPosition)
            bufferSlider.value = settings.bufferTargetPosition;
    }

    SettingsViewModel {
        id: settings
    }

    Connections {
        target: settings

        function onChanged() {
            page.syncFields();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: qsTr("Settings")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                text: settings.currencyCode
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
            }
        }

        ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: settings.hasProfile
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.spaceLg

                // -----------------------------------------------------------
                // Profiles
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: profilesCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: profilesCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Profiles")
                            note: qsTr("%1 in this database").arg(page.profiles.count)
                        }

                        Text {
                            text: qsTr("A profile is one independent financial dataset: its own "
                                 + "accounts, debts, months and history. Profiles do not sync. "
                                 + "Duplicating one makes a copy, not a second view of the same "
                                 + "household.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceXs

                            Repeater {
                                model: page.profiles.profiles

                                delegate: Rectangle {
                                    id: profileRow

                                    required property var modelData

                                    Layout.fillWidth: true
                                    implicitHeight: profileRowLayout.implicitHeight
                                                     + 2 * Theme.spaceSm
                                    radius: Theme.radiusSmall
                                    color: profileRow.modelData.isActive ? Theme.surfaceRaised
                                                                        : "transparent"
                                    border.width: 1
                                    border.color: profileRow.modelData.isActive ? Theme.border
                                                                               : "transparent"

                                    RowLayout {
                                        id: profileRowLayout

                                        anchors.fill: parent
                                        anchors.margins: Theme.spaceSm
                                        spacing: Theme.spaceSm

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: Theme.spaceSm

                                                Text {
                                                    text: profileRow.modelData.name
                                                    color: Theme.textPrimary
                                                    font.pixelSize: Theme.fontBody
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                // An example says so here as well as in the
                                                // rail: the list is where the household
                                                // decides what to delete.
                                                Rectangle {
                                                    visible: profileRow.modelData.isExample
                                                    implicitWidth: exampleTag.implicitWidth
                                                                   + 2 * Theme.spaceSm
                                                    implicitHeight: exampleTag.implicitHeight
                                                                    + 2 * Theme.spaceXs
                                                    radius: Theme.radiusSmall
                                                    color: Theme.surface
                                                    border.width: 1
                                                    border.color: Theme.warning

                                                    Text {
                                                        id: exampleTag

                                                        anchors.centerIn: parent
                                                        text: qsTr("Example")
                                                        color: Theme.warning
                                                        font.pixelSize: Theme.fontCaption
                                                        font.weight: Font.DemiBold
                                                    }
                                                }
                                            }

                                            Text {
                                                text: profileRow.modelData.trackingModeLabel
                                                      + " · " + profileRow.modelData.phaseLabel
                                                color: Theme.textMuted
                                                font.pixelSize: Theme.fontCaption
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        Text {
                                            visible: profileRow.modelData.isActive
                                            text: qsTr("Current")
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                        }

                                        ProfileSwitch {
                                            checked: profileRow.modelData.isActive
                                            enabled: !profileRow.modelData.isActive
                                            onToggled: page.profiles.switchTo(
                                                           profileRow.modelData.id)
                                        }

                                        PrimaryButton {
                                            text: qsTr("Delete")
                                            filled: false
                                            onClicked: {
                                                page.pendingDeleteId = profileRow.modelData.id;
                                                page.pendingDeleteName = profileRow.modelData.name;
                                                deleteProfileDialog.open();
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceSm

                            PrimaryButton {
                                text: qsTr("New profile")
                                onClicked: page.profiles.createNewProfile()
                            }

                            PrimaryButton {
                                text: qsTr("Duplicate profile")
                                filled: false
                                onClicked: {
                                    if (page.profiles.beginDuplicate(page.profiles.activeId).length
                                            === 0)
                                        duplicateDialog.open();
                                }
                            }

                            PrimaryButton {
                                text: qsTr("Create an example")
                                filled: false
                                onClicked: exampleDialog.open()
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            text: qsTr("Creates an independent copy. Changes will not sync "
                                 + "between profiles.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: page.profiles.actionMessage.length > 0
                            text: page.profiles.actionMessage
                            textFormat: Text.PlainText
                            color: Theme.positive
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: page.profiles.actionError.length > 0
                            text: page.profiles.actionError
                            textFormat: Text.PlainText
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // Phase
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: phaseCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: phaseCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Phase")
                            note: settings.phaseLabel
                        }

                        Text {
                            text: qsTr("The phase decides how the buckets are sized and which "
                                 + "way money moves between them. It is not a preference: "
                                 + "recorded months and balances stay as they are, but every "
                                 + "target is calculated again.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            implicitHeight: segmentRow.implicitHeight + 4
                            radius: Theme.radiusSmall
                            color: Theme.background
                            border.color: Theme.border
                            border.width: 1

                            RowLayout {
                                id: segmentRow

                                anchors.fill: parent
                                anchors.margins: 2
                                spacing: 2

                                Repeater {
                                    model: settings.phaseChoices

                                    delegate: PhaseSegment {
                                        id: phaseSegment

                                        required property var modelData

                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: phaseSegment.modelData.label
                                        selected: phaseSegment.modelData.index
                                                  === settings.phaseIndex
                                        onClicked: {
                                            settings.beginPhaseChange(phaseSegment.modelData.index);
                                            if (settings.comparisonVisible)
                                                phaseSheet.open();
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            visible: settings.phaseError.length > 0
                            text: settings.phaseError
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // How you use hinkwise
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: trackingCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: trackingCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("How you use hinkwise")
                            note: qsTr("What an old month means")
                        }

                        Text {
                            text: qsTr("Both choices keep the same record. What changes is "
                                 + "how the application reads its age: a month you have not "
                                 + "written down yet, or a budget that is simply doing its "
                                 + "job.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceSm

                            Repeater {
                                model: settings.trackingModeChoices

                                delegate: ChoiceCard {
                                    id: trackingChoice

                                    required property var modelData

                                    label: trackingChoice.modelData.label
                                    description: trackingChoice.modelData.description
                                    checked: trackingChoice.modelData.index
                                             === settings.trackingModeIndex
                                    onClicked: settings.trackingModeIndex = trackingChoice.modelData.index
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceSm

                            PrimaryButton {
                                text: qsTr("Save")
                                enabled: settings.trackingModeDirty
                                onClicked: settings.saveTrackingMode()
                            }

                            PrimaryButton {
                                text: qsTr("Discard")
                                filled: false
                                visible: settings.trackingModeDirty
                                onClicked: settings.discardTrackingMode()
                            }

                            Text {
                                visible: settings.trackingModeDirty
                                text: qsTr("Unsaved changes")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            visible: settings.trackingModeError.length > 0
                            text: settings.trackingModeError
                            textFormat: Text.PlainText
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // Buffer target
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: bufferCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: bufferCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Buffer target")
                            note: qsTr("Where in the band this household wants to sit")
                        }

                        Text {
                            text: qsTr("The model gives the buffer a band and leaves the point "
                                 + "inside it to the household. Cautious sits at the band's low "
                                 + "end, bold at its high end.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceMd

                            Text {
                                text: qsTr("Cautious")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }

                            Slider {
                                id: bufferSlider

                                Layout.fillWidth: true
                                from: 0
                                to: settings.bufferTargetMaximum
                                stepSize: 100
                                snapMode: Slider.SnapAlways
                                value: settings.bufferTargetPosition
                                onMoved: settings.bufferTargetPosition = value

                                background: Rectangle {
                                    x: bufferSlider.leftPadding
                                    y: bufferSlider.topPadding + bufferSlider.availableHeight / 2
                                       - height / 2
                                    implicitHeight: 4
                                    width: bufferSlider.availableWidth
                                    height: implicitHeight
                                    radius: 2
                                    color: Theme.surfaceRaised

                                    Rectangle {
                                        width: bufferSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: 2
                                        color: Theme.buffer
                                    }
                                }

                                handle: Rectangle {
                                    x: bufferSlider.leftPadding + bufferSlider.visualPosition
                                       * (bufferSlider.availableWidth - width)
                                    y: bufferSlider.topPadding + bufferSlider.availableHeight / 2
                                       - height / 2
                                    implicitWidth: 16
                                    implicitHeight: 16
                                    radius: 8
                                    color: bufferSlider.pressed ? Theme.textPrimary
                                                                : Theme.buffer
                                    border.color: Theme.background
                                    border.width: 2
                                }
                            }

                            Text {
                                text: qsTr("Bold")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }
                        }

                        Text {
                            text: settings.bufferTargetPreview
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.Light
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("A bigger buffer is safer but earns roughly nothing, so "
                                 + "it should be as small as the household's situation allows.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceSm

                            PrimaryButton {
                                text: qsTr("Save")
                                enabled: settings.bufferTargetDirty
                                onClicked: settings.saveBufferTarget()
                            }

                            PrimaryButton {
                                text: qsTr("Discard")
                                filled: false
                                visible: settings.bufferTargetDirty
                                onClicked: settings.discardBufferTarget()
                            }

                            Text {
                                visible: settings.bufferTargetDirty
                                text: qsTr("Unsaved changes")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            visible: settings.bufferTargetError.length > 0
                            text: settings.bufferTargetError
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // Jurisdiction
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: jurisdictionCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: jurisdictionCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Jurisdiction")
                            note: qsTr("The tax and withdrawal assumptions")
                        }

                        Text {
                            text: qsTr("The withdrawal rate is what a year of expenses is divided "
                                 + "by to become a capital target, and the capital tax drag is "
                                 + "what the return loses to tax each year. The three multipliers "
                                 + "are the band the target is read from, in years of expenses: "
                                 + "cautious, classic and after tax.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceMd

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Safe withdrawal rate")
                                hint: qsTr("Before tax")

                                MoneyField {
                                    id: rateField

                                    text: settings.safeWithdrawalRateText
                                    placeholder: qsTr("4")
                                    unit: "%"
                                    error: settings.safeWithdrawalRateError
                                    onEdited: (value) => settings.safeWithdrawalRateText = value
                                }
                            }

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Capital tax drag")
                                hint: qsTr("Each year")

                                MoneyField {
                                    id: dragField

                                    text: settings.capitalTaxDragText
                                    placeholder: qsTr("1")
                                    unit: "%"
                                    error: settings.capitalTaxDragError
                                    onEdited: (value) => settings.capitalTaxDragText = value
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Low multiplier")

                                MoneyField {
                                    id: lowField

                                    text: settings.fiMultiplierLowText
                                    placeholder: qsTr("21")
                                    unit: qsTr("×")
                                    onEdited: (value) => settings.fiMultiplierLowText = value
                                }
                            }

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Default multiplier")

                                MoneyField {
                                    id: defaultField

                                    text: settings.fiMultiplierDefaultText
                                    placeholder: qsTr("25")
                                    unit: qsTr("×")
                                    onEdited: (value) => settings.fiMultiplierDefaultText = value
                                }
                            }

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("High multiplier")

                                MoneyField {
                                    id: highField

                                    text: settings.fiMultiplierHighText
                                    placeholder: qsTr("33")
                                    unit: qsTr("×")
                                    onEdited: (value) => settings.fiMultiplierHighText = value
                                }
                            }
                        }

                        Text {
                            visible: settings.multiplierError.length > 0
                            text: settings.multiplierError
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceSm

                            PrimaryButton {
                                text: qsTr("Save")
                                enabled: settings.jurisdictionDirty
                                onClicked: settings.saveJurisdiction()
                            }

                            PrimaryButton {
                                text: qsTr("Discard")
                                filled: false
                                visible: settings.jurisdictionDirty
                                onClicked: settings.discardJurisdiction()
                            }

                            Text {
                                visible: settings.jurisdictionDirty
                                text: qsTr("Unsaved changes")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            PrimaryButton {
                                text: qsTr("Reset to Swedish defaults")
                                filled: false
                                onClicked: settings.resetJurisdictionToSwedishDefaults()
                            }
                        }

                        Text {
                            text: qsTr("Resetting fills the fields with the Swedish constants. "
                                 + "Nothing is applied until you save, and every figure in the "
                                 + "application is recalculated when you do.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // The household: name and currency
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: householdCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: householdCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Household")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceMd

                            FormRow {
                                Layout.fillWidth: true
                                label: qsTr("Display name")

                                TextField {
                                    id: nameField

                                    Layout.fillWidth: true
                                    text: settings.displayNameText
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    selectionColor: Theme.buffer
                                    selectedTextColor: Theme.background
                                    onTextEdited: settings.displayNameText = text

                                    background: Rectangle {
                                        implicitHeight: 34
                                        radius: Theme.radiusSmall
                                        color: Theme.background
                                        border.width: 1
                                        border.color: nameField.activeFocus ? Theme.textSecondary
                                                                          : Theme.border
                                    }
                                }
                            }

                            PrimaryButton {
                                Layout.alignment: Qt.AlignBottom
                                text: qsTr("Save")
                                enabled: settings.displayNameDirty
                                onClicked: settings.saveDisplayName()
                            }

                            PrimaryButton {
                                Layout.alignment: Qt.AlignBottom
                                text: qsTr("Discard")
                                filled: false
                                visible: settings.displayNameDirty
                                onClicked: settings.discardDisplayName()
                            }

                            Text {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: Theme.spaceSm
                                visible: settings.displayNameDirty
                                text: qsTr("Unsaved changes")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                            }
                        }

                        Text {
                            visible: settings.displayNameError.length > 0
                            text: settings.displayNameError
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        FormRow {
                            Layout.fillWidth: true
                            label: qsTr("Base currency")
                            hint: qsTr("Read-only")

                            TextField {
                                Layout.fillWidth: true
                                readOnly: true
                                text: settings.currencyCode + " · " + settings.currencyName
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontBody
                                selectionColor: Theme.buffer
                                selectedTextColor: Theme.background

                                background: Rectangle {
                                    implicitHeight: 34
                                    radius: Theme.radiusSmall
                                    color: Theme.background
                                    border.width: 1
                                    border.color: Theme.border
                                }
                            }
                        }

                        Text {
                            text: qsTr("Every balance is stored in the profile currency. "
                                 + "Switching it here would silently relabel the amounts "
                                 + "already recorded, so this build does not offer it: a "
                                 + "household that thinks in another currency is set up with "
                                 + "it from the first run.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // Data: the file, and the backup that leaves it
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: dataCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: dataCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Data")
                        }

                        FormRow {
                            Layout.fillWidth: true
                            label: qsTr("Database")

                            Text {
                                text: settings.databasePath
                                textFormat: Text.PlainText
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceSm

                            PrimaryButton {
                                text: qsTr("Export backup")
                                onClicked: backupDialog.open()
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            visible: settings.backupMessage.length > 0
                            text: settings.backupMessage
                            textFormat: Text.PlainText
                            color: Theme.positive
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: settings.backupError.length > 0
                            text: settings.backupError
                            textFormat: Text.PlainText
                            color: Theme.danger
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("The backup is one JSON file holding every account, "
                                 + "debt, month and valuation together with the phase history. "
                                 + "It is written where you choose, and it is yours: keep a "
                                 + "copy somewhere other than this machine.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("A duplicate stays inside this database. Use Export for a "
                                 + "copy you can keep.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            implicitHeight: 1
                            color: Theme.border
                        }

                        Text {
                            text: qsTr("Import is not yet available. It would have to replace "
                                 + "everything already recorded, and this build cannot do that "
                                 + "without risking what is here, so it writes backups and "
                                 + "nothing else.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // -----------------------------------------------------------
                // Phase history
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
                        spacing: Theme.spaceXs

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("Phase history")
                        }

                        Text {
                            visible: settings.phaseHistory.length === 0
                            text: qsTr("No phase changes recorded yet. The phase the household "
                                 + "started in is where it began, not a change.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: settings.phaseHistory

                            delegate: RowLayout {
                                id: historyRow

                                required property var modelData

                                Layout.fillWidth: true
                                spacing: Theme.spaceMd

                                Text {
                                    text: historyRow.modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: historyRow.modelData.when
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }
                }

                // -----------------------------------------------------------
                // About
                // -----------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: aboutCard.implicitHeight + 2 * Theme.spaceLg
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: aboutCard

                        anchors.fill: parent
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        SectionHeader {
                            Layout.fillWidth: true
                            title: qsTr("About")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                text: qsTr("Hinkwise")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontTitle
                                font.weight: Font.DemiBold
                            }

                            Text {
                                text: settings.appVersion
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontBody
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            text: qsTr("Personal finance planning around the four-bucket model: "
                                 + "buffer, preserve, growth and play, sized from what the "
                                 + "household actually spends.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("Licensed under GPL-3.0-or-later. The source is yours to "
                                 + "read, change and share.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("Hinkwise links dynamically against Qt %1, the version "
                                 + "this system provides, which is licensed under "
                                 + "LGPL-3.0-only and is not redistributed here.")
                                      .arg(settings.qtVersion)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("Hinkwise is an independent project and is not "
                                 + "affiliated with RikaTillsammans.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: qsTr("It is not financial advice. The model's ratios are "
                                 + "general, the numbers are your own, and the decisions are "
                                 + "yours too.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Text {
            visible: !settings.hasProfile
            text: qsTr("Set up a household first: there is nothing to configure until the "
                 + "four buckets know what they are sized against.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    // Where an exported backup goes. QtQuick.Dialogs does the picking; the view
    // model decides what a path means and reports what was written.
    FileDialog {
        id: backupDialog

        title: qsTr("Save a backup")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON backup (*.json)")]
        currentFile: settings.backupFileUrl
        onAccepted: settings.exportBackup(selectedFile)
    }

    // A copy is named before it exists: two profiles with one name are two rows
    // the household cannot tell apart.
    Popup {
        id: duplicateDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 2 * Theme.spaceLg : 480)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape
        // Dismissing the question is the same as cancelling it; confirming has
        // already cleared the pending name by the time this runs.
        onClosed: page.profiles.cancelDuplicate()

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
                text: qsTr("Duplicate profile")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("Creates an independent copy. Changes will not sync between "
                     + "profiles.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Name of the copy")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
            }

            TextField {
                id: duplicateNameField

                Layout.fillWidth: true
                text: page.profiles.duplicateName
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                selectionColor: Theme.buffer
                selectedTextColor: Theme.background
                onTextEdited: page.profiles.duplicateName = text

                background: Rectangle {
                    implicitHeight: 34
                    radius: Theme.radiusSmall
                    color: Theme.background
                    border.width: 1
                    border.color: duplicateNameField.activeFocus ? Theme.textSecondary
                                                                 : Theme.border
                }
            }

            Text {
                visible: page.profiles.actionError.length > 0
                text: page.profiles.actionError
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
                    onClicked: duplicateDialog.close()
                }

                PrimaryButton {
                    text: qsTr("Create copy")
                    onClicked: {
                        if (page.profiles.confirmDuplicate().length === 0)
                            duplicateDialog.close();
                    }
                }
            }
        }
    }

    // Two stories, both flagged as examples. The choice says what each one
    // contains before it is written, because an example that surprises the
    // household is worse than none.
    Popup {
        id: exampleDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent ? parent.width - 2 * Theme.spaceLg : 520)
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
                text: qsTr("Create an example")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("An example is a whole household built from a story: its accounts, "
                     + "debts, months and balances. It is marked as an example everywhere, "
                     + "and deleting it removes every row it made.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: page.profiles.exampleChoices

                delegate: ColumnLayout {
                    id: exampleChoice

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    PrimaryButton {
                        Layout.fillWidth: true
                        text: exampleChoice.modelData.label
                        onClicked: {
                            if (page.profiles.createExample(exampleChoice.modelData.index).length
                                    === 0)
                                exampleDialog.close();
                        }
                    }

                    Text {
                        text: exampleChoice.modelData.description
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Text {
                visible: page.profiles.actionError.length > 0
                text: page.profiles.actionError
                textFormat: Text.PlainText
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceSm

                Item {
                    Layout.fillWidth: true
                }

                PrimaryButton {
                    text: qsTr("Cancel")
                    filled: false
                    onClicked: exampleDialog.close()
                }
            }
        }
    }

    // Deleting a profile is deleting a household's whole record, so the
    // question says what goes with it in the same words every time.
    ConfirmDialog {
        id: deleteProfileDialog

        title: qsTr("Delete profile")
        message: qsTr("Delete %1? Its accounts, debts, months and history go with it. This "
                      + "cannot be undone.")
                     .arg(page.pendingDeleteName)
        onConfirmed: page.profiles.deleteProfile(page.pendingDeleteId)
    }

    // The phase change, with what it does to this household's own figures.
    Popup {
        id: phaseSheet

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, parent ? parent.width - 2 * Theme.spaceLg : 640)
        modal: true
        focus: true
        padding: Theme.spaceLg
        closePolicy: Popup.CloseOnEscape
        onClosed: settings.cancelPhaseChange()

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
                text: qsTr("Change phase to %1?").arg(settings.pendingPhaseLabel)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("These are the targets this phase gives this household, in its own "
                     + "money at this month's expenses. Nothing that has been recorded changes.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            ScrollView {
                id: comparisonScroll

                Layout.fillWidth: true
                Layout.maximumHeight: Math.min(340,
                                               (phaseSheet.parent ? phaseSheet.parent.height : 720)
                                               - 360)
                Layout.topMargin: Theme.spaceXs
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: comparisonScroll.availableWidth
                    spacing: Theme.spaceSm

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Item {
                            Layout.preferredWidth: 130
                        }

                        Text {
                            text: qsTr("Now")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                        }

                        Text {
                            text: qsTr("After")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                        }
                    }

                    Repeater {
                        model: settings.comparisonRows

                        delegate: RowLayout {
                            id: comparisonRow

                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            Text {
                                text: comparisonRow.modelData.label
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                Layout.preferredWidth: 130
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                text: comparisonRow.modelData.before
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                text: comparisonRow.modelData.after
                                color: comparisonRow.modelData.changed ? Theme.textPrimary
                                                                       : Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                font.weight: comparisonRow.modelData.changed ? Font.DemiBold
                                                                             : Font.Normal
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("Grey rows stay as they are. The 10 % cap on Play is a cap and not "
                     + "a target, which is why it survives every phase.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                visible: !settings.comparisonChanges
                text: qsTr("These two phases share the same sizing table, so no figure above "
                     + "moves.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Recorded months and balances are not modified. Only how the "
                     + "targets are calculated changes.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                visible: settings.phaseError.length > 0
                text: settings.phaseError
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
                    focus: true
                    // Close the sheet rather than clearing the pending phase
                    // behind it: onClosed is the single place that resets the
                    // pending state, so cancelling and dismissing stay identical.
                    // Clearing here left the sheet open reading from a phase
                    // that no longer existed, with a blank title and no rows.
                    onClicked: phaseSheet.close()
                }

                PrimaryButton {
                    text: qsTr("Change phase")
                    onClicked: {
                        if (settings.confirmPhaseChange().length > 0)
                            return;
                        phaseSheet.close();
                    }
                }
            }
        }
    }

    // The inverse of onClosed: whenever the pending phase is cleared - by a
    // successful change, by cancel, or by any future path - the sheet closes.
    // The sheet is the only thing that may hold a pending phase, so the two can
    // never disagree again, whatever a button forgets to do.
    Connections {
        target: settings

        function onChanged() {
            if (!settings.comparisonVisible)
                phaseSheet.close();
        }
    }
}
