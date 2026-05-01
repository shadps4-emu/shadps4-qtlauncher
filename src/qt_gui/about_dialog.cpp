// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "about_dialog.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWidget>

AboutDialog::AboutDialog(QWidget* parent) {
    const QPalette p = qApp->palette();

    auto* engine = new QQmlApplicationEngine(parent);
    auto* ctx = engine->rootContext();
    ctx->setContextProperty("themeWindow", p.color(QPalette::Window).name());
    ctx->setContextProperty("themeText", p.color(QPalette::WindowText).name());
    ctx->setContextProperty("themeButton", p.color(QPalette::Button).name());
    ctx->setContextProperty("themeButtonText", p.color(QPalette::ButtonText).name());
    ctx->setContextProperty("themeHighlight", p.color(QPalette::Highlight).name());
    ctx->setContextProperty("themeHighlightedText", p.color(QPalette::HighlightedText).name());
    ctx->setContextProperty("themeLink", p.color(QPalette::Link).name());
    ctx->setContextProperty("themeMid", p.color(QPalette::Mid).name());

    ctx->setContextProperty("licenseText", []() -> QString {
        QFile f(":/LICENSE");
        (void)f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll());
    }());

    engine->load(QUrl(QStringLiteral("qrc:/qml/AboutDialog.qml")));
}