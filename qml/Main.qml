// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

ApplicationWindow {
    id: window

    width: 1120
    height: 720
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("Hinkwise")
    color: Theme.background

    DashboardViewModel {
        id: dashboardModel

        Component.onCompleted: dashboardModel.reload()
    }

    UpdateFlowViewModel {
        id: updateFlowModel
    }

    /// The households in this database. One model feeds the settings list, the
    /// rail's switcher and the shell's example badge, so the three cannot
    /// disagree about which one is open.
    ProfilesViewModel {
        id: profilesModel

        // Only the shell knows what is on screen, so the two things a switch
        // must not throw away are bound here from the pages that own them.
        updateFlowOpen: updateFlow.visible
        cashflowDirty: cashflowPage.unsavedChanges
        settingsDirty: settingsPage.unsavedChanges

        // A profile was created empty and is active now: the wizard fills it
        // in, and the shell steps aside until it has.
        onOnboardingRequested: {
            wizardPage.restart();
            window.onboarding = true;
        }
    }

    /// True while the wizard is filling in a profile created from settings.
    /// The pages are hidden meanwhile, because the wizard creates the household
    /// every other screen needs before there is anything to show.
    property bool onboarding: false

    // The update flow is the same sheet wherever it starts: the overview's
    // status line, the accounts page, cashflow, or the plan with nothing to
    // project. Opening it always begins at the current month.
    function openUpdateFlow() {
        updateFlowModel.begin();
        updateFlow.step = 0;
        updateFlow.visible = true;
        updateFlow.forceActiveFocus();
    }

    // The year-end review is an occasional action, so it is not a rail item:
    // the plan owns it, and the overview offers a way in.
    function openYearEndReview() {
        rail.currentIndex = 3;
        planPage.showingReview = true;
    }

    // Before there is a household there is nothing to show in the rail, so the
    // first run takes the whole window. Once the wizard has written the
    // profile, dashboardModel.hasProfile flips and the shell appears.
    OnboardingPage {
        id: wizardPage

        anchors.fill: parent
        visible: !dashboardModel.hasProfile || window.onboarding
        onFinished: window.onboarding = false
    }

    RowLayout {
        anchors.fill: parent
        visible: dashboardModel.hasProfile && !window.onboarding
        spacing: 0

        NavRail {
            id: rail

            Layout.fillHeight: true
            phaseLabel: dashboardModel.phaseLabel
            yearsOfFreedomLabel: dashboardModel.yearsOfFreedomLabel
            profiles: profilesModel
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: rail.currentIndex

            DashboardPage {
                dashboard: dashboardModel
                onUpdateRequested: window.openUpdateFlow()
                onReviewRequested: window.openYearEndReview()
            }

            BucketsPage {
                updateActionLabel: dashboardModel.updateActionLabel
                onUpdateRequested: window.openUpdateFlow()
            }

            CashflowPage {
                id: cashflowPage

                updateActionLabel: dashboardModel.updateActionLabel
                onUpdateRequested: window.openUpdateFlow()
            }

            PlanPage {
                id: planPage

                updateActionLabel: dashboardModel.updateActionLabel
                onNavigateRequested: (index) => rail.currentIndex = index
                onUpdateRequested: window.openUpdateFlow()
            }

            GuidePage {
            }

            SettingsPage {
                id: settingsPage

                profiles: profilesModel
            }
        }
    }

    UpdateFlowOverlay {
        id: updateFlow

        anchors.fill: parent
        visible: false
        flow: updateFlowModel
        onCloseRequested: updateFlow.visible = false
    }
}
