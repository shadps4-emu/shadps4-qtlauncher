// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later


#include "about_dialog.h"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QWidget>

#include "gui_settings.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>

AboutDialog::AboutDialog(std::shared_ptr<gui_settings> gui_settings, QWidget*)
    : m_gui_settings(std::move(gui_settings))
{
    const QPalette p = qApp->palette();

    QQuickStyle::setStyle("Fusion");

    auto* engine = new QQmlApplicationEngine;
    auto* ctx = engine->rootContext();
    ctx->setContextProperty("themeWindow",          p.color(QPalette::Window).name());
    ctx->setContextProperty("themeText",            p.color(QPalette::WindowText).name());
    ctx->setContextProperty("themeButton",          p.color(QPalette::Button).name());
    ctx->setContextProperty("themeButtonText",      p.color(QPalette::ButtonText).name());
    ctx->setContextProperty("themeHighlight",       p.color(QPalette::Highlight).name());
    ctx->setContextProperty("themeHighlightedText", p.color(QPalette::HighlightedText).name());
    ctx->setContextProperty("themeLink",            p.color(QPalette::Link).name());
    ctx->setContextProperty("themeMid",             p.color(QPalette::Mid).name());

    ctx->setContextProperty("guiSettings", m_gui_settings.get());
    ctx->setContextProperty("licenseText", []() -> QString {
        QFile f(":/LICENSE");
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll());
    }());

    QObject::connect(engine, &QQmlApplicationEngine::warnings, engine,
                     [](const QList<QQmlError>& warnings) {
                         for (const auto& warning : warnings) {
                             qWarning().noquote() << warning.toString();
                         }
                     });

    engine->load(QUrl(QStringLiteral("qrc:/qml/AboutDialog.qml")));
}