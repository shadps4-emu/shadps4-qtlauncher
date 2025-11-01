// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <optional>

#include <QDialog>
#include <QLineEdit>

#include "ipc/ipc_client.h"

constexpr auto UI_SKY_NUM = 16;

class skylander_creator_dialog : public QDialog {
    Q_OBJECT

public:
    explicit skylander_creator_dialog(QWidget* parent);
    QString get_file_path() const;

protected:
    QString file_path;
};

class skylander_dialog : public QDialog {
    Q_OBJECT

public:
    explicit skylander_dialog(QWidget* parent, std::shared_ptr<IpcClient> ipc_client);
    ~skylander_dialog();
    static skylander_dialog* get_dlg(QWidget* parent, std::shared_ptr<IpcClient> ipc_client);

    skylander_dialog(skylander_dialog const&) = delete;
    void operator=(skylander_dialog const&) = delete;

protected:
    void clear_skylander(u8 slot);
    void create_skylander(u8 slot);
    void load_skylander(u8 slot);
    void load_skylander_path(u8 slot, const QString& path);

    void update_edits();

protected:
    QLineEdit* edit_skylanders[UI_SKY_NUM]{};
    static std::optional<std::tuple<u8, u16, u16>> sky_slots[UI_SKY_NUM];

private:
    static skylander_dialog* inst;
    std::shared_ptr<IpcClient> m_ipc_client;
};
