// SPDX-FileCopyrightText: Copyright 2025 shadLauncher 4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QHeaderView>
#include <QtWidgets>
#include "core/emulator_settings.h"
#include "gui_settings.h"
#include "user_manager_dialog.h"

UserManagerDialog::UserManagerDialog(std::shared_ptr<gui_settings> gui_settings,
                                     std::shared_ptr<EmulatorSettings> emulator_settings,
                                     QWidget* parent)
    : QDialog(parent), m_gui_settings(std::move(gui_settings)),
      m_emu_settings(std::move(emulator_settings)) {
    setWindowTitle(tr("User Manager"));
    setMinimumSize(QSize(600, 400));
    setModal(true);

    // Table
    m_table = new QTableWidget(this);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setColumnCount(4); // User ID, Name, Color, Port
    m_table->setCornerButtonEnabled(false);
    m_table->setAlternatingRowColors(true);
    m_table->setHorizontalHeaderLabels({"User ID", "User Name", "Color", "Controller Port"});
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultSectionSize(150);
    m_table->installEventFilter(this);

    // Buttons
    push_create_user = new QPushButton(tr("&Create User"), this);
    push_create_user->setAutoDefault(false);

    push_remove_user = new QPushButton(tr("&Delete User"), this);
    push_remove_user->setAutoDefault(false);

    push_rename_user = new QPushButton(tr("&Rename User"), this);
    push_rename_user->setAutoDefault(false);

    push_set_default = new QPushButton(tr("&Set Default User"), this);
    push_set_default->setAutoDefault(false);

    push_set_color = new QPushButton(tr("&Set Color"), this);
    push_set_color->setAutoDefault(false);

    push_set_controller = new QPushButton(tr("&Set Controller Port"), this);
    push_set_controller->setAutoDefault(false);

    push_close = new QPushButton(tr("&Close"), this);
    push_close->setAutoDefault(false);

    // Button Layout
    QHBoxLayout* hbox_buttons = new QHBoxLayout();
    hbox_buttons->addWidget(push_create_user);
    hbox_buttons->addWidget(push_remove_user);
    hbox_buttons->addWidget(push_rename_user);
    hbox_buttons->addWidget(push_set_default);
    hbox_buttons->addWidget(push_set_color);
    hbox_buttons->addWidget(push_set_controller);
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(push_close);

    // Main Layout
    QVBoxLayout* vbox_main = new QVBoxLayout();
    vbox_main->addWidget(m_table);
    vbox_main->addLayout(hbox_buttons);
    setLayout(vbox_main);

    m_active_user = m_emu_settings->GetUserManager().GetUsers().default_user_id;
    UpdateTable();

    restoreGeometry(m_gui_settings->GetValue(gui::user_manager_geometry).toByteArray());

    // Button enabling lambda
    auto enable_buttons = [this]() {
        const u32 key = GetUserKey();
        bool valid = key != 0;
        push_remove_user->setEnabled(valid && key != m_active_user);
        push_rename_user->setEnabled(valid);
        push_set_default->setEnabled(valid && key != m_active_user);
        push_set_color->setEnabled(valid);
        push_set_controller->setEnabled(valid);
    };

    enable_buttons();

    connect(push_create_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserCreate);
    connect(push_remove_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserRemove);
    connect(push_rename_user, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserRename);
    connect(push_set_default, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserSetDefault);
    connect(push_set_color, &QAbstractButton::clicked, this, &UserManagerDialog::OnUserSetColor);
    connect(push_set_controller, &QAbstractButton::clicked, this,
            &UserManagerDialog::OnUserSetControllerPort);

    connect(push_close, &QAbstractButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, enable_buttons);
}

void UserManagerDialog::UpdateTable(bool mark_only) {
    QFont bold_font;
    bold_font.setBold(true);

    auto& manager = m_emu_settings->GetUserManager();
    const auto& users = manager.GetAllUsers();

    if (mark_only) {
        for (int i = 0; i < m_table->rowCount(); ++i) {
            for (int col = 0; col < m_table->columnCount(); ++col) {
                QTableWidgetItem* item = m_table->item(i, col);
                if (item) {
                    bool is_active =
                        (m_active_user == m_table->item(i, 0)->data(Qt::UserRole).toUInt());
                    item->setFont(is_active ? bold_font : QFont());
                }
            }
        }
        return;
    }

    m_table->setRowCount(static_cast<int>(users.size()));

    for (int row = 0; row < users.size(); ++row) {
        const User& u = users[row];

        // User ID
        QTableWidgetItem* id_item = new QTableWidgetItem(QString::number(u.user_id));
        id_item->setData(Qt::UserRole, u.user_id);
        id_item->setFlags(id_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, id_item);

        // Username
        QTableWidgetItem* username_item = new QTableWidgetItem(QString::fromStdString(u.user_name));
        username_item->setData(Qt::UserRole, u.user_id);
        username_item->setFlags(username_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 1, username_item);

        // Color
        QTableWidgetItem* color_item = new QTableWidgetItem();
        color_item->setFlags(color_item->flags() & ~Qt::ItemIsEditable);
        color_item->setData(Qt::DecorationRole, GetQColorFromIndex(u.user_color));
        m_table->setItem(row, 2, color_item);

        // Controller port
        QString controller_text = (u.controller_port >= 1 && u.controller_port <= 4)
                                      ? QString::number(u.controller_port)
                                      : "-";
        QTableWidgetItem* controller_item = new QTableWidgetItem(controller_text);
        controller_item->setFlags(controller_item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 3, controller_item);

        // Bold if active
        bool is_active = (m_active_user == u.user_id);
        if (is_active) {
            id_item->setFont(bold_font);
            username_item->setFont(bold_font);
            color_item->setFont(bold_font);
            controller_item->setFont(bold_font);
        }
    }

    // Resize headers
    m_table->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
}

u32 UserManagerDialog::GetUserKey() const {
    int row = m_table->currentRow();
    if (row < 0)
        return 0;
    const QTableWidgetItem* item = m_table->item(row, 0);
    if (!item)
        return 0;

    bool ok = false;
    u32 id = item->data(Qt::UserRole).toUInt(&ok);
    if (!ok)
        return 0;

    const auto& users = m_emu_settings->GetUserManager().GetAllUsers();
    auto it =
        std::find_if(users.begin(), users.end(), [id](const User& u) { return u.user_id == id; });
    return (it != users.end()) ? id : 0;
}

void UserManagerDialog::OnUserCreate() {
    auto& manager = m_emu_settings->GetUserManager();
    int smallest = 1;
    for (auto& u : manager.GetAllUsers()) {
        if (u.user_id == smallest)
            ++smallest;
        else
            break;
    }
    if (smallest > 16) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot add more users."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create New User"));
    dialog.setModal(true);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("New User ID: %1").arg(smallest)));
    layout->addWidget(new QLabel(tr("Username (3–16 chars, letters, numbers, _, -)")));
    QLineEdit* edit = new QLineEdit(&dialog);
    edit->setValidator(
        new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9_-]{3,16}$")));
    layout->addWidget(edit);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, [&]() {
        QString name = edit->text().trimmed();
        if (!edit->hasAcceptableInput()) {
            QMessageBox::warning(&dialog, tr("Invalid Username"),
                                 tr("Username must be 3–16 chars and valid."));
            return;
        }
        User u;
        u.user_id = smallest;
        u.user_name = name.toStdString();
        u.user_color = 0;
        u.controller_port = -1;
        manager.AddUser(u);
        UpdateTable();
        m_emu_settings->Save();
        dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void UserManagerDialog::OnUserRemove() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    auto& manager = m_emu_settings->GetUserManager();
    if (QMessageBox::question(this, tr("Delete Confirmation"), tr("Delete user ID %1?").arg(id),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) == QMessageBox::Yes) {
        manager.RemoveUser(id);
        UpdateTable();
        m_emu_settings->Save();
    }
}

void UserManagerDialog::OnUserRename() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    auto& manager = m_emu_settings->GetUserManager();
    User* user = manager.GetUserByID(id);
    if (!user)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Rename User"));
    dialog.setMinimumWidth(300);
    QVBoxLayout layout(&dialog);
    layout.addWidget(
        new QLabel(tr("Old Username: %1").arg(QString::fromStdString(user->user_name))));
    QLineEdit edit(QString::fromStdString(user->user_name));
    layout.addWidget(&edit);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);

    auto ok_button = buttons.button(QDialogButtonBox::Ok);
    ok_button->setEnabled(false);
    QRegularExpression regex("^[A-Za-z0-9_-]{3,16}$");
    QObject::connect(&edit, &QLineEdit::textChanged, [&]() {
        ok_button->setEnabled(regex.match(edit.text().trimmed()).hasMatch());
    });
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        manager.RenameUser(id, edit.text().trimmed().toStdString());
        UpdateTable();
        m_emu_settings->Save();
    }
}

void UserManagerDialog::OnUserSetDefault() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    auto& manager = m_emu_settings->GetUserManager();
    manager.SetDefaultUser(id);
    m_active_user = id;
    UpdateTable();
    m_emu_settings->Save();
}

void UserManagerDialog::OnUserSetColor() {
    u32 id = GetUserKey();
    if (id == 0)
        return;
    auto& manager = m_emu_settings->GetUserManager();
    User* user = manager.GetUserByID(id);
    if (!user)
        return;

    QStringList colors = {"Blue", "Red", "Green", "Pink"};
    bool ok = false;
    QString color = QInputDialog::getItem(this, tr("Set User Color"), tr("Select color:"), colors,
                                          user->user_color, false, &ok);
    if (ok) {
        user->user_color = colors.indexOf(color);
        UpdateTable();
        m_emu_settings->Save();
    }
}

void UserManagerDialog::OnUserSetControllerPort() {
    const u32 user_id = GetUserKey();
    if (user_id == 0)
        return;

    auto& manager = m_emu_settings->GetUserManager();

    // Current port of the selected user
    User* user = manager.GetUserByID(user_id);
    if (!user)
        return;

    bool ok = false;
    int new_port =
        QInputDialog::getInt(this, tr("Set Controller Port"), tr("Assign port (1-4) to this user:"),
                             user->controller_port > 0 ? user->controller_port : 1, // default
                             1, 4, 1, &ok);

    if (ok) {
        manager.SetControllerPort(user_id, new_port);
        UpdateTable();
        m_emu_settings->Save();
    }
}

void UserManagerDialog::closeEvent(QCloseEvent* event) {
    m_gui_settings->SetValue(gui::user_manager_geometry, saveGeometry());
    QDialog::closeEvent(event);
}
