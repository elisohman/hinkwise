// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hinkwise

// The model in one sitting: what the application is, what each bucket is for,
// what to enter, what the figures mean, and the two mistakes worth avoiding.
// It is a page rather than onboarding because it stays useful after the
// household has entered everything.
Item {
    id: page

    readonly property var bucketRows: [
        {
            label: qsTr("Buffer"),
            horizon: qsTr("0–3 years"),
            purpose: qsTr("Cash for shocks and spending within three years.")
        },
        {
            label: qsTr("Preserve value"),
            horizon: qsTr("3–9 years"),
            purpose: qsTr("Money you will need soon, kept steady.")
        },
        {
            label: qsTr("Growth"),
            horizon: qsTr("10+ years"),
            purpose: qsTr("The long-run engine: broad funds and equities.")
        },
        {
            label: qsTr("Play"),
            horizon: qsTr("Any time"),
            purpose: qsTr("A small pot for experiments, inside its cap.")
        }
    ]

    component GuideSection: ColumnLayout {
        id: section

        property string heading: ""

        Layout.fillWidth: true
        spacing: Theme.spaceSm

        SectionHeader {
            Layout.fillWidth: true
            title: section.heading
        }
    }

    component Bullet: RowLayout {
        id: bullet

        property string line: ""

        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Rectangle {
            implicitWidth: 4
            implicitHeight: 4
            radius: 2
            color: Theme.textMuted
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.spaceSm
        }

        Text {
            text: bullet.line
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    ScrollView {
        id: scroll

        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spaceLg

            Text {
                text: qsTr("Guide")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Light
            }

            GuideSection {
                heading: qsTr("What hinkwise is")

                Text {
                    text: qsTr("A planning tool for the four-bucket model: your accounts and "
                         + "debts, sorted by when the money is needed.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: qsTr("Everything you enter stays on this machine.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            GuideSection {
                heading: qsTr("The four buckets")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceLg

                    Text {
                        text: qsTr("Bucket")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        Layout.preferredWidth: 130
                    }

                    Text {
                        text: qsTr("Horizon")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        Layout.preferredWidth: 90
                    }

                    Text {
                        text: qsTr("Roughly what belongs there")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        Layout.fillWidth: true
                    }
                }

                Repeater {
                    model: page.bucketRows

                    delegate: RowLayout {
                        id: bucketRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Theme.spaceLg

                        Text {
                            text: bucketRow.modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            Layout.preferredWidth: 130
                            elide: Text.ElideRight
                        }

                        Text {
                            text: bucketRow.modelData.horizon
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.preferredWidth: 90
                        }

                        Text {
                            text: bucketRow.modelData.purpose
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            GuideSection {
                heading: qsTr("What to enter")

                Bullet {
                    line: qsTr("Accounts at market value, with debts entered separately.")
                }

                Bullet {
                    line: qsTr("Monthly income split into needs and wants.")
                }

                Bullet {
                    line: qsTr("Savings optionally split into buffer, investments and amortisation.")
                }

                Bullet {
                    line: qsTr("Each month's balances, so the figures stay current.")
                }
            }

            GuideSection {
                heading: qsTr("What the numbers mean")

                Bullet {
                    line: qsTr("Years of freedom: investable capital divided by a year of "
                         + "expenses.")
                }

                Bullet {
                    line: qsTr("Buffer months: the buffer divided by one month of expenses.")
                }

                Bullet {
                    line: qsTr("Crash impact: how far total net worth would fall if the growth "
                         + "bucket halved.")
                }
            }

            GuideSection {
                heading: qsTr("The two traps")

                Bullet {
                    line: qsTr("Property goes in at market value, not at your equity. The "
                         + "mortgage is a separate debt; hinkwise works out the equity. If you "
                         + "would rather record only your equity, leave the mortgage out "
                         + "entirely - recording both subtracts it twice.")
                }

                Bullet {
                    line: qsTr("Amortisation counts as saving: paying down the mortgage builds "
                         + "net worth like a transfer to a fund.")
                }
            }

            GuideSection {
                heading: qsTr("Not financial advice")

                Text {
                    text: qsTr("Hinkwise is an independent planning aid, not financial, tax or "
                         + "investment advice, and it is not affiliated with RikaTillsammans.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: qsTr("The model: <a href=\"https://rikatillsammans.se/spara-och-"
                         + "investera/fyra-hinkar-modellen/\">fyra-hinkar-modellen at "
                         + "RikaTillsammans</a>.")
                    textFormat: Text.StyledText
                    linkColor: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
            }
        }
    }
}
