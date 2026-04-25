// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string source
    property string url
    property bool hovered: false

    width: 130
    height: 130

    Image {
        id: img
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectFit
    }

    ShaderEffect {
        anchors.fill: parent

        property variant source: img
        property real invert: hovered ? 1.0 : 0.0

        fragmentShader: "qrc:/shaders/invert.frag.qsb"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        onEntered: root.hovered = true
        onExited: root.hovered = false

        onClicked: {
            if (root.url !== "")
                Qt.openUrlExternally(root.url)
        }
    }
}