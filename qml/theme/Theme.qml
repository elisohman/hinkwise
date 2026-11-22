// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQuick

QtObject {
    // Surfaces, darkest to lightest
    readonly property color background: "#0f1419"
    readonly property color surface: "#161d26"
    readonly property color surfaceRaised: "#1d2733"
    readonly property color border: "#2a3745"

    // Text
    readonly property color textPrimary: "#e6edf3"
    readonly property color textSecondary: "#9aa7b4"
    readonly property color textMuted: "#6b7a89"

    // Semantic, one per bucket
    readonly property color buffer: "#4ea1d3"
    readonly property color preserve: "#5ac8a0"
    readonly property color growth: "#c9a227"
    readonly property color play: "#b06ab3"

    readonly property color positive: "#5ac8a0"
    readonly property color warning: "#d9a441"
    readonly property color danger: "#d9534f"

    // Type scale
    readonly property int fontHero: 56
    readonly property int fontDisplay: 34
    readonly property int fontTitle: 20
    readonly property int fontMetric: 26
    readonly property int fontBody: 14
    readonly property int fontCaption: 12

    // Spacing
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 16
    readonly property int spaceLg: 24
    readonly property int spaceXl: 40

    readonly property int radius: 10
    readonly property int radiusSmall: 4

    function bucketColour(key: string): color {
        switch (key) {
        case "buffer":
            return buffer;
        case "preserve":
            return preserve;
        case "growth":
            return growth;
        case "play":
            return play;
        default:
            return textMuted;
        }
    }

    function severityColour(key: string): color {
        switch (key) {
        case "critical":
            return danger;
        case "warning":
            return warning;
        case "advice":
            return preserve;
        default:
            return textMuted;
        }
    }
}
