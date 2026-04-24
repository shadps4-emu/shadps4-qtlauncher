// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QObject>

class QWidget;
class gui_settings;

class AboutDialog : public QObject {
    Q_OBJECT

public:
    explicit AboutDialog(std::shared_ptr<gui_settings> gui_settings, QWidget* parent = nullptr);
    ~AboutDialog() = default;

private:
    std::shared_ptr<gui_settings> m_gui_settings;
};