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
        id: dashboard

        Component.onCompleted: dashboard.reload()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXl
        spacing: Theme.spaceLg

        ColumnLayout {
            spacing: Theme.spaceXs

            Text {
                text: qsTr("Hinkwise")
                color: Theme.textPrimary
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("Your personal economy, seen through the four-bucket model.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }

        // Nothing has been entered yet: say so plainly instead of showing four
        // empty cards against zero targets.
        ColumnLayout {
            visible: !dashboard.hasProfile
            spacing: Theme.spaceSm

            Text {
                text: qsTr("Set up your economy")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("Data entry arrives in the next step. Once your profile, assets and "
                           + "monthly flow are in, the buckets fill themselves from the model.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 640
            }
        }

        // Headline: the cautious figure first, then the one number the model
        // uses to say how free the household is.
        ColumnLayout {
            visible: dashboard.hasProfile
            spacing: Theme.spaceXs

            Text {
                text: dashboard.headlineNetWorthLabel
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            Text {
                text: qsTr("Headline net worth, excluding housing and pension")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }

            Text {
                text: qsTr("%1 years of freedom").arg(dashboard.yearsOfFreedom.toFixed(1))
                color: Theme.positive
                font.pixelSize: Theme.fontTitle
            }

            Text {
                text: qsTr("Phase: %1 · Currency: %2").arg(dashboard.phaseLabel).arg(
                          dashboard.currencyLabel)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }

        GridLayout {
            visible: dashboard.hasProfile
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.spaceLg
            rowSpacing: Theme.spaceMd

            Repeater {
                model: [
                    {
                        title: qsTr("Annual expenses"),
                        value: dashboard.annualExpensesLabel
                    },
                    {
                        title: qsTr("Monthly expenses"),
                        value: dashboard.monthlyExpensesLabel
                    },
                    {
                        title: qsTr("Buffer"),
                        value: dashboard.bufferMonthsLabel
                    },
                    {
                        title: qsTr("Financial assets"),
                        value: dashboard.financialAssetsLabel
                    },
                    {
                        title: qsTr("Net worth"),
                        value: dashboard.netWorthLabel
                    },
                    {
                        title: qsTr("FI progress"),
                        value: dashboard.fiProgressLabel
                    },
                    {
                        title: qsTr("Savings rate"),
                        value: dashboard.savingsRateLabel
                    },
                    {
                        title: qsTr("Crash impact"),
                        value: dashboard.crashImpactLabel
                    },
                    {
                        title: qsTr("Housing and pension"),
                        value: dashboard.pensionAndHousingShareLabel
                    }
                ]

                delegate: ColumnLayout {
                    id: metricCell

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Text {
                        text: metricCell.modelData.title
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                    }

                    Text {
                        text: metricCell.modelData.value
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle
                    }
                }
            }
        }

        GridLayout {
            visible: dashboard.hasProfile
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spaceMd
            rowSpacing: Theme.spaceMd

            Repeater {
                model: dashboard.buckets

                delegate: BucketCard {
                    required property var model
                    required property string label
                    required property string horizonLabel
                    required property string balanceLabel
                    required property string colourKey

                    Layout.fillWidth: true
                    title: label
                    horizon: horizonLabel
                    amount: balanceLabel
                    // BucketCard already declares targetLabel and fillRatio as
                    // required properties, so they come through the model
                    // rather than being declared again on the delegate.
                    targetLabel: model.targetLabel
                    fillRatio: model.fillRatio
                    accent: Theme.bucketColour(colourKey)
                }
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            text: dashboard.hasProfile
                  ? qsTr("%1 checks from the model").arg(dashboard.insightsCount)
                  : qsTr("Skeleton build — no data yet.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }
    }
}
