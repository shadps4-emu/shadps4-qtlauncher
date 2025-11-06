// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QGridLayout>
#include <QLineEdit>

#include "ipc/ipc_client.h"

class minifig_creator_dialog : public QDialog {
    Q_OBJECT

public:
    explicit minifig_creator_dialog(QWidget* parent);
    QString get_file_path() const;

protected:
    QString m_file_path;
};

class minifig_move_dialog : public QDialog {
    Q_OBJECT

public:
    explicit minifig_move_dialog(QWidget* parent, u8 old_index);
    u8 get_new_pad() const;
    u8 get_new_index() const;

protected:
    u8 m_pad = 0;
    u8 m_index = 0;

private:
    void add_minifig_position(QGridLayout* grid_panel, u8 index, u8 row, u8 column, u8 old_index);
};

class dimensions_dialog : public QDialog {
    Q_OBJECT

public:
    explicit dimensions_dialog(QWidget* parent, std::shared_ptr<IpcClient> ipc_client);
    ~dimensions_dialog();
    static dimensions_dialog* get_dlg(QWidget* parent, std::shared_ptr<IpcClient> ipc_client);
    void clear_all();

    dimensions_dialog(dimensions_dialog const&) = delete;
    void operator=(dimensions_dialog const&) = delete;

protected:
    void clear_figure(u8 pad, u8 index);
    void create_figure(u8 pad, u8 index);
    void load_figure(u8 pad, u8 index);
    void move_figure(u8 pad, u8 index);
    void load_figure_path(u8 pad, u8 index, const QString& path);
    static u32 GetFigureId(const std::array<u8, 0x2D * 0x04>& buf);
    static std::array<u8, 16> GenerateFigureKey(const std::array<u8, 0x2D * 0x04>& buf);
    static u32 Scramble(const std::array<u8, 7>& uid, u8 count);
    static std::array<u8, 4> DimensionsRandomize(const std::vector<u8>& key, u8 count);
    static std::array<u8, 8> Decrypt(const u8* buf, std::optional<std::array<u8, 16>> key);

protected:
    std::array<QLineEdit*, 7> m_edit_figures{};

private:
    void add_minifig_slot(QHBoxLayout* layout, u8 pad, u8 index);
    static dimensions_dialog* inst;
    std::shared_ptr<IpcClient> m_ipc_client;
};
