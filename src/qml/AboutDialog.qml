// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Fusion

ApplicationWindow {
    id: root
    width: 800
    height: 400
    visible: true
    title: qsTr("About")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            id: bar
            Layout.fillWidth: true
            background: Rectangle { color: themeButton }

            TabButton {
                text: qsTr("About")
                background: Rectangle {
                    color: bar.currentIndex === 0 ? palette.highlight : palette.button
                }
                contentItem: Text {
                    text: parent.text
                    color: bar.currentIndex === 0 ? palette.highlightedText : palette.buttonText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                text: qsTr("Links")
                background: Rectangle {
                    color: bar.currentIndex === 1 ? palette.highlight : palette.button
                }
                contentItem: Text {
                    text: parent.text
                    color: bar.currentIndex === 1 ? palette.highlightedText : palette.buttonText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }}
            TabButton {
                text: qsTr("Contribute")
                background: Rectangle {
                    color: bar.currentIndex === 2 ? palette.highlight : palette.button
                }
                contentItem: Text {
                    text: parent.text
                    color: bar.currentIndex === 2 ? palette.highlightedText : palette.buttonText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }}
            TabButton {
                text: qsTr("License")
                background: Rectangle {
                    color: bar.currentIndex === 3 ? palette.highlight : palette.button
                }
                contentItem: Text {
                    text: parent.text
                    color: bar.currentIndex === 3 ? palette.highlightedText : palette.buttonText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }}
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bar.currentIndex

            // --- About ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themeWindow

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.topMargin: 10

                    Item {
                        Layout.fillHeight: true
                        Layout.preferredWidth: parent.width * 0.4

                        Image {
                            anchors.fill: parent
                            anchors.margins: 5
                            source: "qrc:/images/shadps4.png"
                            fillMode: Image.PreserveAspectFit
                        }
                    }

                    ColumnLayout {
                        Layout.fillHeight: true

                        Text {
                            text: "shadPS4"
                            font.pixelSize: 32
                            font.bold: true
                            color: themeText
                            Layout.margins: 5
                        }

                        Text {
                            text: qsTr("shadPS4 is an experimental open-source emulator for\n" +
                                "the PlayStation 4.\n\n" +
                                "This software should not be used to play games you\n" +
                                "have not legally obtained.")
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: 18
                            color: themeText
                            Layout.topMargin: 0
                            Layout.leftMargin: 5
                            Layout.rightMargin: 5
                            Layout.bottomMargin: 5
                        }
                    }
                }
            }

            // --- Links ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themeWindow

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 16

                    AboutIcon { source: "qrc:/images/github.png";  url: "https://github.com/shadps4-emu/shadPS4" }
                    AboutIcon { source: "qrc:/images/discord.png"; url: "https://discord.gg/bFJxfftGW6" }
                    AboutIcon { source: "qrc:/images/youtube.png"; url: "https://www.youtube.com/@shadPS4/videos" }
                    AboutIcon { source: "qrc:/images/ko-fi.png";   url: "https://ko-fi.com/shadps4" }
                    AboutIcon { source: "qrc:/images/website.png"; url: "https://shadps4.net" }
                }
            }

            // --- Contribute ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themeWindow

                Column {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                        margins: 50
                    }
                    spacing: 24

                    Text {
                        text: qsTr("Want to contribute to shadPS4? Check out our GitHub repositories:")
                        font.pixelSize: 18
                        color: themeText
                        wrapMode: Text.WordWrap
                        width: parent.width
                        lineHeight: 1.5
                    }

                    Rectangle {
                        width: parent.width * 0.4
                        height: 1
                        color: themeMid
                        opacity: 0.5
                    }

                    Column {
                        spacing: 24
                        width: parent.width

                        Column {
                            spacing: 6
                            Text {
                                text: "shadPS4"
                                font.pixelSize: 16
                                font.bold: true
                                color: themeText
                            }
                            Text {
                                textFormat: Text.RichText
                                text: "<a href='https://github.com/shadps4-emu/shadps4' style='color:" + themeLink + "; text-decoration:none;'>github.com/shadps4-emu/shadps4</a>"
                                font.pixelSize: 14
                                onLinkActivated: Qt.openUrlExternally(link)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }

                        Column {
                            spacing: 6
                            Text {
                                text: "QtLauncher"
                                font.pixelSize: 16
                                font.bold: true
                                color: themeText
                            }
                            Text {
                                textFormat: Text.RichText
                                text: "<a href='https://github.com/shadps4-emu/shadps4-qtlauncher' style='color:" + themeLink + "; text-decoration:none;'>github.com/shadps4-emu/shadps4-qtlauncher</a>"
                                font.pixelSize: 14
                                onLinkActivated: Qt.openUrlExternally(link)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width * 0.4
                        height: 1
                        color: themeMid
                        opacity: 0.5
                    }

                    Text {
                        textFormat: Text.RichText
                        text: qsTr("Want to help translate shadPS4? We use ") +
                            "<a href='https://crowdin.com/project/shadps4' style='color:" + themeLink + "; text-decoration:none;'>Crowdin</a>" +
                            qsTr(" to manage our translations.")
                        font.pixelSize: 18
                        color: themeText
                        wrapMode: Text.WordWrap
                        width: parent.width
                        lineHeight: 1.5
                        onLinkActivated: Qt.openUrlExternally(link)
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // --- License ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themeWindow

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: qsTr("shadPS4 is licensed under the GPLv2 license")
                        font.pixelSize: 16
                        font.bold: true
                        color: themeText
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: themeMid
                        opacity: 0.5
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                            color: themeText
                            lineHeight: 1.6
                            text: licenseText
                        }
                    }
                }
            }
        }
    }
}